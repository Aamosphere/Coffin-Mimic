#pragma once

#include <JuceHeader.h>
#include "Engine.h"
#include "Reverb.h"

// The Mimic answers: it listens to the track, waits for the player to leave
// a gap, and fills it with a copy of the last phrase, bent out of shape.
// While the player plays it keeps quiet (and steps back when they start
// again mid-answer); its echoes and its cave ring on underneath.
//
// Nothing here allocates or locks after prepare().
namespace mimic
{
    // What an answer does with the phrase.
    //   parrot     says it back, on tape
    //   ghost      an octave away (or two), layered
    //   cloud      dissolved into a drifting swarm of grains
    //   backwards  reversed, swelling into the cave
    //   dub        the last note thrown into a spiralling tape echo
    //   mock       the last syllable stuttered back, laughing
    //   hum        the last note frozen into a drone
    //   radio      from a broken radio in the next room
    enum class Trick : uint8_t { parrot, ghost, cloud, backwards, dub, mock, hum, radio };
    inline constexpr int numTricks = 8;

    //==========================================================================
    // Tape-like saturation: a biased tanh (even harmonics), with first-order
    // antiderivative anti-aliasing so even hard drive stays clean.
    class Saturator
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void set (float amount);                 // 0..1
        float process (int ch, float x);
        bool active() const { return mix > 0.0f; }

    private:
        double shaped (int ch, double x);

