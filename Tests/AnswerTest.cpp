// The answers on real recordings: MimicAnswerTest <samples folder> <output folder>
//
// Every sample is played as call and response: a stretch of it, a gap, the
// rest, a gap. Checks that the Mimic answers in the gaps, steps back when the
// player comes in, never clicks, never runs away, and writes the renders.
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

namespace
{
    struct FakePlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0, bpm = 120.0;
        bool playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            info.setIsPlaying (playing);
            return info;
        }
    };

    void setParam (MimicProcessor& p, const char* id, float plain)
    {
        auto* param = p.parameters.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }

    struct Result
    {
        juce::AudioBuffer<float> out;
        float peak = 0.0f, worstClick = 0.0f, gapEnergy = 0.0f, playEnergyAdded = 0.0f;
        int answers = 0, clicks = 0;
        unsigned tricks = 0;
        double seconds = 0.0, realtime = 0.0;
        bool finite = true;
    };

    // Clicks: a second difference far above the local level of second
    // differences around it.
    int countClicks (const juce::AudioBuffer<float>& b, double sr, float& worst, double limit = 30.0)
    {
        const int n = b.getNumSamples();
        const int window = (int) (0.01 * sr);
        int clicks = 0;
        worst = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* x = b.getReadPointer (ch);
            std::vector<float> d2 ((size_t) n, 0.0f);

            for (int i = 2; i < n; ++i)
                d2[(size_t) i] = std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]);

            double sum = 0.0;
            int lastEvent = -1000000;

            for (int i = 0; i < n; ++i)
            {
                sum += (double) d2[(size_t) i];

                if (i >= window)
                    sum -= (double) d2[(size_t) (i - window)];

                if (i < window * 2)
                    continue;

                // Compare a sample with the window before it.
                const int at = i - window / 2;
                const double before = (sum - d2[(size_t) at]) / (window - 1);
                const float v = d2[(size_t) at];

                if (v > 0.03f && v > limit * before)
                {
                    // One count per event (the running sum is kept whole).
                    if (at > lastEvent + window)
                    {
                        ++clicks;

                        if (std::getenv ("VERBOSE"))
                            std::printf ("      click at %.3fs ch %d  x%.0f  value %.3f\n", at / sr, ch, v / juce::jmax (1.0e-6, before), v);
                    }

                    lastEvent = at;
                    worst = juce::jmax (worst, (float) (v / juce::jmax (1.0e-6, before)));
                }
            }
        }

        return clicks;
    }

    Result render (const juce::AudioBuffer<float>& input, double sr, const std::function<void (MimicProcessor&)>& setup,
                   const std::vector<std::pair<int, int>>& gaps)
    {
        MimicProcessor p;
        setup (p);
        FakePlayHead head;
        p.setPlayHead (&head);
        const int blockSize = 512;
        p.prepareToPlay (sr, blockSize);

        Result res;
        const int length = input.getNumSamples();
        res.out.setSize (2, length);
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        int lastCount = 0;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();

        for (int start = 0; start < length; start += blockSize)
        {
            const int n = juce::jmin (blockSize, length - start);
            block.setSize (2, n, false, false, true);

            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, input, ch, start, n);

            head.ppq = start / sr * head.bpm / 60.0;
            p.processBlock (block, midi);

            for (int ch = 0; ch < 2; ++ch)
                res.out.copyFrom (ch, start, block, ch, 0, n);

            const int c = p.answerCountOut.load();
            static int lastState = -1;

            if (std::getenv ("VERBOSE") && p.answerStateOut.load() != lastState)
            {
                lastState = p.answerStateOut.load();
                std::printf ("      %.2fs state %d  input %.3f\n", start / sr, lastState, p.inputLevelOut.load());
            }

            if (c != lastCount && std::getenv ("VERBOSE"))
                std::printf ("    answer %d at %.2fs: trick %d\n", c, start / sr, p.answerTrickOut.load());

            if (c != lastCount)
            {
                lastCount = c;
                const int t = p.answerTrickOut.load();
                if (t >= 0)
                    res.tricks |= 1u << t;
            }
        }

        res.realtime = (length / sr) / ((juce::Time::getMillisecondCounterHiRes() - t0) * 0.001);
        res.answers = lastCount;
        res.seconds = length / sr;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < length; ++i)
            {
                const float y = res.out.getSample (ch, i);
                res.finite = res.finite && std::isfinite (y);
                res.peak = juce::jmax (res.peak, std::abs (y));
            }

        // How much the Mimic says in the gaps, against what it adds while
        // the player plays.
        double gapSum = 0.0, playSum = 0.0;
        int gapCount = 0, playCount = 0;

        for (int i = 0; i < length; ++i)
        {
            bool inGap = false;

            for (const auto& g : gaps)
                inGap = inGap || (i >= g.first && i < g.second);

            const float added = res.out.getSample (0, i) - input.getSample (0, i);

            if (inGap)
            {
                gapSum += (double) added * added;
                ++gapCount;
            }
            else
            {
                playSum += (double) added * added;
                ++playCount;
            }
        }

        res.gapEnergy = (float) std::sqrt (gapSum / juce::jmax (1, gapCount));
        res.playEnergyAdded = (float) std::sqrt (playSum / juce::jmax (1, playCount));
        // Clicks in what the Mimic added (the drums have sharp edges of
        // their own).
        juce::AudioBuffer<float> added (2, length);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < length; ++i)
                added.setSample (ch, i, res.out.getSample (ch, i) - input.getSample (ch, i));

        // ...but a replayed hit may be as sharp as the sharpest in the
        // recording itself.
        float inputWorst = 0.0f;
        countClicks (input, sr, inputWorst, 14.0);
        res.clicks = countClicks (added, sr, res.worstClick, juce::jmax (30.0, 1.15 * inputWorst));
        p.releaseResources();
        return res;
    }

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& b, double sr)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                           .withSampleRate (sr)
                                                           .withNumChannels (2)
                                                           .withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 3)
    {
        std::printf ("usage: MimicAnswerTest <samples folder> <output folder> [only this file]\n");
        return 1;
    }

    const juce::File samples { juce::String (argv[1]) }, out { juce::String (argv[2]) };
    const juce::String only = argc > 3 ? juce::String (argv[3]) : juce::String();
    out.createDirectory();
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    bool ok = true;

    auto files = samples.findChildFiles (juce::File::findFiles, false, "*.wav");
    files.sort();

    const char* materials[] { "drums", "line", "chords", "voice" };

    for (const auto& f : files)
    {
        if (only.isNotEmpty() && ! f.getFileName().contains (only))
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));

        if (reader == nullptr)
            continue;

        const double sr = reader->sampleRate;
        const int len = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> src (2, len);
        reader->read (&src, 0, len, 0, true, true);

        // Call and response: the first half, a gap, the second half, a gap.
        const int half = len / 2, gap1 = (int) (2.5 * sr), gap2 = (int) (3.5 * sr);
        juce::AudioBuffer<float> call (2, len + gap1 + gap2);
        call.clear();

        for (int ch = 0; ch < 2; ++ch)
        {
            call.copyFrom (ch, 0, src, ch, 0, half);
            call.copyFrom (ch, half + gap1, src, ch, half, len - half);
        }

        // A short fade at the cuts, so the input itself does not click.
        for (int ch = 0; ch < 2; ++ch)
        {
            const int fade = (int) (0.01 * sr);

            for (int i = 0; i < fade; ++i)
            {
                const float g = (float) i / (float) fade;
                call.setSample (ch, half - 1 - i, call.getSample (ch, half - 1 - i) * g);
                call.setSample (ch, half + gap1 + i, call.getSample (ch, half + gap1 + i) * g);
                call.setSample (ch, len + gap1 - 1 - i, call.getSample (ch, len + gap1 - 1 - i) * g);
            }
        }

        const std::vector<std::pair<int, int>> gaps { { half + (int) (0.3 * sr), half + gap1 }, { len + gap1 + (int) (0.3 * sr), len + gap1 + gap2 } };
        const auto name = f.getFileNameWithoutExtension();

        // Every trick on its own, then all of them.
        for (int t = -1; t < mimic::numTricks; ++t)
        {
            for (float mockery : { 0.2f, 0.8f })
            {
                if (t >= 0 && mockery > 0.5f && only.isEmpty())
                    continue;

                auto r = render (call, sr, [t, mockery] (MimicProcessor& p)
                {
                    setParam (p, mimic::ids::mockery, mockery);
                    setParam (p, mimic::ids::hunger, 0.6f);

                    for (int k = 0; k < mimic::numTricks; ++k)
                        setParam (p, mimic::ids::trickIds[k], t < 0 || k == t ? 1.0f : 0.0f);
                }, gaps);

                // It answers, the answers live in the gaps, nothing clicks.
                const bool answered = r.answers >= 1 && r.gapEnergy > 0.003f;
                const bool good = r.finite && r.peak < 1.5f && r.clicks == 0 && answered;
                ok = ok && good;

                const juce::String label = t < 0 ? juce::String ("all") : mimic::ids::trickNames[t];
                std::printf ("%-14s %-10s mock %.1f  %s answers %d tricks 0x%02x  gap %.3f  added while playing %.3f  peak %.2f  clicks %d (x%.0f)  %.0fx realtime\n",
                             name.toRawUTF8(), label.toRawUTF8(), mockery, good ? "ok  " : "FAIL", r.answers, r.tricks,
                             r.gapEnergy, r.playEnergyAdded, r.peak, r.clicks, r.worstClick, r.realtime);

                writeWav (out.getChildFile (name + " - " + label + (mockery > 0.5f ? " mock 80" : "") + ".wav"), r.out, sr);
            }
        }

        // Snares with Breaks on the real recording, looped four times, no
        // answers: to hear the cuts.
        if (only.isEmpty() && (name.startsWith ("rummut") || name.startsWith ("Output") || name.startsWith ("kitara2")))
        {
            juce::AudioBuffer<float> loop4 (2, len * 4);

            for (int k = 0; k < 4; ++k)
                for (int ch = 0; ch < 2; ++ch)
                    loop4.copyFrom (ch, k * len, src, ch, 0, len);

            for (float bite : { 0.25f, 0.6f })
            {
                auto r = render (loop4, sr, [bite] (MimicProcessor& p)
                {
                    setParam (p, mimic::ids::answer, 0.0f);
                    setParam (p, mimic::ids::snares, 1.0f);
                    setParam (p, mimic::ids::bite, bite);
                    setParam (p, mimic::ids::breaks, 0.8f);
                }, {});

                const bool good = r.finite && r.peak < 1.5f;
                ok = ok && good;
                std::printf ("%-14s snares %2.0f %% + breaks  %s peak %.2f  %.0fx realtime\n", name.toRawUTF8(), bite * 100.0f, good ? "ok  " : "FAIL", r.peak, r.realtime);
                writeWav (out.getChildFile (name + " - SNARES " + juce::String (juce::roundToInt (bite * 100.0f)) + " breaks.wav"), r.out, sr);
            }
        }

        juce::ignoreUnused (materials);
    }

    std::printf (ok ? "\nALL OK\n" : "\nSOME FAILED\n");
    return ok ? 0 : 1;
}
