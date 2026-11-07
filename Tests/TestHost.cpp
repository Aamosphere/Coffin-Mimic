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
    };

    void setParam (MimicProcessor& p, const char* id, float plain)
    {
        auto* param = p.parameters.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }

    RenderResult render (const juce::AudioBuffer<float>& input, const std::function<void (MimicProcessor&)>& setup,
                   const juce::AudioBuffer<float>* prey = nullptr, const juce::MidiBuffer* midi = nullptr)
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

        for (int start = 0; start < length; start += blockSize)
        {
            const int n = juce::jmin (blockSize, length - start);
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
            p.processBlock (block, m);

            // Engagement is reported per block, so a block next to an engaged
            // one may hold its fade: count both neighbours.
            const bool engagedNow = p.engageOut.load() > 0.0f || p.inFillOut.load();
            const bool engaged = engagedNow || wasEngaged;
            wasEngaged = engagedNow;

            res.kindsSeen |= 1u << juce::jlimit (0, 31, p.kindOut.load());

            if (p.inFillOut.load() && ! wasFill)
                ++res.fillsHeard;

            wasFill = p.inFillOut.load();

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

    const auto drumLoop = drums (16), lead = melody (16), chords = pad (16);
    const char* materials[] { "percussive", "melodic", "harmonic" };
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
    for (auto [name, buffer] : { std::pair<const char*, const juce::AudioBuffer<float>*> { "classify drums", &drumLoop },
                                 { "classify melody", &lead }, { "classify pad", &chords } })
    {
        auto r = render (*buffer, [] (MimicProcessor& p) { setParam (p, mimic::ids::mode, mimic::ids::modeFill); setParam (p, mimic::ids::probability, 0.0f); });
        report (name, r, true);
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
                                       { "melody", &lead }, { "pad", &chords } })
        {
            const juce::AudioBuffer<float>* prey = factory[i].tags.contains ("sidechain") ? (buffer == &drumLoop ? &lead : &drumLoop) : nullptr;
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

            const unsigned allowed = (1u << (int) mimic::Kind::pass) | (1u << (int) mimic::contentKinds[c]);
            const bool good = (r.kindsSeen & ~allowed) == 0 && (r.kindsSeen & (1u << (int) mimic::contentKinds[c])) != 0;
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

    std::printf (ok ? "ALL OK\n" : "SOME CHECKS FAILED\n");
    return ok ? 0 : 1;
}