        float drive = 1.0f, bias = 0.0f, makeup = 1.0f, mix = 0.0f, biasOut = 0.0f;
        double x1[2] {}, f1[2] {};
        float dcX[2] {}, dcY[2] {}, dcCoeff = 0.999f;
        float toneZ[2] {}, toneA = 0.0f;
        double sr = 48000.0;
    };

    //==========================================================================
    // A stereo tape echo: the head wobbles (wow and flutter), every repeat
    // gets darker, thinner and a little more driven, and the repeats can
    // bounce from side to side. Changing the time glides, like a varispeed
    // tape: the echoes bend in pitch.
    class TapeDelay
    {
    public:
        struct Settings
        {
            float delaySamples = 12000.0f;
            float feedback = 0.35f;     // 0..0.97
            float toneHz = 4500.0f;     // lowpass in the loop
            float wow = 0.2f;           // 0..1
            float pingPong = 0.5f;      // 0..1
            float grit = 0.2f;          // 0..1, drive in the loop
        };

        void prepare (double sampleRate, double maxSeconds);
        void reset();
        void set (const Settings& s) { target = s; }
        void process (float inL, float inR, float& outL, float& outR);

    private:
        float read (int ch, float delay) const;

        std::vector<float> buffer[2];
        int write = 0, mask = 0;
        double sr = 48000.0;
        Settings target;
        float delay = 12000.0f, feedback = 0.0f, glide = 0.0f, smooth = 0.0f;
        float lp[2] {}, hp[2] {}, hpX[2] {}, lpA = 0.0f, hpA = 0.0f, lastToneHz = -1.0f;
        float wowPhase = 0.0f, flutterPhase = 0.0f, wowInc = 0.0f, flutterInc = 0.0f;
        float drift = 0.0f, driftTarget = 0.0f;
        int driftCountdown = 0;
        Rng rng { 0x7a9e };
    };

    //==========================================================================
    // One phrase, copied out of the track when it ended. A second, lowpassed
    // copy is read when playing faster than the original, so octaves up do
    // not alias.
    struct Capture
    {
        void prepare (double sampleRate, double seconds);

        double sampleRate = 48000.0;
        std::vector<float> data[2], smooth[2];
        int length = 0, capacity = 0;
        std::array<int, 64> onsets {};
        int numOnsets = 0;
        float rms = 0.0f, peak = 0.0f;
        int lastOnset = 0;      // where the last note starts
        int sustainAt = 0;      // a steady place in the last note

        // Cubic read; silence outside. reversed: position 0 is the end.
        float read (int ch, double pos, bool reversed, bool fast) const;
        float mono (int index, bool reversed) const;

        // The onset nearest to end - want that leaves at least minimum.
        int startFor (int end, int want, int minimum) const;
        float rmsOver (int from, int to) const;
    };

    //==========================================================================
    // Plays a stretch of a capture like a tape: at a rate (an octave up is
    // 2), forwards or backwards, with a little wow if asked.
    class TapeRead
    {
    public:
        void start (const Capture*, double from, double to, double rate, bool reversed);
        bool render (float& l, float& r);   // false once past the end
        bool active() const { return capture != nullptr && on; }
        void setWow (float depth, float hz, double sampleRate);
        double progress() const;

    private:
        const Capture* capture = nullptr;
        bool on = false, reversed = false;
        double pos = 0.0, end = 0.0, begin = 0.0, rate = 1.0;
        float wowDepth = 0.0f, wowPhase = 0.0f, wowInc = 0.0f;
        double fadeLength = 144.0;   // source samples
    };

    //==========================================================================
    // Pitch and time apart: overlapping grains, each one placed where it
    // fits the last one best (WSOLA), so a voice or a single line shifts by
    // an octave without the usual warble. Speed is in source samples per
    // output sample (0.5 = half speed), pitch is a ratio.
    class Stretch
    {
    public:
        void prepare (double sampleRate);
        void start (const Capture*, double from, double to, double speed, float pitch, bool reversed, bool align);
        bool render (float& l, float& r);
        bool active() const { return capture != nullptr && on; }

    private:
        struct Grain { bool on = false; double pos = 0.0; int age = 0; };

        void spawn();
        double bestStart (double nominal, double natural) const;

        const Capture* capture = nullptr;
        bool on = false, reversed = false, align = true, fast = false;
        double from = 0.0, to = 0.0, speed = 1.0, nominal = 0.0, lastStart = -1.0;
        float pitch = 1.0f;
        int length = 2048, hop = 1024, countdown = 0, searchReach = 512;
        std::array<Grain, 2> grains {};
        int nextGrain = 0;
        std::vector<float> window;
        bool finishing = false;
    };

    //==========================================================================
    // A swarm of grains over a stretch of a capture: the scan moves through
    // it slowly (or stands still: a freeze), grains land around it, some an
    // octave or a fifth away, some backwards, spread across the stereo field.
    class Cloud
    {
    public:
        struct Settings
        {
            double from = 0.0, to = 0.0;
            double scan = 0.3;            // source samples per output sample (0 = frozen)
            float grainMs = 120.0f;
            float density = 30.0f;        // grains per second
            float jitterMs = 40.0f;       // how far around the scan they land
            float reverse = 0.0f;         // chance of a backwards grain
            float spread = 1.0f;          // stereo
            std::array<float, 4> pitches { 1.0f, 1.0f, 1.0f, 1.0f };
            float detuneCents = 6.0f;
        };

        void prepare (double sampleRate);
        void start (const Capture*, const Settings&, uint64_t seed);
        void stop() { spawning = false; }
        void render (float& l, float& r);
        bool active() const;

    private:
        struct Grain
        {
            bool on = false, reversed = false;
            double pos = 0.0, rate = 1.0;
            int age = 0, length = 1;
            float gainL = 0.7f, gainR = 0.7f;
        };

        void spawn();

        const Capture* capture = nullptr;
        Settings s;
        double sr = 48000.0, scanPos = 0.0;
        bool spawning = false;
        float countdown = 0.0f, norm = 1.0f;
        std::array<Grain, 32> grains {};
        std::array<float, 1025> hann {};
        Rng rng { 1 };
    };

    //==========================================================================
    // The cave the answers live in: the Gargoyle's reverb, with an optional
    // shimmer (its own tail fed back an octave up).
    class Cave
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset();
        void set (float space, float decayScale, float shimmer);
        void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    private:
        gargoyle::Reverb reverb;
        PitchShifter octave;
        Svf shimmerTone;
        std::vector<float> feedL, feedR, prevL, prevR;
        int prevCount = 0;
        float shimmer = 0.0f, shimmerTarget = 0.0f;
        float lastSpace = -1.0f, lastDecay = -1.0f;
        double sr = 48000.0;
    };

    //==========================================================================
    struct AnswerSettings
    {
        float level = 0.7f;      // 0..1, how loud the answers are (0 = never)
        float hunger = 0.5f;     // 0..1, how soon and how often it answers
        float mockery = 0.35f;   // 0..1, how far an answer is bent
        float space = 0.5f;      // 0..1, the cave and the echoes
        float heat = 0.25f;      // 0..1, saturation
        unsigned enabled = 0xff; // one bit per Trick
        int material = percussive;
        uint32_t seed = 0;

        double samplesPerBeat = 24000.0;   // a quarter note
        bool playing = false;
        double ppq = 0.0;                  // at the start of the block
        double ppqPerSample = 0.0;
    };

    class Answerer
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset();

        // Listens to the dry track (in) and adds the answers to out.
        void process (const float* inL, const float* inR, float* outL, float* outR, int n, const AnswerSettings&);

        // For the editor: what the Mimic is doing.
        enum State { idle, hearing, waiting, answering, retreating };
        State state() const { return shownState; }
        float waitProgress() const { return shownWait; }
        float answerProgress() const { return shownProgress; }
        int trick() const { return shownTrick; }
        int answers() const { return answerCount; }
        bool busy() const;   // anything still sounding

    private:
        struct Slot
        {
            bool on = false;
            Trick trick = Trick::parrot;
            Capture capture;
            int64_t age = 0, length = 1;
            float attack = 0.01f, releaseSamples = 1000.0f;
            float gain = 1.0f, env = 0.0f, fade = 1.0f, fadeStep = 0.0f;
            bool retreating = false, swell = false;
            float dry = 1.0f, toDelay = 0.0f, toCave = 0.0f;
            TapeRead tape, tape2;
            Stretch stretch, stretch2;
            float stretchGain = 1.0f, stretch2Gain = 0.0f, tapeGain = 1.0f, tape2Gain = 0.0f;
            Cloud cloud;
            float cloudGain = 0.0f;
            Svf hp, lp, band;
            bool useHp = false, useLp = false, useBand = false;
            float lpFrom = 18000.0f, lpTo = 18000.0f;
            int filterCountdown = 0;
            // Radio
            bool radio = false;
            float crushStep = 0.0f, holdL = 0.0f, holdR = 0.0f, holdCount = 0.0f, holdLength = 1.0f;
            // Mock
            bool mock = false;
            int mockRepeats = 0, mockIndex = 0;
            double mockFrom = 0.0, mockTo = 0.0, mockStep = 1.0;
            int64_t mockNext = 0;
            float mockPan = 0.0f, mockGain = 1.0f, mockPitchStep = 0.0f, mockDecay = 0.85f;
            bool mockOctaves = false, mockRoll = false;
            float panL = 1.0f, panR = 1.0f;
            float tapePanL = 1.0f, tapePanR = 1.0f, tape2PanL = 1.0f, tape2PanR = 1.0f;
            Saturator sat;
            float spiralFrom = 0.0f, spiralTo = 0.0f;
        };

        void listen (float mono, double ppqNow, const AnswerSettings&);
        void phraseEnded (double ppqNow, const AnswerSettings&);
        void capture (Capture&, int64_t from, int64_t to);
        void begin (Slot&, Trick, const AnswerSettings&, uint64_t seed);
        Trick pickTrick (const AnswerSettings&, Rng&);
        void renderSlot (Slot&, float& l, float& r, const AnswerSettings&);
        void startMockRepeat (Slot&);

        double sr = 48000.0;
        int64_t now = 0;

        // Everything the track played lately.
        std::vector<float> ring[2];
        int64_t ringMask = 0;

        // Listening.
        OnsetDetector detector;
        float env = 0.0f, envAtt = 0.0f, envRel = 0.0f, phrasePeak = 0.0f, phraseSum = 0.0f;
        int64_t phraseStart = -1, lastOnsetAt = -1, phraseSamples = 0;
        std::array<int64_t, 64> phraseOnsets {};
        int numPhraseOnsets = 0;
        bool inPhrase = false;
        int64_t pendingAt = -1;    // an answer waits for the grid
        int64_t pendingFrom = 0, pendingTo = 0;
        Trick pendingTrick = Trick::parrot;
        uint64_t pendingSeed = 0;
        float lastPeak = 0.0f;      // of the phrase answered, for ducking
        float endEnv = 0.0f;        // the level when it ended
        bool asleep = true;         // echoes and cave stopped
        bool answeredLast = true;
        int64_t delayHold = 0;      // a dub echo keeps its feedback this long
        int phraseCount = 0, answerCount = 0;
        int lastTrick = -1;

        std::array<Slot, 3> slots;
        int active = 0;

        TapeDelay delay;
        Cave cave;
        float duck = 1.0f, tailDuck = 1.0f, duckAtt = 0.0f, duckRel = 0.0f;
        float levelSmooth = 0.0f;

        static constexpr int chunk = 256;
        std::array<float, chunk> caveInL {}, caveInR {}, caveOutL {}, caveOutR {}, dryL {}, dryR {}, tailGain {};
        int quietFor = 0;

        State shownState = idle;
        float shownWait = 0.0f, shownProgress = 0.0f;
        int shownTrick = -1;
    };
}
