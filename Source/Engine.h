#pragma once

#include <JuceHeader.h>
#include <array>
#include <cstdint>

// The Mimic's DSP building blocks. Nothing here allocates or locks after
// prepare(); everything runs per sample on the audio thread.
namespace mimic
{
    //==========================================================================
    // Deterministic randomness: the same seed always gives the same fill, so an
    // offline bounce sounds exactly like playback.
    inline uint64_t hash64 (uint64_t x)
    {
        x += 0x9e3779b97f4a7c15ull;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        return x ^ (x >> 31);
    }

    inline uint64_t hash64 (uint64_t a, uint64_t b) { return hash64 (a ^ hash64 (b + 0x632be59bd9b4e019ull)); }

    struct Rng
    {
        uint64_t state;

        explicit Rng (uint64_t seed = 1) : state (hash64 (seed) | 1ull) {}

        uint32_t next()
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return (uint32_t) (state >> 32);
        }

        float uni()                     { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
        float range (float a, float b)  { return a + (b - a) * uni(); }
        bool chance (float p)           { return uni() < p; }
        int below (int n)               { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
    };

    //==========================================================================
    // A fill (or a bar of Wild mode) is a script of segments laid out in
    // quarter notes. The script is pure data, generated from a seed.
    enum class Kind : uint8_t { pass, stutter, roll, reverse, scatter, tape, gate, grains };
    inline constexpr int numContent = 7;   // every kind after pass

    enum MaterialClass { percussive, melodic, harmonic };

    struct Segment
    {
        double start = 0.0, length = 0.0;   // ppq from the start of the span
        Kind kind = Kind::pass;
        double repeat = 0.25;               // ppq: repeat / slot / gate step / grain window
        double step = 0.25;                 // the grid step the script was built on
        double reach = 4.0;                 // scatter: how far back slices may come from (ppq)
        float rateStart = 1.0f, rateEnd = 1.0f;
        float gainStart = 1.0f, gainEnd = 1.0f;
        float decay = 1.0f;                 // gain multiplier per repeat
        float shrink = 1.0f;                // roll: repeat length factor per repeat
        float cutStart = 1.0f, cutEnd = 1.0f;   // filter sweep, 1 = open
        bool highPass = false;
        float density = 1.0f;               // gate and grains
        float reverseChance = 0.0f, octaveChance = 0.0f;
        uint32_t seed = 0;
    };

    struct Script
    {
        static constexpr int maxSegments = 128;
        std::array<Segment, maxSegments> segments;
        int count = 0;

        void clear() { count = 0; }
        void add (const Segment& s) { if (count < maxSegments) segments[(size_t) count++] = s; }
        int find (double position) const;          // segment index at a span position, -1 if none
        uint64_t signature() const;                // what the fill sounds like, roughly
    };

    struct GenSettings
    {
        double span = 1.0;          // ppq
        double beat = 1.0;          // ppq per beat
        double bar = 4.0;           // ppq per bar
        double step = 0.25;         // grid step
        float intensity = 0.5f;     // 0..1
        int material = percussive;
        unsigned enabled = 0x7f;    // one bit per content kind (stutter = bit 0)
        bool fill = true;           // a fill builds to a climax; Wild varies a bar
    };

    void generate (Script&, const GenSettings&, uint64_t seed);

    //==========================================================================
    // The rolling buffer of what the main track just played.
    class History
    {
    public:
        void prepare (double sampleRate, double seconds);
        void clear();

        void write (int64_t now, float l, float r)
        {
            const auto i = (size_t) (now & mask);
            data[0][i] = l;
            data[1][i] = r;
        }

        // Reads at an absolute sample position (never later than now).
        float read (int ch, double pos, int64_t now) const;
        int64_t size() const { return (int64_t) data[0].size(); }

    private:
        std::vector<float> data[2];
        int64_t mask = 0;
    };

    //==========================================================================
    // Transients: a fast and a slow follower on a high-passed signal.
    class OnsetDetector
    {
    public:
        void prepare (double sampleRate);
        void reset();
        bool process (float x);          // true on the sample an onset is found
        float level() const { return fast; }

    private:
        float hpX = 0.0f, hpY = 0.0f, hpCoeff = 0.0f;
        float fast = 0.0f, slow = 0.0f;
        float fastAtt = 0.0f, fastRel = 0.0f, slowAtt = 0.0f, slowRel = 0.0f;
        int holdoff = 0, holdoffSamples = 0;
        bool armed = true;
    };

    // Where the recent onsets of the main track are, so slices start just
    // before a transient instead of cutting through it.
    class OnsetList
    {
    public:
        void reset() { count = 0; }
        void add (int64_t position);

        // Latest onset in [target - window, target], minus a short pre-roll;
        // target itself when there is none.
        double snap (double target, double window, double preroll, bool* found = nullptr) const;

    private:
        std::array<int64_t, 256> times {};
        int count = 0, write = 0;
    };

    //==========================================================================
    // Is the track drums, a single line or chords / pads? Coarse, steady and
    // cheap: transient density, how much the level sustains, and how clearly
    // periodic the waveform is (normalised autocorrelation).
    class Classifier
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void push (float mono, bool onset);

        int material() const { return current; }
        float clarity = 0.0f, sustain = 0.0f, onsetRate = 0.0f;

    private:
        void analyse();

        int decimation = 4, decimCount = 0;
        float decimAcc = 0.0f, lp1 = 0.0f, lp2 = 0.0f, lpCoeff = 0.0f;
        std::vector<float> frame;
        int frameFill = 0;
        float env = 0.0f, peak = 0.0f, envAtt = 0.0f, envRel = 0.0f, peakRel = 0.0f;
        double envSum = 0.0, peakSum = 0.0;
        int onsets = 0;
        int64_t frameSamples = 0;
        double sampleRate = 44100.0;
        int current = harmonic, candidate = harmonic;
        float candidateSeconds = 0.0f;
        bool primed = false;
    };

