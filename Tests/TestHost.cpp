// Offline test host: renders synthetic loops through the Mimic with a fake
// transport, checks the rules (no NaN, no clicks, clean between fills,
// deterministic bounces) and writes WAV files to listen to.
//
// usage: MimicTest <output folder>
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr double bpm = 120.0;

    struct FakePlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0;
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

    float noise (juce::Random& r) { return r.nextFloat() * 2.0f - 1.0f; }

    // 16th-note drum loop: kick, snare, hats.
    juce::AudioBuffer<float> drums (int bars)
    {
        const int length = (int) (sampleRate * 60.0 / bpm * 4.0 * bars);
        juce::AudioBuffer<float> b (2, length);
        b.clear();
        juce::Random r (1);
        const double sixteenth = sampleRate * 60.0 / bpm / 4.0;
        const int kick[16]  { 1,0,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,0 };
        const int snare[16] { 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,1 };

        for (int s = 0; s < bars * 16; ++s)
        {
            const int start = (int) (s * sixteenth);
            const int i = s % 16;

            for (int n = 0; n < (int) (sampleRate * 0.4) && start + n < length; ++n)
            {
                const float t = (float) n / (float) sampleRate;
                float x = 0.0f;

                if (kick[i])
                    x += 0.8f * std::sin (juce::MathConstants<float>::twoPi * (50.0f * t + 90.0f * (1.0f - std::exp (-t * 30.0f)) / 30.0f)) * std::exp (-t * 7.0f);

                if (snare[i])
                    x += (0.45f * noise (r) + 0.3f * std::sin (juce::MathConstants<float>::twoPi * 190.0f * t)) * std::exp (-t * 18.0f);

                x += 0.12f * noise (r) * std::exp (-t * (i % 2 ? 90.0f : 45.0f));

                b.addSample (0, start + n, x);
                b.addSample (1, start + n, x * 0.97f);
            }
        }

        return b;
    }

    // Monophonic saw lead.
    juce::AudioBuffer<float> melody (int bars)
    {
        const int length = (int) (sampleRate * 60.0 / bpm * 4.0 * bars);
        juce::AudioBuffer<float> b (2, length);
        const int notes[8] { 60, 63, 67, 70, 72, 70, 67, 65 };
        const double eighth = sampleRate * 60.0 / bpm / 2.0;
        double phase = 0.0;

        for (int n = 0; n < length; ++n)
        {
            const int note = notes[(int) (n / eighth) % 8];
            const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            phase += f / sampleRate;
            phase -= std::floor (phase);
            const double t = std::fmod ((double) n, eighth) / sampleRate;
            const float env = (float) (std::min (1.0, t * 200.0) * std::exp (-t * 3.0));
            const float x = 0.35f * env * (float) (2.0 * phase - 1.0);
            b.setSample (0, n, x);
            b.setSample (1, n, x);
        }

        return b;
    }

    // Sustained minor-seventh pad, slightly detuned.
    juce::AudioBuffer<float> pad (int bars)
    {
        const int length = (int) (sampleRate * 60.0 / bpm * 4.0 * bars);
        juce::AudioBuffer<float> b (2, length);
        const double freqs[] { 220.0, 261.63, 329.63, 392.0, 221.1, 262.9 };
        double phases[6] {};

        for (int n = 0; n < length; ++n)
        {
            float x = 0.0f;

            for (int v = 0; v < 6; ++v)
            {
                phases[v] += freqs[v] / sampleRate;
                phases[v] -= std::floor (phases[v]);
                x += (float) (2.0 * phases[v] - 1.0);
            }

            const float slow = 0.6f + 0.4f * (float) std::sin (n / sampleRate * 0.7);
            b.setSample (0, n, 0.06f * x * slow);
            b.setSample (1, n, 0.06f * x * slow);
        }

        return b;
    }

    // A sung line: a buzzy source with vibrato and slow drift, gliding
    // between pitches that are off the piano's grid, through moving vowel
    // formants, in syllables with a breathy consonant at the start.
    juce::AudioBuffer<float> voice (int bars)
    {
        const int length = (int) (sampleRate * 60.0 / bpm * 4.0 * bars);
        juce::AudioBuffer<float> b (2, length);
        juce::Random r (7);
        const double targets[] { 196.0, 231.0, 262.0, 247.0, 205.0, 288.0, 262.0, 221.0 };
        const double syllable = sampleRate * 60.0 / bpm * 0.75;   // dotted eighths
        double phase = 0.0, f = targets[0];
        mimic::Svf formants[3];
        const float vowels[4][3] { { 730, 1090, 2440 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };

        for (int n = 0; n < length; ++n)
        {
            const int index = (int) (n / syllable);
            const double t = std::fmod ((double) n, syllable) / sampleRate;
            const double target = targets[index % 8] * (1.0 + 0.004 * std::sin (n / sampleRate * 0.9));
            f += (target - f) * (1.0 - std::exp (-1.0 / (0.04 * sampleRate)));
            const double vib = std::exp2 (0.35 / 12.0 * std::sin (juce::MathConstants<double>::twoPi * 5.6 * n / sampleRate) * std::min (1.0, t * 4.0));
            phase += f * vib / sampleRate;
            phase -= std::floor (phase);
            const float source = (float) (2.0 * phase - 1.0) * 0.6f + 0.05f * noise (r);

            if (n % 64 == 0)
                for (int k = 0; k < 3; ++k)
                    formants[k].set (vowels[index % 4][k], 6.0f, sampleRate);

            float x = 0.0f;

            for (int k = 0; k < 3; ++k)
                x += formants[k].process (0, source, 2) * (k == 0 ? 1.0f : 0.5f);

            const float consonant = t < 0.03 ? 0.25f * noise (r) * (float) (1.0 - t / 0.03) : 0.0f;
            const float env = (float) (std::min (1.0, t * 30.0) * std::min (1.0, (syllable / sampleRate - t) * 12.0));
            const float y = 0.5f * (x * env + consonant);
            b.setSample (0, n, y);
            b.setSample (1, n, y);
        }

        return b;
    }

    struct RenderResult
    {
        juce::AudioBuffer<float> out;
        bool finite = true;
        float peak = 0.0f, maxJump = 0.0f;
        int material = -1;
        double cleanError = 0.0;   // largest |out - in| outside fills (fill mode)
        int cleanWhere = -1;
        int fillsHeard = 0;
        unsigned kindsSeen = 0;    // one bit per mimic::Kind heard at block ends
        double firstFillPpq = -1.0;
        std::vector<double> fillStarts, possessAt;
        std::vector<std::pair<double, int>> kinds;   // block-end ppq and the kind playing
        int restBlocks = 0, loudRestBlocks = 0;   // blocks the shown script says are a Break
        float loudestRest = 0.0f;
    };

    // Called before every block when set: changes made while playing.
    std::function<void (MimicProcessor&, double ppq)> blockHook;

    void setParam (MimicProcessor& p, const char* id, float plain)
    {
        auto* param = p.parameters.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }

    RenderResult render (const juce::AudioBuffer<float>& input, const std::function<void (MimicProcessor&)>& setup,
                   const juce::AudioBuffer<float>* prey = nullptr, const juce::MidiBuffer* midi = nullptr,
                   double loopPpq = 0.0, double loopStartPpq = 0.0)
    {
        MimicProcessor p;

        if (prey != nullptr)
        {
            auto layout = p.getBusesLayout();
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            p.setBusesLayout (layout);
        }

        setup (p);
        FakePlayHead head;
        p.setPlayHead (&head);
        p.prepareToPlay (sampleRate, blockSize);

        RenderResult res;
        const int length = input.getNumSamples();
        res.out.setSize (2, length);

        const int channels = p.getTotalNumInputChannels();
        juce::AudioBuffer<float> block (juce::jmax (channels, 2), blockSize);
        bool wasFill = false, wasEngaged = false;

        for (int start = 0, n = 0; start < length; start += n)
        {
            n = juce::jmin (blockSize, length - start);

            // Hosts split the block where a cycle loop jumps back.
            if (loopPpq > 0.0)
            {
                const double loopSamples = loopPpq * 60.0 / bpm * sampleRate;
                const int toEnd = (int) std::llround (loopSamples - std::fmod ((double) start, loopSamples));
                n = juce::jmin (n, toEnd > 0 ? toEnd : n);
            }

            block.setSize (block.getNumChannels(), n, false, false, true);
            block.clear();

            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, input, ch, start, n);

            if (prey != nullptr && channels >= 4)
                for (int ch = 0; ch < 2; ++ch)
                    block.copyFrom (2 + ch, 0, *prey, ch, start, n);

            juce::MidiBuffer m;

            if (midi != nullptr)
                for (const auto meta : *midi)
                    if (meta.samplePosition >= start && meta.samplePosition < start + n)
                        m.addEvent (meta.getMessage(), meta.samplePosition - start);

            head.ppq = start / sampleRate * bpm / 60.0;

            if (loopPpq > 0.0)
                head.ppq = loopStartPpq + std::fmod (head.ppq, loopPpq);

            const int possessBefore = p.possessCount.load();

            if (blockHook)
                blockHook (p, head.ppq);

            p.processBlock (block, m);

            // Engagement is reported per block, so a block next to an engaged
            // one may hold its fade: count both neighbours.
            const bool engagedNow = p.engageOut.load() > 0.0f || p.inFillOut.load();
            const bool engaged = engagedNow || wasEngaged;
            wasEngaged = engagedNow;

            res.kindsSeen |= 1u << juce::jlimit (0, 31, p.kindOut.load());
            res.kinds.push_back ({ head.ppq, p.kindOut.load() });

            if (p.possessCount.load() != possessBefore)
                res.possessAt.push_back (p.lastPossessPpq.load());

            if (p.inFillOut.load() && ! wasFill)
            {
                ++res.fillsHeard;
                res.fillStarts.push_back (head.ppq);

                if (res.firstFillPpq < 0.0)
                    res.firstFillPpq = head.ppq;
            }

            wasFill = p.inFillOut.load();

            // Is this whole block inside a Break of the script on show?
            {
                const double blockPpq = n / sampleRate * bpm / 60.0;
                const double end = p.shownPosition.load(), begin = end - blockPpq;
                bool inRest = false;

                for (int k = 0; k < p.shownCount.load() && end >= 0.0 && begin >= 0.0; ++k)
                {
                    const auto packed = p.shownSegments[(size_t) k].load();
                    const double segStart = (packed & 0xfff) / 96.0, segLength = ((packed >> 12) & 0xfff) / 96.0;

                    if ((int) (packed >> 24) == (int) mimic::Kind::rest && begin >= segStart + 0.01 && end <= segStart + segLength - 0.01)
                        inRest = true;
                }

                if (inRest)
                {
                    float rms = 0.0f;
                    for (int i = 0; i < n; ++i)
                        rms += block.getSample (0, i) * block.getSample (0, i);
                    rms = std::sqrt (rms / (float) n);
                    ++res.restBlocks;
                    res.loudRestBlocks += rms > 0.05f ? 1 : 0;
                    res.loudestRest = juce::jmax (res.loudestRest, rms);
                }
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                res.out.copyFrom (ch, start, block, ch, 0, n);

                for (int i = 0; i < n; ++i)
                {
                    const float y = block.getSample (ch, i);

                    if (! std::isfinite (y))
                        res.finite = false;

                    res.peak = juce::jmax (res.peak, std::abs (y));

                    if (! engaged)
                    {
                        const double e = std::abs (y - input.getSample (ch, start + i));

                        if (e > res.cleanError)
                        {
                            res.cleanError = e;
                            res.cleanWhere = start + i;
                        }
                    }
                }
            }
        }

        // Clicks: the largest sample-to-sample jump beyond what the input had.
        float inputJump = 0.0f;

        for (int i = 1; i < length; ++i)
        {
            inputJump = juce::jmax (inputJump, std::abs (input.getSample (0, i) - input.getSample (0, i - 1)));
            res.maxJump = juce::jmax (res.maxJump, std::abs (res.out.getSample (0, i) - res.out.getSample (0, i - 1)));
        }

        res.maxJump /= juce::jmax (1.0e-6f, inputJump);
        res.material = p.materialOut.load();
        p.releaseResources();
        return res;
    }

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& b)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                           .withSampleRate (sampleRate)
                                                           .withNumChannels (2)
                                                           .withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File folder (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    folder.createDirectory();

    const auto drumLoop = drums (16), lead = melody (16), chords = pad (16), singer = voice (16);
    const char* materials[] { "percussive", "melodic", "harmonic", "voice" };
    bool ok = true;

    auto report = [&] (const juce::String& name, const RenderResult& r, bool fillMode)
    {
        const bool clean = ! fillMode || r.cleanError == 0.0;
        const bool good = r.finite && r.peak < 2.0f && clean;
        ok = ok && good;
        std::printf ("%-28s %s peak %.2f  jump x%.1f  material %-10s fills %d  clean-error %.2g @%d\n",
                     name.toRawUTF8(), good ? "ok  " : "FAIL", r.peak, r.maxJump,
                     r.material >= 0 ? materials[r.material] : "?", r.fillsHeard, r.cleanError, r.cleanWhere);
    };

    // Material recognition.
    {
        const juce::AudioBuffer<float>* buffers[] { &drumLoop, &lead, &chords, &singer };
        const char* names[] { "classify drums", "classify melody", "classify pad", "classify voice" };

        for (int m = 0; m < 4; ++m)
        {
            auto r = render (*buffers[m], [] (MimicProcessor& p) { setParam (p, mimic::ids::mode, mimic::ids::modeFill); setParam (p, mimic::ids::probability, 0.0f); });
            report (names[m], r, true);

            if (r.material != m)
            {
                std::printf ("   ^ FAIL: expected %s\n", materials[m]);
                ok = false;
            }
        }
    }

    // Breaks work at any Bite: with Breaks up, there are holes.
    for (float amount : { 0.0f, 1.0f })
    {
        auto r = render (drumLoop, [amount] (MimicProcessor& p)
        {
            setParam (p, mimic::ids::bite, 0.1f);
            setParam (p, mimic::ids::breaks, amount);
        });

        const bool rests = (r.kindsSeen & (1u << (int) mimic::Kind::rest)) != 0;
        const bool good = rests == (amount > 0.5f) && r.loudRestBlocks == 0;
        ok = ok && good;
        std::printf ("breaks %.0f %%, bite 10 %%         %s rests heard: %s, %d blocks in a Break, %d of them not silent (loudest rms %.3f)\n",
                     amount * 100.0f, good ? "ok  " : "FAIL", rests ? "yes" : "no", r.restBlocks, r.loudRestBlocks, r.loudestRest);
        writeWav (folder.getChildFile (juce::String ("breaks ") + (amount > 0.5f ? "100" : "0") + " - drums.wav"), r.out);
    }

    // Breaks stay silent whatever else is on.
    {
        struct Case { const char* name; std::function<void (MimicProcessor&)> extra; };
        const Case cases[] {
            { "bite 60", [] (MimicProcessor& p) { setParam (p, mimic::ids::bite, 0.6f); } },
            { "speed 1/32", [] (MimicProcessor& p) { setParam (p, mimic::ids::speed, 1.0f); } },
            { "speed 1/8", [] (MimicProcessor& p) { setParam (p, mimic::ids::speed, 0.3f); } },
            { "add mode", [] (MimicProcessor& p) { setParam (p, mimic::ids::replace, 0.0f); } },
            { "throw", [] (MimicProcessor& p) { setParam (p, mimic::ids::throwAmount, 0.6f); } },
            { "fill mode", [] (MimicProcessor& p) { setParam (p, mimic::ids::mode, mimic::ids::modeFill); setParam (p, mimic::ids::fillEvery, 0.0f); setParam (p, mimic::ids::fillLength, 2.0f); } },
            { "swing", [] (MimicProcessor& p) { setParam (p, mimic::ids::swing, 0.7f); } },
            { "throat", [] (MimicProcessor& p) { setParam (p, mimic::ids::filterType, 1.0f); setParam (p, mimic::ids::resonance, 0.8f); } },
            { "all chews", [] (MimicProcessor& p) { for (auto* id : mimic::ids::contentIds) setParam (p, id, 1.0f); setParam (p, mimic::ids::bite, 0.5f); } },
        };

        for (const auto& c : cases)
        {
            auto r = render (drumLoop, [&c] (MimicProcessor& p) { setParam (p, mimic::ids::breaks, 1.0f); c.extra (p); });
            const bool good = r.loudRestBlocks == 0 && r.maxJump < 1.6f;
            ok = ok && good;
            std::printf ("silent breaks, %-14s  %s %d blocks in a Break, %d not silent (loudest rms %.3f), jump x%.1f\n",
                         c.name, good ? "ok  " : "FAIL", r.restBlocks, r.loudRestBlocks, r.loudestRest, r.maxJump);
        }
    }

    // A looped bar: the same every pass, or new every pass. (The loop is
    // one bar of drums repeated, so every pass hears the same audio.)
    juce::AudioBuffer<float> oneBar (2, drumLoop.getNumSamples());

    for (int i = 0; i < oneBar.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            oneBar.setSample (ch, i, drumLoop.getSample (ch, i % (int) (sampleRate * 2.0)));

    for (int evolve = 0; evolve < 2; ++evolve)
    {
        auto r = render (oneBar, [evolve] (MimicProcessor& p)
        {
            setParam (p, mimic::ids::bite, 0.6f);
            setParam (p, mimic::ids::loop, (float) evolve);
        }, nullptr, nullptr, 4.0);

        const int bar = (int) (sampleRate * 2.0);
        int differing = 0;

        // From the third pass: on the first, Auto is still learning the track.
        for (int pass = 3; pass < 9; ++pass)
        {
            float diff = 0.0f;

            // Skip the first 20 ms: the loop jump crossfades.
            for (int i = (int) (0.02 * sampleRate); i < bar; ++i)
                diff = juce::jmax (diff, std::abs (r.out.getSample (0, pass * bar + i) - r.out.getSample (0, (pass - 1) * bar + i)));

            differing += diff > 0.01f ? 1 : 0;

            if (std::getenv ("MIMIC_DEBUG") != nullptr)
            {
                int where = 0; float d = 0.0f;
                for (int i = (int) (0.02 * sampleRate); i < bar; ++i)
                {
                    const float e = std::abs (r.out.getSample (0, pass * bar + i) - r.out.getSample (0, (pass - 1) * bar + i));
                    if (e > d) { d = e; where = i; }
                }
                std::printf ("   pass %d: max diff %.3f at %.3f ppq\n", pass, d, where / sampleRate * 2.0);
            }
        }

        const bool good = evolve ? differing >= 4 : differing == 0;
        ok = ok && good;
        std::printf ("looped bar, %-16s %s %d of 6 passes differ from the one before\n",
                     mimic::ids::loopNames[evolve].toRawUTF8(), good ? "ok  " : "FAIL", differing);
        writeWav (folder.getChildFile (juce::String ("looped bar - ") + mimic::ids::loopNames[evolve] + ".wav"), r.out);
    }

    // Phrases counted from bar 3: the first 4-bar fill is at the end of bar 6.
    {
        auto r = render (drumLoop, [] (MimicProcessor& p)
        {
            setParam (p, mimic::ids::mode, mimic::ids::modeFill);
            setParam (p, mimic::ids::fillEvery, 2.0f);
            setParam (p, mimic::ids::fillLength, (float) mimic::ids::lastBeat);
            setParam (p, mimic::ids::phraseStart, 2.0f);
        });

        const bool good = std::abs (r.firstFillPpq - 23.0) < 0.1;
        ok = ok && good;
        std::printf ("phrase start bar 3            %s first fill at ppq %.2f (want 23)\n", good ? "ok  " : "FAIL", r.firstFillPpq);
    }

    // Possess: at Bite 0 nothing happens on its own, but the track's own
    // hits fire events - deterministically.
    {
        auto setup = [] (float amount)
        {
            return [amount] (MimicProcessor& p)
            {
                setParam (p, mimic::ids::bite, 0.0f);
                setParam (p, mimic::ids::possess, amount);
            };
        };

        auto off = render (drumLoop, setup (0.0f));
        auto on = render (drumLoop, setup (1.0f)), again = render (drumLoop, setup (1.0f));
        const unsigned events = ~(1u << (int) mimic::Kind::pass);
        float diff = 0.0f;

        for (int i = 0; i < on.out.getNumSamples(); ++i)
            diff = juce::jmax (diff, std::abs (on.out.getSample (0, i) - again.out.getSample (0, i)));

        const bool good = (off.kindsSeen & events) == 0 && (on.kindsSeen & events) != 0 && diff == 0.0f
                       && on.finite && on.peak < 2.0f;
        ok = ok && good;
        std::printf ("possess 0 / 100 %%, bite 0      %s events 0x%03x / 0x%03x, jump x%.1f, rerender diff %g\n",
                     good ? "ok  " : "FAIL", off.kindsSeen & events, on.kindsSeen & events, on.maxJump, diff);
        writeWav (folder.getChildFile ("possess 100 - drums.wav"), on.out);
    }

    // Voice: drums at the main input, the singer at PREY IN. Vocoded, the
    // drums' level follows the singer; carved, it dips where she sings.
    {
        // The level of the voice's band, 300 Hz - 3 kHz, every 10 ms.
        auto envelope = [] (const juce::AudioBuffer<float>& b)
        {
            std::vector<float> e;
            const int hop = (int) (0.01 * sampleRate);
            mimic::Svf hp1, hp2, lp1, lp2;
            hp1.set (300.0f, 0.7071f, sampleRate);
            hp2.set (300.0f, 0.7071f, sampleRate);
            lp1.set (3000.0f, 0.7071f, sampleRate);
            lp2.set (3000.0f, 0.7071f, sampleRate);

            for (int i = 0; i + hop <= b.getNumSamples(); i += hop)
            {
                double sum = 0.0;
                for (int n = 0; n < hop; ++n)
                {
                    const float x = lp2.process (0, lp1.process (0, hp2.process (0, hp1.process (0, b.getSample (0, i + n), 1), 1), 0), 0);
                    sum += x * x;
                }
                e.push_back ((float) std::sqrt (sum / hop));
            }

            return e;
        };

        auto correlation = [] (const std::vector<float>& a, const std::vector<float>& b)
        {
            const size_t n = std::min (a.size(), b.size());
            double ma = 0, mb = 0;
            for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
            ma /= (double) n; mb /= (double) n;
            double sab = 0, saa = 0, sbb = 0;
            for (size_t i = 0; i < n; ++i)
            {
                sab += (a[i] - ma) * (b[i] - mb);
                saa += (a[i] - ma) * (a[i] - ma);
                sbb += (b[i] - mb) * (b[i] - mb);
            }
            return (float) (sab / std::sqrt (saa * sbb + 1.0e-20));
        };

        // The singer sings a beat, then rests a beat.
        auto phrased = singer;
        const double beat = sampleRate * 60.0 / bpm;

        for (int i = 0; i < phrased.getNumSamples(); ++i)
        {
            const double t = std::fmod ((double) i, 2.0 * beat) / beat;
            const float gate = (float) juce::jlimit (0.0, 1.0, std::min (t * 50.0, (1.0 - t) * 50.0));
            for (int ch = 0; ch < 2; ++ch)
                phrased.setSample (ch, i, phrased.getSample (ch, i) * gate);
        }

        const auto preyEnv = envelope (phrased);
        const float dry = correlation (envelope (drumLoop), preyEnv);

        // Carve is measured on a noise bed: it fills every band, so a hole
        // shows (the drums' kick leaks into any band filter).
        juce::AudioBuffer<float> bed (2, drumLoop.getNumSamples());
        {
            juce::Random r (11);
            for (int i = 0; i < bed.getNumSamples(); ++i)
            {
                const float x = 0.2f * noise (r);
                bed.setSample (0, i, x);
                bed.setSample (1, i, x);
            }
        }

        for (int carve = 0; carve < 2; ++carve)
        {
            const auto& track = carve ? bed : drumLoop;
            auto r = render (track, [carve] (MimicProcessor& p)
            {
                setParam (p, mimic::ids::bite, 0.0f);
                setParam (p, mimic::ids::prey, mimic::ids::preySidechain);
                setParam (p, mimic::ids::voice, 1.0f);
                setParam (p, mimic::ids::voiceInvert, (float) carve);
            }, &phrased);

            // How much the voice band of the drums gains (in dB, against the
            // dry drums) while the singer is loud, compared with while she
            // is quiet: up when vocoded, down when carved.
            const auto outEnv = envelope (r.out), dryEnv = envelope (track);
            auto sorted = preyEnv;
            std::sort (sorted.begin(), sorted.end());
            const float quiet = sorted[sorted.size() * 3 / 10], loud = sorted[sorted.size() * 7 / 10];
            double outLoud = 1.0e-12, dryLoud = 1.0e-12, outQuiet = 1.0e-12, dryQuiet = 1.0e-12;

            for (size_t i = 0; i < std::min (outEnv.size(), preyEnv.size()); ++i)
            {
                if (preyEnv[i] >= loud)  { outLoud += outEnv[i] * outEnv[i]; dryLoud += dryEnv[i] * dryEnv[i]; }
                if (preyEnv[i] <= quiet) { outQuiet += outEnv[i] * outEnv[i]; dryQuiet += dryEnv[i] * dryEnv[i]; }
            }

            if (std::getenv ("MIMIC_DEBUG") != nullptr)
                std::printf ("   loud: out/dry %.1f dB, quiet: out/dry %.1f dB (thresholds %.4f / %.4f)\n",
                             10.0 * std::log10 (outLoud / dryLoud), 10.0 * std::log10 (outQuiet / dryQuiet), quiet, loud);

            const float contrast = (float) (10.0 * std::log10 ((outLoud / dryLoud) / (outQuiet / dryQuiet)));
            const float c = correlation (outEnv, preyEnv);
            const bool good = r.finite && r.peak < 2.0f && (carve ? contrast < -6.0f : contrast > 10.0f);
            ok = ok && good;
            std::printf ("voice 100 %%, %-6s             %s singing vs quiet %+.1f dB, envelope corr %.2f (dry %.2f), peak %.2f, jump x%.1f\n",
                         carve ? "carve" : "vocode", good ? "ok  " : "FAIL", contrast, c, dry, r.peak, r.maxJump);
            writeWav (folder.getChildFile (juce::String ("voice 100 ") + (carve ? "carve - noise" : "vocode - drums") + " with singer.wav"), r.out);
        }
    }

    // A cycle loop over bars 2-3 with a fill every 2 bars: the fill is at the
    // end of bar 2 on every pass, never in bar 3.
    for (int evolve = 0; evolve < 2; ++evolve)
    {
        auto r = render (drumLoop, [evolve] (MimicProcessor& p)
        {
            setParam (p, mimic::ids::mode, mimic::ids::modeFill);
            setParam (p, mimic::ids::fillEvery, 1.0f);
            setParam (p, mimic::ids::fillLength, (float) mimic::ids::lastBeat);
            setParam (p, mimic::ids::loop, (float) evolve);
        }, nullptr, nullptr, 8.0, 4.0);

        int wrong = 0;
        for (auto f : r.fillStarts)
            wrong += (f >= 7.0 - 0.05 && f < 8.0) ? 0 : 1;

        const bool good = r.fillStarts.size() >= 6 && wrong == 0;
        ok = ok && good;
        std::printf ("loop bars 2-3, fills / 2 bars  %s %s: %d fills, %d not at the end of bar 2\n", good ? "ok  " : "FAIL",
                     mimic::ids::loopNames[evolve].toRawUTF8(), (int) r.fillStarts.size(), wrong);
    }

    // Possess keeps time: a prey that hits off the grid fires events on it.
    {
        juce::AudioBuffer<float> sloppy (2, drumLoop.getNumSamples());
        sloppy.clear();
        juce::Random rr (3);
        const double sixteenth = sampleRate * 60.0 / bpm / 4.0;

        for (int h = 0; h < (int) (sloppy.getNumSamples() / sixteenth) - 1; h += 3)
        {
            const int at = (int) (h * sixteenth + rr.nextFloat() * sixteenth * 0.8);
            for (int n = 0; n < 2000 && at + n < sloppy.getNumSamples(); ++n)
            {
                const float x = 0.6f * noise (rr) * std::exp (-(float) n / 300.0f);
                sloppy.setSample (0, at + n, x);
                sloppy.setSample (1, at + n, x);
            }
        }

        auto r = render (drumLoop, [] (MimicProcessor& p)
        {
            setParam (p, mimic::ids::bite, 0.0f);
            setParam (p, mimic::ids::prey, mimic::ids::preySidechain);
            setParam (p, mimic::ids::possess, 1.0f);
        }, &sloppy);

        int offGrid = 0;
        for (auto at : r.possessAt)
        {
            const double steps = at / 0.25;
            const double into = steps - std::floor (steps + 0.001);
            const bool off = into >= 0.21;
            offGrid += off ? 1 : 0;

            if (off && std::getenv ("MIMIC_DEBUG") != nullptr)
                std::printf ("   off grid at ppq %.4f\n", at);
        }

        const bool good = r.possessAt.size() > 20 && offGrid == 0;
        ok = ok && good;
        std::printf ("possess on a sloppy prey       %s %d events, %d off the 1/16 grid\n", good ? "ok  " : "FAIL", (int) r.possessAt.size(), offGrid);
    }

    // Dynamics: the same pattern with more level differences, still the same
    // on every bounce.
    {
        auto setup = [] (float d)
        {
            return [d] (MimicProcessor& p) { setParam (p, mimic::ids::bite, 0.7f); setParam (p, mimic::ids::dynamics, d); };
        };

        auto flat = render (drumLoop, setup (0.0f)), dyn = render (drumLoop, setup (1.0f)), again = render (drumLoop, setup (1.0f));
        float diff = 0.0f, rerender = 0.0f;

        for (int i = 0; i < dyn.out.getNumSamples(); ++i)
        {
            diff = juce::jmax (diff, std::abs (dyn.out.getSample (0, i) - flat.out.getSample (0, i)));
            rerender = juce::jmax (rerender, std::abs (dyn.out.getSample (0, i) - again.out.getSample (0, i)));
        }

        const bool good = diff > 0.05f && rerender == 0.0f && dyn.finite && dyn.maxJump < 1.6f;
        ok = ok && good;
        std::printf ("dynamics 0 / 100 %%             %s max difference %.2f, rerender %g, jump x%.1f\n", good ? "ok  " : "FAIL", diff, rerender, dyn.maxJump);
        writeWav (folder.getChildFile ("dynamics 100 - drums.wav"), dyn.out);
    }

    // Each material has its own versions: shards and bends on a melody, fog
    // on a pad. Echo rings on with any of them.
    {
        struct Case { const char* name; const juce::AudioBuffer<float>* in; unsigned want; };
        const Case cases[] {
            { "melody", &lead, (1u << (int) mimic::Kind::shards) | (1u << (int) mimic::Kind::bend) },
            { "pad", &chords, 1u << (int) mimic::Kind::fog },
            { "voice", &singer, 1u << (int) mimic::Kind::bend },
        };

        for (const auto& c : cases)
        {
            auto r = render (*c.in, [] (MimicProcessor& p)
            {
                setParam (p, mimic::ids::bite, 0.7f);
                for (auto* id : mimic::ids::contentIds)
                    setParam (p, id, 0.0f);
                for (auto* id : { mimic::ids::useStutter, mimic::ids::useRoll, mimic::ids::useGrains })
                    setParam (p, id, 1.0f);
            });

            const bool good = (r.kindsSeen & c.want) == c.want && r.finite && r.peak < 2.0f && r.maxJump < 2.0f;
            ok = ok && good;
            std::printf ("%-8s own versions        %s heard 0x%04x, peak %.2f, jump x%.1f\n", c.name, good ? "ok  " : "FAIL", r.kindsSeen, r.peak, r.maxJump);
            writeWav (folder.getChildFile (juce::String ("material versions - ") + c.name + ".wav"), r.out);
        }

        for (auto [name, in] : { std::pair<const char*, const juce::AudioBuffer<float>*> { "drums", &drumLoop }, { "melody", &lead }, { "voice", &singer } })
        {
            auto r = render (*in, [] (MimicProcessor& p)
            {
                setParam (p, mimic::ids::bite, 0.6f);
                for (auto* id : mimic::ids::contentIds)
                    setParam (p, id, 0.0f);
                setParam (p, mimic::ids::useEcho, 1.0f);
                setParam (p, mimic::ids::useMash, 1.0f);
            });

            float diff = 0.0f;
            for (int i = 0; i < r.out.getNumSamples(); ++i)
                diff = juce::jmax (diff, std::abs (r.out.getSample (0, i) - in->getSample (0, i)));

            const bool good = (r.kindsSeen & (1u << (int) mimic::Kind::echo)) != 0 && diff > 0.05f && r.finite && r.peak < 2.0f;
            ok = ok && good;
            std::printf ("echo on %-8s             %s heard 0x%04x, peak %.2f, jump x%.1f\n", name, good ? "ok  " : "FAIL", r.kindsSeen, r.peak, r.maxJump);
            writeWav (folder.getChildFile (juce::String ("echo - ") + name + ".wav"), r.out);
        }
    }

    // Switching the material while playing changes the pattern at once:
    // the melody played as drums for 4 bars, then picked as a melody.
    {
        blockHook = [] (MimicProcessor& p, double ppq)
        {
            setParam (p, mimic::ids::material, ppq < 16.0 ? (float) mimic::ids::materialPercussive : (float) mimic::ids::materialMelodic);
        };

        auto r = render (lead, [] (MimicProcessor& p)
        {
            setParam (p, mimic::ids::bite, 0.5f);
            setParam (p, mimic::ids::variation, 3.0f);   // a pattern holds 8 bars
        });
        blockHook = nullptr;

        double firstOwn = -1.0;
        int ownBefore = 0;
        for (auto [ppq, kind] : r.kinds)
        {
            const bool own = kind == (int) mimic::Kind::shards || kind == (int) mimic::Kind::bend;
            if (own && ppq < 16.0) ++ownBefore;
            if (own && ppq >= 16.0 && firstOwn < 0.0) firstOwn = ppq;
        }

        const bool good = ownBefore == 0 && firstOwn >= 16.0 && firstOwn < 20.0;
        ok = ok && good;
        std::printf ("material switched at ppq 16    %s first melody version at ppq %.2f (pattern holds 8 bars)\n", good ? "ok  " : "FAIL", firstOwn);
    }

    // How much of each material's own versions is heard, by Bite.
    for (float bite : { 0.1f, 0.3f, 0.6f })
    {
        std::printf ("bite %3.0f %%: ", bite * 100.0f);

        for (auto [name, in] : { std::pair<const char*, const juce::AudioBuffer<float>*> { "melody", &lead }, { "pad", &chords }, { "voice", &singer } })
        {
            auto r = render (*in, [bite] (MimicProcessor& p) { setParam (p, mimic::ids::bite, bite); });
            int fx = 0, own = 0;
            for (auto [ppq, kind] : r.kinds)
            {
                fx += kind != 0 ? 1 : 0;
                own += kind >= (int) mimic::Kind::shards && kind <= (int) mimic::Kind::fog ? 1 : 0;
            }
            std::printf ("%s effects %2d %%, own versions %2d %%   ", name, 100 * fx / (int) r.kinds.size(), 100 * own / (int) r.kinds.size());
        }

        std::printf ("\n");
    }

    // Every factory preset on the drum loop, the melody and the pad.
    const auto& factory = mimic::PresetManager::getFactory();

    for (int i = 0; i < factory.size(); ++i)
    {
        const bool midiPrey = factory[i].tags.contains ("midi");
        juce::MidiBuffer notes;

        if (midiPrey)
            for (int b = 0; b < 64; ++b)
            {
                const int pos = (int) (b * sampleRate * 0.5);
                constexpr int intervals[] { 0, 7, 12, 3, -5, 0, 10, 5 };
                const int note = 60 + intervals[b % 8];
                notes.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), pos);
                notes.addEvent (juce::MidiMessage::noteOff (1, note), pos + (int) (sampleRate * 0.2));
            }

        for (auto [suffix, buffer] : { std::pair<const char*, const juce::AudioBuffer<float>*> { "drums", &drumLoop },
                                       { "melody", &lead }, { "pad", &chords }, { "voice", &singer } })
        {
            const bool sung = factory[i].tags.contains ("vocoder") || factory[i].tags.contains ("ducking");
            const juce::AudioBuffer<float>* prey = ! factory[i].tags.contains ("sidechain") ? nullptr
                                                 : sung ? (buffer == &singer ? &lead : &singer)
                                                        : (buffer == &drumLoop ? &lead : &drumLoop);
            auto r = render (*buffer, [i] (MimicProcessor& p) { p.presets.loadFactory (i); }, prey, midiPrey ? &notes : nullptr);
            // Echo presets let the reverb tail ring on after a fill.
            report (factory[i].name + " / " + suffix, r, factory[i].fill && ! factory[i].tags.contains ("echo"));
            writeWav (folder.getChildFile (juce::String (i).paddedLeft ('0', 2) + " " + factory[i].name + " - " + suffix + ".wav"), r.out);
        }
    }

    // Nothing leaks: with one content switch on, only that content plays.
    for (int c = 0; c < mimic::numContent; ++c)
    {
        for (int fillMode = 0; fillMode < 2; ++fillMode)
        {
            auto r = render (drumLoop, [c, fillMode] (MimicProcessor& p)
            {
                setParam (p, mimic::ids::mode, fillMode ? mimic::ids::modeFill : mimic::ids::modeWild);
                setParam (p, mimic::ids::bite, 1.0f);
                setParam (p, mimic::ids::fillEvery, 0.0f);
                setParam (p, mimic::ids::fillLength, (float) mimic::ids::lastBar);

                for (int i = 0; i < mimic::numContent; ++i)
                    setParam (p, mimic::ids::contentIds[i], i == c ? 1.0f : 0.0f);
            });

            unsigned allowed = 1u << (int) mimic::Kind::pass;

            for (int k = 0; k < 32; ++k)
                if (mimic::belongsTo ((mimic::Kind) k, mimic::contentKinds[c]))
                    allowed |= 1u << k;

            const bool good = (r.kindsSeen & ~allowed) == 0 && (r.kindsSeen & allowed & ~1u) != 0;
            ok = ok && good;
            std::printf ("only %-8s %-5s %s heard 0x%03x\n", mimic::ids::contentNames[c].toRawUTF8(),
                         fillMode ? "fill" : "wild", good ? "ok  " : "FAIL", r.kindsSeen);
        }
    }

    // Deterministic: two renders of the same thing are identical.
    {
        auto setup = [] (MimicProcessor& p) { p.presets.loadFactory (6); };
        auto a = render (drumLoop, setup), b = render (drumLoop, setup);
        float diff = 0.0f;

        for (int i = 0; i < a.out.getNumSamples(); ++i)
            diff = juce::jmax (diff, std::abs (a.out.getSample (0, i) - b.out.getSample (0, i)));

        std::printf ("%-28s %s max difference %g\n", "deterministic bounce", diff == 0.0f ? "ok  " : "FAIL", diff);
        ok = ok && diff == 0.0f;
    }

    writeWav (folder.getChildFile ("dry - drums.wav"), drumLoop);
    writeWav (folder.getChildFile ("dry - melody.wav"), lead);
    writeWav (folder.getChildFile ("dry - pad.wav"), chords);
    writeWav (folder.getChildFile ("dry - voice.wav"), singer);

    std::printf (ok ? "ALL OK\n" : "SOME CHECKS FAILED\n");
    return ok ? 0 : 1;
}
