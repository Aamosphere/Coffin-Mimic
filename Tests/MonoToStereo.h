#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstdio>

// The host's "Mono -> Stereo" insert: one channel in, two out, with the
// sidechain bus (when there is one) enabled.
//
// The host hands over one buffer in which the output's right channel and the
// sidechain's left channel are the same memory. Here the sidechain is kept
// silent, so a plugin that forgets to copy its mono input to the right
// channel comes out silent on the right and fails.
template <typename Processor>
bool checkMonoToStereo (const char* name)
{
    Processor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());

    for (int b = 1; b < p.getBusCount (true); ++b)
        layout.inputBuses.add (juce::AudioChannelSet::stereo());

    layout.outputBuses.add (juce::AudioChannelSet::stereo());

    const bool accepted = p.setBusesLayout (layout);
    const double sr = 48000.0;
    const int block = 512;
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);

    const int channels = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (channels, block);
    juce::MidiBuffer midi;
    double sumL = 0.0, sumR = 0.0;
    bool finite = true;
    int64_t t = 0;

    for (int k = 0; k < 400; ++k)
    {
        buffer.clear();

        // A plucked note every half second on the track; the sidechain silent.
        for (int i = 0; i < block; ++i, ++t)
        {
            const double inNote = (double) (t % 24000) / sr;
            buffer.setSample (0, i, (float) (0.4 * std::exp (-inNote * 4.0) * std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) t / sr)));
        }

        p.processBlock (buffer, midi);

        if (k >= 100)
            for (int i = 0; i < block; ++i)
            {
                const float l = buffer.getSample (0, i), r = buffer.getSample (1, i);
                finite = finite && std::isfinite (l) && std::isfinite (r);
                sumL += (double) l * l;
                sumR += (double) r * r;
            }
    }

    p.releaseResources();
    const double rmsL = std::sqrt (sumL / (300.0 * block)), rmsR = std::sqrt (sumR / (300.0 * block));
    const bool good = accepted && finite && rmsL > 1.0e-4 && rmsR > 0.25 * rmsL;
    std::printf ("%-28s %s layout %s, left %.1f dB, right %.1f dB\n", name, good ? "ok  " : "FAIL",
                 accepted ? "accepted" : "REFUSED",
                 juce::Decibels::gainToDecibels ((float) rmsL), juce::Decibels::gainToDecibels ((float) rmsR));
    return good;
}
