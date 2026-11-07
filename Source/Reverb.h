// Copied from Gargoyle (Aamosphere/Gargoyle Source/Reverb.*): the Throw send
// uses the Gargoyle's own space. Keep the two copies in step.
#pragma once

#include <JuceHeader.h>
#include <array>

namespace gargoyle
{
    // The spaces the Gargoyle guards. Early reflections come from the room's
    // shape (image sources); the tail's delay range and colour come from its
    // character.
    struct Space
    {
        float length, width, height;   // metres, at size factor 1
        float reflect;                 // wall reflection gain
        bool openRoof;                 // no ceiling reflections
        float lineMinMs, lineMaxMs;    // tail delay lines, at size factor 1
        float air;                     // early reflection lowpass, Hz
        float lows, highs;             // base decay ratios at the band edges
    };

    const Space& space (int index);
    int numSpaces();

    // Size knob (0..1) to the factor that scales the room.
    inline float sizeFactor (float size) { return 0.4f * std::pow (4.0f, size); }

    //==========================================================================
    // Early reflections plus a 16-line feedback delay network. Produces the
    // wet signal only; the processor mixes it with the dry.
    class Reverb
    {
    public:
        struct Settings
        {
            int space = 0;
            float size = 0.6f;          // 0..1
            float decay = 3.0f;         // RT60 at mid frequencies, seconds
            float early = 1.0f;         // early reflection gain
            float predelayMs = 10.0f;
            float width = 1.0f;         // 0 mono .. 1 full
            float lows = 1.0f;          // decay multiplier below ~250 Hz
            float highs = 1.0f;         // decay multiplier above ~4 kHz
            float diffuse = 0.75f;      // 0..1
            float drift = 0.4f;         // modulation depth 0..1
            float brightness = 9000.0f; // tail input lowpass, Hz
            bool freeze = false;
        };

        void prepare (double sampleRate);
        void reset();

        // Settings apply smoothly from this call on. Lengths glide, so
        // size and space changes do not click.
        void setSettings (const Settings&);

        void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples);

    private:
        static constexpr int numLines = 16;
        static constexpr int numTaps = 24;
        static constexpr int numDiffusers = 4;
        static constexpr int controlInterval = 32;

        struct DelayBuffer
        {
            std::vector<float> data;
            int mask = 0, write = 0;

            void setSize (int minimumLength);
            void clear() { std::fill (data.begin(), data.end(), 0.0f); write = 0; }
            void push (float x) { data[(size_t) write] = x; write = (write + 1) & mask; }

            // Reads `delay` samples behind the last pushed sample.
            float readLinear (float delay) const;
            float readCubic (float delay) const;
        };

        struct Allpass
        {
            DelayBuffer buffer;
            float length = 100.0f, target = 100.0f;
            float process (float x, float g);
        };

        struct OnePole
        {
            float a = 0.0f, z = 0.0f;
            void setLowpass (float hz, double rate) { a = std::exp (-juce::MathConstants<float>::twoPi * hz / (float) rate); }
            float lowpass (float x) { z = x + a * (z - x); return z; }
        };

        // First order shelves around the decay band edges: lowpass and
        // highpass parts with exact unity at DC / Nyquist.
        struct Shelf
        {
            float zl = 0.0f, zh = 0.0f, xl = 0.0f, xh = 0.0f;
            float process (float x, float lowGain, float highGain, float aLow, float aHigh);
        };

        void updateControl();
        void computeTaps();
        float shelfMagnitudeAtMid (float lowGain, float highGain) const;

        double sr = 48000.0;
        Settings target;
        bool tapsDirty = true;

        DelayBuffer inputL, inputR, mid;
        std::array<float, numTaps> tapDelayL {}, tapDelayR {}, tapGainL {}, tapGainR {};
        std::array<float, numTaps> tapTargetL {}, tapTargetR {}, tapGainTargetL {}, tapGainTargetR {};
        OnePole airL, airR, brightL, brightR;
        float lowcutL = 0.0f, lowcutR = 0.0f, lowcutA = 0.0f;

        std::array<Allpass, numDiffusers> diffL, diffR;

        std::array<DelayBuffer, numLines> lines;
        std::array<float, numLines> lineLength {}, lineTarget {};
        std::array<float, numLines> gainMid {}, ratioLow {}, ratioHigh {};
        std::array<Shelf, numLines> shelves;
        std::array<float, numLines> modCos {}, modSin {}, modRotC {}, modRotS {}, modDepth {};
        float shelfLowA = 0.0f, shelfHighA = 0.0f;

        float predelay = 0.0f, earlyGain = 0.0f, injectGain = 1.0f, width = 1.0f, diffusion = 0.5f;
        float glide = 0.0f;   // per control step
        int controlCounter = 0;
    };

    //==========================================================================
    // Listens to the dry signal and decides how the Gargoyle behaves: stone
    // (still) while you play, awake in the pauses, charged by how long and
    // hard you played, and learning how long your pauses are.
    class Guardian
    {
    public:
        struct Settings
        {
            float stone = 0.5f;     // 0..1, how far the wet ducks while playing
            float charge = 0.4f;    // 0..1, how much stored playing blooms out
            float wakeMs = 300.0f;  // how fast it wakes after you stop
            float living = 0.5f;    // 0..1, how much your dynamics colour the space
            float decay = 3.0f;     // current RT60, drains the charge
        };

        void prepare (double sampleRate);
        void reset();

        // Call once per block of dry signal; returns the wet gain at the
        // block's end (ramp to it across the block).
        void process (const float* inL, const float* inR, int numSamples, const Settings&);

        float wetGain() const        { return gain; }
        float decayScale() const     { return rtScale; }
        float brightness() const     { return brightHz; }
        float activity() const       { return act; }
        float storedCharge() const   { return stored; }
        float learnedPause() const   { return pause; }

    private:
        double sr = 48000.0;
        float env = -120.0f, slowEnv = -120.0f;
        float act = 0.0f, stored = 0.0f;
        float gain = 1.0f, rtScale = 1.0f, brightHz = 9000.0f;
        bool playing = false;
        double silentFor = 0.0;
        float pause = 2.5f;
        float heldLevel = 0.5f;   // how loud the last phrase was, 0..1
    };
}