    //==========================================================================
    // Topology-preserving state-variable filter (two channels).
    struct Svf
    {
        float ic1[2] {}, ic2[2] {};
        float k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }

        void set (float hz, float q, double sampleRate)
        {
            const float g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (10.0f, (float) sampleRate * 0.45f, hz) / (float) sampleRate);
            k = 1.0f / q;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        // 0 = low-pass, 1 = high-pass
        float process (int ch, float x, int type)
        {
            const float v3 = x - ic2[ch];
            const float v1 = a1 * ic1[ch] + a2 * v3;
            const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.0f * v1 - ic1[ch];
            ic2[ch] = 2.0f * v2 - ic2[ch];
            return type == 0 ? v2 : x - k * v1 - v2;
        }
    };

    //==========================================================================
    // Plays a script segment out of the History: slices, rolls, reverses,
    // scatters, tape stops, gates and grain clouds. Every switch between
    // voices crossfades, so nothing clicks.
    class Player
    {
    public:
        void prepare (double sampleRate, const History*, const OnsetList*);
        void reset();

        // Starts a segment now; it lasts lengthSamples. Crossfades from
        // whatever played before.
        void begin (const Segment&, int64_t now, double samplesPerPpq, int64_t lengthSamples);
        void beginPass (int64_t now);

        // Slicer tooth: the prey struck. Restart the current slice, or, when
        // nothing is happening, replay the last hit of the main track.
        void strike (int64_t now, double samplesPerPpq, double stepPpq, uint32_t seed);

        Kind kind() const { return current.kind; }
        bool active() const { return current.kind != Kind::pass; }

        // passWeight: how much of the output is the untouched live input.
        void render (int64_t now, float inL, float inR, float& outL, float& outR, float& passWeight);

    private:
        struct Grain
        {
            bool on = false;
            double pos = 0.0;
            float rate = 1.0f;
            int length = 1, age = 0;
            float gainL = 0.7f, gainR = 0.7f;
        };

        struct Voice
        {
            bool alive = false;
            Kind kind = Kind::pass;
            Segment seg;
            int64_t startNow = 0, lengthS = 1, endK = -1;
            double spq = 1.0;
            double capture = 0.0, repeatS = 1.0, minRepeatS = 1.0, phase = 0.0;
            int64_t repStart = 0;
            int repeatIndex = 0;
            float repGain = 1.0f, slotRate = 1.0f;
            bool reversed = false;
            float fade = 1.0f, fadeStep = 1.0f;     // fade in (current) or out (tail)
            Svf filter;
            bool filterOn = false;
            int filterCountdown = 0;
            std::array<Grain, 16> grains {};
            int spawnCountdown = 0;
            uint32_t grainCount = 0;
        };

        void startRepeat (Voice&, int64_t now);
        void pickScatterSlot (Voice&, int64_t now);
        void pushTail();
        float fadeSamples (bool onsetAligned) const;
        float rateAt (const Voice&, float p) const;
        void renderVoice (Voice&, int64_t now, float inL, float inR, float& l, float& r);
        void spawnGrain (Voice&, int64_t now, float p);

        double sr = 44100.0;
        const History* history = nullptr;
        const OnsetList* onsets = nullptr;
        Voice current;
        std::array<Voice, 3> tails;
    };

    //==========================================================================
    // Two-tap delay-line pitch shifter for the Pitcher tooth.
    class PitchShifter
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void process (float& l, float& r, float ratio);

    private:
        std::vector<float> buffer[2];
        int write = 0, mask = 0;
        float phase = 0.0f, windowS = 1.0f;
    };
}
