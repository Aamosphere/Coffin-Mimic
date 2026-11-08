// Runs audio files through the material classifier: MimicClassify <files...>
#include <JuceHeader.h>
#include "../Source/Engine.h"

int main (int argc, char** argv)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const char* names[] { "PERC", "MELO", "HARM", "VOIC" };

    for (int a = 1; a < argc; ++a)
    {
        const juce::File f { juce::String (argv[a]) };
        juce::AudioBuffer<float> buf;
        double sr = 48000.0;

        if (juce::String (argv[a]) == "synthpad")
        {
            buf.setSize (2, (int) (sr * 8));
            const double freqs[] { 220.0, 261.63, 329.63, 392.0, 221.1, 262.9 };
            double phases[6] {};
            for (int n = 0; n < buf.getNumSamples(); ++n)
            {
                float x = 0.0f;
                for (int v = 0; v < 6; ++v) { phases[v] += freqs[v] / sr; phases[v] -= std::floor (phases[v]); x += (float) (2.0 * phases[v] - 1.0); }
                const float slow = 0.6f + 0.4f * (float) std::sin (n / sr * 0.7);
                buf.setSample (0, n, 0.06f * x * slow); buf.setSample (1, n, 0.06f * x * slow);
            }
        }
        else
        {
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
            if (! reader) { std::printf ("cannot read %s\n", argv[a]); continue; }
            buf.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);
            sr = reader->sampleRate;
        }

        mimic::OnsetDetector det; det.prepare (sr);
        mimic::Classifier cls; cls.prepare (sr);
        juce::String line;
        float peak = 0;

        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < buf.getNumChannels(); ++c) m += buf.getSample (c, i);
            m /= (float) buf.getNumChannels();
            peak = juce::jmax (peak, std::abs (m));
            cls.push (m, det.process (m));

            if (i % (int) (sr / 2) == 0 && i > 0)
            {
                line << names[cls.material()][0];
                if (std::getenv ("SERIES"))
                    std::printf ("  %4.1fs %s tonal %.2f flat %.3f low %.2f clar %.2f drift %4.1f wob %4.1f\n", i / sr, names[cls.material()], cls.tonal, cls.flatness, cls.lowShare, cls.clarity, cls.pitchDrift, cls.pitchWobble);
            }
        }

        std::printf ("%-16s %s clar %.2f drift %4.1f wob %4.1f | tonal %.2f flat %.3f low %.2f pk %.2f [%s]\n",
                     f.getFileName().toRawUTF8(), names[cls.material()], cls.clarity,
                     cls.pitchDrift, cls.pitchWobble, cls.tonal, cls.flatness, cls.lowShare, peak, line.toRawUTF8());
    }
}
