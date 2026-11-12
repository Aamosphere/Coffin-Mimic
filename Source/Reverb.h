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
        int maxOrder = 2;              // image sources up to this many bounces
        float scatter = 0.0f;          // furniture, people: reflections irregular and duller
    };

    const Space& space (int index);
    int numSpaces();

    // Size knob (0..1) to the factor that scales the room.
    inline float sizeFactor (float size) { return 0.4f * std::pow (4.0f, size); }

    //==========================================================================
    // Early reflections plus a 32-line feedback delay network. Produces the
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

            // A captured space: its tail shape instead of the chosen
            // space's, and no image-source reflections (its own recorded
            // ones come in through earlyL / earlyR).
            bool captured = false;
            Space capturedSpace { 10.0f, 7.0f, 4.0f, 0.8f, false, 6.0f, 28.0f, 9000.0f, 1.0f, 0.6f };
        };

        void prepare (double sampleRate);
        void reset();

        // Settings apply smoothly from this call on. Lengths glide, so
        // size and space changes do not click.
        void setSettings (const Settings&);

        // tailL / tailR (optional): fed to the tail only, past the early
        // reflections (the Halo's octave goes in there).
        void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                      const float* tailL = nullptr, const float* tailR = nullptr,
                      const float* earlyL = nullptr, const float* earlyR = nullptr);

    private:
        const Space& currentSpace() const { return target.captured ? target.capturedSpace : space (target.space); }

        static constexpr int numLines = 32;
        static constexpr int numTaps = 64;
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
        OnePole airL, airR, brightL, brightR, lateAirL, lateAirR;
        std::array<bool, numTaps> tapLate {};   // two or more bounces: duller
        float lowcutL = 0.0f, lowcutR = 0.0f, lowcutA = 0.0f;

        std::array<Allpass, numDiffusers> diffL, diffR;

        std::array<DelayBuffer, numLines> lines;
        std::array<float, numLines> lineLength {}, lineTarget {};
        // An allpass inside each line's loop: every pass round the network
        // smears each echo into several, so the tail thickens quickly into
        // a smooth wash instead of ringing (the "metallic" comb of a bare
        // delay network).
        std::array<Allpass, numLines> loopDiffusers;
        // The diffused input, so each line can take it in at its own moment
        // (0 to ~23 ms apart): the lines start out different from each other.
        DelayBuffer feedL, feedR;
        std::array<int, numLines> feedTap {};
        // How each line is fed and heard: signs with no pattern a Hadamard
        // row could match (a row would hand one line's echo straight back
        // out, repeating every time round its loop).
        std::array<float, numLines> feedSign {}, outSignL {}, outSignR {};
        float loopDiffusion = 0.5f;
        float lineSeconds (int i) const;   // a line's whole loop: its delay and its allpass
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

        // The dry signal's peak in each of the three bands for the coming
        // block (call before process): each band of the wet ducks under its
        // own band of the dry, so a bass line does not push the whole tail
        // down, only its lows.
        void setBandPeaks (float low, float mid, float high) { bandPeak[0] = low; bandPeak[1] = mid; bandPeak[2] = high; }

        // Stone's EQ: how far each band of the wet ducks (0..1 of Stone), and
        // whether every band listens to the whole dry signal (linked) rather
        // than to its own band of it.
        void setBandDepths (float low, float mid, float high, bool linkedBands)
        {
            depth[0] = low; depth[1] = mid; depth[2] = high;
            linked = linkedBands;
        }
        float bandGain (int band) const { return bandGains[(size_t) band]; }

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
        float bandPeak[3] {};
        float depth[3] { 1.0f, 1.0f, 1.0f };
        bool linked = false;
        std::array<float, 3> bandEnv { -120.0f, -120.0f, -120.0f }, bandAct {}, bandGains { 1.0f, 1.0f, 1.0f };
    };

    //==========================================================================
    // Three bands with Linkwitz-Riley crossovers (24 dB/oct): lows, mids,
    // highs. The low band gets the upper crossover's allpass too, so the
    // three add back up flat.
    class BandSplit
    {
    public:
        void prepare (double sampleRate, float lowHz, float highHz);
        void reset();
        void process (int ch, float x, float& low, float& mid, float& high);

    private:
        struct Svf
        {
            float g = 0.0f, k = 1.4142f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
            float ic1[2] {}, ic2[2] {};
            void set (float hz, double sr);
            void tick (int ch, float x, float& lp, float& bp, float& hp);
        };

        // Two cascaded Butterworths per side make each Linkwitz-Riley.
        Svf lowA, lowB, highA, highB, restA, restB, midA, midB, allpass;
    };

    //==========================================================================
    // A soft octave up: four overlapping windowed taps on a delay line (long,
    // smooth grains), for the Halo. No feedback: it is reverberated once,
    // never fed back up another octave, so it never turns into a shimmer.
    class SoftOctave
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void process (float inL, float inR, float& outL, float& outR);

    private:
        std::vector<float> buffer[2];
        int write = 0, mask = 0;
        float phase = 0.0f, windowSamples = 1.0f;
    };
}
