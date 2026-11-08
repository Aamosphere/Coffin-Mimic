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
    // slice: a grid piece of the track taken from another place (the beat
    // masher). rest: a Break (silence).
    //
    // What the content switches turn into depends on the material. On a
    // melody a stutter may become shards (the loop layered with itself an
    // octave up and down) and a roll a bend (a loop gliding to a new
    // pitch); on chords and pads stutters and grains may become fog (a
    // drifting, detuned grain haze). echo: the track plays on and is sent
    // into the granular delay.
    enum class Kind : uint8_t { pass, stutter, roll, reverse, scatter, tape, gate, grains, rest, slice,
                                shards, bend, fog, echo };

    // The switchable kinds, in the order of the content switches.
    inline constexpr Kind contentKinds[] { Kind::stutter, Kind::roll, Kind::reverse, Kind::scatter,
                                           Kind::tape, Kind::gate, Kind::grains, Kind::slice, Kind::echo };
    inline constexpr int numContent = 9;

    // Does a kind come from this content switch? (Shards are stutters, a
    // bend is a roll, fog comes from stutters or grains.)
    inline bool belongsTo (Kind k, Kind contentSwitch)
    {
        if (k == Kind::shards) return contentSwitch == Kind::stutter;
        if (k == Kind::bend)   return contentSwitch == Kind::roll;
        if (k == Kind::fog)    return contentSwitch == Kind::stutter || contentSwitch == Kind::grains;
        return k == contentSwitch;
    }

    enum MaterialClass { percussive, melodic, harmonic, vocal };
    inline constexpr int numMaterials = 4;

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
        float swing = 0.0f;                 // 0..0.5: odd repeats start late
        double back = 0.0;                  // slice: how far behind the segment start its source starts (ppq)
        double minRepeat = 0.125;           // roll: shortest repeat (ppq)
        double grainNote = 0.25;            // grains: spawn grid and length unit (ppq)
        float grainOverlap = 1.0f;          // grain length in grain notes
        float taper = 1.0f;                 // grain window: 0.1 = punchy, 1 = Hann
        bool punch = false;                 // short repeats are articulated like hits
        std::array<int8_t, 4> arp {};       // semitones per repeat (melodic stutters)
        int arpLength = 0;
        uint32_t seed = 0;

        float arpRatio (int repeatIndex) const
        {
            return arpLength > 0 ? std::exp2 ((float) arp[(size_t) (repeatIndex % arpLength)] / 12.0f) : 1.0f;
        }

        float maxArpRatio() const
        {
            float m = 1.0f;
            for (int i = 0; i < arpLength; ++i)
                m = juce::jmax (m, std::exp2 ((float) arp[(size_t) i] / 12.0f));
            return m;
        }
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
        float swing = 0.0f;         // 0..0.5 of a step
        float breaks = 0.0f;        // 0..1, how often a rest follows an event
        double offsetInBar = 0.0;   // where the span starts in its bar (ppq)
    };

    void generate (Script&, const GenSettings&, uint64_t seed);

    // Possess tooth: one event of an enabled kind, starting at span position
    // 0 and lasting length ppq, for the prey to fire at once.
    Segment makeEvent (const GenSettings&, double length, uint64_t seed);

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

        // An onset within tolerance of position, not later than limit.
        bool near (double position, double tolerance, double limit, double& onset) const;

    private:
        std::array<int64_t, 256> times {};
        int count = 0, write = 0;
    };

    //==========================================================================
    // Is the track drums, a single line, chords / pads or a voice? Coarse,
    // steady and cheap: transient density, how much the level sustains, how
    // clearly periodic the waveform is (normalised autocorrelation) and how
    // the pitch moves. A voice glides and wobbles between the notes of the
    // scale; an instrument sits on them.
    class Classifier
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void push (float mono, bool onset);

        int material() const { return current; }
        float clarity = 0.0f, sustain = 0.0f, onsetRate = 0.0f;
        float pitchDrift = 0.0f;    // cents off the 12-TET grid, on average
        float pitchWobble = 0.0f;   // cents of movement within notes

        // The spectrum, which is what tells a strummed guitar from drums:
        // how much of the energy sits in stable peaks (tonal), how noise-like
        // it is (flatness) and how much of it is below 150 Hz (a bass).
        float tonal = 0.0f, flatness = 0.0f, lowShare = 0.0f;

    private:
        void analyse();
        void analyseSpectrum();
        void decide (float seconds);

        std::unique_ptr<juce::dsp::FFT> fft;
        int fftSize = 2048, hop = 1024, ringWrite = 0, hopCount = 0, spectralFrames = 0;
        std::vector<float> ring, window, fftData, magnitude, previousMagnitude;
        // Pitch of a stretch of the frame: clarity 0..1 and the frequency.
        float nsdfPitch (int from, int length, double& hz) const;

        int decimation = 4, decimCount = 0;
        float decimAcc = 0.0f, lp1 = 0.0f, lp2 = 0.0f, lpCoeff = 0.0f;
        std::vector<float> frame;
        int frameFill = 0;
        float env = 0.0f, peak = 0.0f, envAtt = 0.0f, envRel = 0.0f, peakRel = 0.0f;
        double envSum = 0.0, peakSum = 0.0;
        int onsets = 0;
        int64_t frameSamples = 0;
        double sampleRate = 44100.0;
        int current = percussive, candidate = percussive;   // until it has heard enough: the plainest versions
        float candidateSeconds = 0.0f, heardSeconds = 0.0f;
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

        // 0 = low-pass, 1 = high-pass, 2 = band-pass (unity at the peak)
        float process (int ch, float x, int type)
        {
            const float v3 = x - ic2[ch];
            const float v1 = a1 * ic1[ch] + a2 * v3;
            const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.0f * v1 - ic1[ch];
            ic2[ch] = 2.0f * v2 - ic2[ch];
            return type == 0 ? v2 : (type == 1 ? x - k * v1 - v2 : k * v1);
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

        // Dynamics, 0..1: how much the level of events and their repeats
        // varies, with accents on the beat.
        void setDynamics (float d) { dynamics = d; }

        // Slicer tooth: the prey struck. Restart the current slice, or, when
        // nothing is happening, replay the last hit of the main track.
        void strike (int64_t now, double samplesPerPpq, double stepPpq, uint32_t seed);

        // Audio before this sample is from another place in the song (the
        // playhead jumped): nothing is taken from it.
        void setValidFrom (int64_t sample) { validFrom = sample; }

        Kind kind() const { return current.kind; }
        bool active() const { return current.kind != Kind::pass; }

        struct Weights { float pass = 0.0f, rest = 0.0f; };

        // Weights: how much of the output is the untouched live input, and
        // how much is a Break (silence).
        void render (int64_t now, float inL, float inR, float& outL, float& outR, Weights&);

    private:
        struct Grain
        {
            bool on = false;
            double pos = 0.0;
            float rate = 1.0f;
            int length = 1, age = 0;
            float gainL = 0.7f, gainR = 0.7f;
            float accent = 1.0f;
        };

        struct Voice
        {
            bool alive = false;
            Kind kind = Kind::pass;
            Segment seg;
            int64_t startNow = 0, lengthS = 1, endK = -1;
            double spq = 1.0;
            double capture = 0.0, repeatS = 1.0, baseRepeatS = 1.0, minRepeatS = 1.0, phase = 0.0;
            int64_t repStart = 0;
            int repeatIndex = 0;
            float repGain = 1.0f, slotRate = 1.0f;
            float accent = 1.0f, repAccent = 1.0f;   // Dynamics
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
        double swungRepeat (const Voice&, int index) const;
        void pickScatterSlot (Voice&, int64_t now);
        void pushTail (double fadeSeconds = 0.0015);
        float accentFor (uint32_t seed, int index, bool onBeat) const;
        float fadeSamples (bool onsetAligned) const;
        float rateAt (const Voice&, float p) const;
        void renderVoice (Voice&, int64_t now, float inL, float inR, float& l, float& r);
        void spawnGrain (Voice&, int64_t now, float p);

        double sr = 44100.0;
        float dynamics = 0.0f;
        int64_t validFrom = 0;
        const History* history = nullptr;
        const OnsetList* onsets = nullptr;
        Voice current;
        std::array<Voice, 6> tails;
    };

    //==========================================================================
    // Echo: a tempo-synced granular delay. What is sent in comes back in
    // overlapping grains, smeared a little in time; some grains an octave
    // up (shimmer) or backwards, and every repeat is grained again, so the
    // echoes dissolve as they fade.
    class GrainDelay
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void setup (double delaySamples, float feedback, float shimmer, float reverse);
        void process (float inL, float inR, float& outL, float& outR);

    private:
        struct Grain
        {
            bool on = false;
            double pos = 0.0, rate = 1.0;
            int age = 0, length = 1;
            float gainL = 1.0f, gainR = 1.0f;
        };

        void spawn();

        std::vector<float> buffer[2];
        int64_t write = 0;
        int64_t mask = 0;
        double sr = 44100.0, delay = 22050.0;
        float feedback = 0.5f, shimmer = 0.0f, reverse = 0.0f;
        int grainLength = 4096, countdown = 0;
        uint32_t grainCount = 0;
        bool left = true;
        std::array<Grain, 6> grains {};
        Svf tone;
        float fbL = 0.0f, fbR = 0.0f;
    };

    //==========================================================================
    // Voice tooth: a channel vocoder. The prey (modulator) lends its
    // spectrum to the track (carrier), band by band. Carve does the
    // opposite: each band of the track ducks where the prey is loud.
    class Vocoder
    {
    public:
        static constexpr int numBands = 20;

        void prepare (double sampleRate);
        void reset();
        // l, r: the track in, the vocoded track out. prey: mono modulator.
        void process (float& l, float& r, float prey, bool carve);

    private:
        Svf mod[numBands], car[numBands];
        float modEnv[numBands] {}, carEnv[numBands] {}, duckEnv[numBands] {};
        float modAtt = 0.0f, modRel = 0.0f, carAtt = 0.0f, carRel = 0.0f, peakRel = 0.0f, duckAtt = 0.0f, duckRel = 0.0f;
        float totalPeak = 0.0f;
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
