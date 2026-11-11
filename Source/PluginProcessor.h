#pragma once

#include <JuceHeader.h>
#include "Parameters.h"
#include "Engine.h"
#include "Presets.h"
#include "Reverb.h"
#include "Answer.h"

class MimicProcessor : public juce::AudioProcessor
{
public:
    MimicProcessor();
    ~MimicProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override { return "Coffin Mimic"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    mimic::PresetManager presets { parameters };

    // Message thread: a new seed for the fills (REROLL).
    void reroll();

    // Live values for the editor.
    std::atomic<int> materialOut { mimic::percussive };
    std::atomic<float> clarityOut { 0.0f }, sustainOut { 0.0f }, onsetRateOut { 0.0f };
    std::atomic<float> preyOut { 0.0f };          // prey envelope, 0..1
    std::atomic<float> fillProgressOut { 0.0f };  // 0..1 over anticipation + fill
    std::atomic<bool> inFillOut { false };
    std::atomic<float> engageOut { 0.0f };        // how much of the output is the Mimic
    std::atomic<int> kindOut { 0 };               // mimic::Kind playing now
    std::atomic<int> barsToFillOut { -1 };        // bars until the next scheduled fill
    std::atomic<float> toothOut[6] { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };   // slicer, shaper, filter, pitcher, voice, possess
    std::atomic<bool> sidechainMissing { false };
    std::atomic<bool> transportPlaying { false };
    std::atomic<float> throwOut { 0.0f };          // a hit just went into the reverb
    std::atomic<float> pitchDriftOut { 0.0f };
    std::atomic<int> possessCount { 0 };           // events the prey has fired
    std::atomic<double> lastPossessPpq { 0.0 };    // song position of the last one

    // The answers: what the Mimic is doing (mimic::Answerer::State), which
    // trick it plays, how long it has waited, how far the answer is.
    std::atomic<int> answerStateOut { 0 }, answerTrickOut { -1 }, answerCountOut { 0 };
    std::atomic<float> answerWaitOut { 0.0f }, answerProgressOut { 0.0f };
    std::atomic<float> inputLevelOut { 0.0f };
    std::atomic<float> swingOut { 0.5f };           // the track's swing as heard, 0.5 = straight

    // The script playing now, for the pattern display: per segment the
    // start and length in 1/96 ppq and the kind, packed.
    static constexpr int maxShown = 64;
    std::array<std::atomic<uint32_t>, maxShown> shownSegments {};
    std::atomic<int> shownCount { 0 };
    std::atomic<float> shownSpan { 4.0f };
    std::atomic<float> shownPosition { -1.0f };    // ppq into the span, -1 when none plays
    std::atomic<int> shownBar { 0 };               // the bar now, 1-based
    std::atomic<int> shownPhraseBar { 0 };         // bars since the phrases started (Phrase Start, loops)

    // The editor's Advanced drawer, remembered while the plugin lives.
    bool advancedOpen = false;

private:
    float trackGain = 1.0f;   // the track under the answers (Answer past 50 %)
    juce::AudioBuffer<float> sideCopy;   // the sidechain, saved before a Mono -> Stereo copy overwrites it
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float value (const char* id) const { return parameters.getRawParameterValue (id)->load(); }
    int choice (const char* id) const { return (int) value (id); }
    bool flag (const char* id) const { return value (id) > 0.5f; }

    uint64_t spanSeed (int64_t key) const;
    void startSpan (int64_t key, double start, const mimic::GenSettings&, bool keep, int64_t seedKey);
    void discontinuity();
    void publishScript (double span);

    double sr = 44100.0;
    int64_t now = 0;

    mimic::History history;
    mimic::OnsetList mainOnsets;
    mimic::OnsetDetector mainDetector, preyDetector;
    mimic::Classifier classifier;
    mimic::Player player;
    mimic::PitchShifter shifter;
    mimic::Script script, previousScript;

    mimic::Answerer answerer;
    std::vector<float> dryCopyL, dryCopyR, answerL, answerR;
    float levelMeter = 0.0f;
    std::array<std::pair<int, int>, 16> breakEvents {};   // this block's breaks: sample, length
    int numBreaks = 0;
    bool wasRest = false;

    // Timeline.
    double freePpq = 0.0, expectedPpq = -1.0;
    bool wasPlaying = false;
    int64_t spanKey = std::numeric_limits<int64_t>::min();
    double spanStart = 0.0;
    int segmentIndex = -1;
    int earlyRestIndex = -1;
    double earlyRestAt = 0.0;
    bool wasInFill = false;
    int fillsInRow = 0;
    int64_t lastFillKey = 0;
    std::atomic<int64_t> keptKey { 0 };
    bool keepWasOn = false;

    // Manual fill from a MIDI note.
    bool manualActive = false;
    double manualStart = 0.0;
    int64_t manualCount = 0;
    double manualOffset = 0.0;

    // The Throat: the filter, its stepped cutoff and the sweep.
    mimic::Svf throat;
    int64_t throatStep = std::numeric_limits<int64_t>::min();
    float throatJump = 0.0f, throatOctaves = 0.0f, throatGlide = 0.0f;
    float throatFrom = 0.0f, throatRamp = 1.0f, throatRampStep = 1.0f;

    // Cycle loops: how many bars the loop jumps have skipped back.
    int64_t loopOffset = 0, lastRawBar = 0;
    int throatCountdown = 0;

    // Prey.
    float preyEnv = 0.0f, preyPeak = 0.0f, preyHp = 0.0f, preyHpX = 0.0f, preyBrightEnv = 0.0f, preyBright = 0.0f;
    float preyAtt = 0.0f, preyRel = 0.0f, preyPeakRel = 0.0f, brightSmooth = 0.0f, hpCoeff = 0.0f;
    bool preyHigh = false;
    int midiNote = -1, midiHeld = 0;
    float midiVelocity = 0.0f;
    mimic::Rng slicerRng, possessRng;

    // Voice and Possess.
    mimic::Vocoder vocoder;
    float voiceMix = 0.0f, voiceStep = 0.0f, possessFlash = 0.0f;
    int64_t possessEnd = -1, possessNext = 0;
    double chopAt = -1.0, possessAt = -1.0;   // prey hits waiting for the grid (ppq)

    // Echo: the granular delay, and the Break duck on the echoes.
    mimic::GrainDelay echoDelay;
    float echoSend = 0.0f, echoRamp = 0.0f, restDuck = 1.0f, restDuckCoeff = 0.0f;
    int echoAlive = 0;

    // Teeth.
    mimic::Svf toothFilter, riser;
    int filterCountdown = 0;
    float pitchRatio = 1.0f, riserLevel = 0.0f, riserCoeff = 0.0f, glideCoeff = 0.0f, strikeFlash = 0.0f;
    int seedValue = 0;
    int lastMaterial = -1;   // the material the pattern playing now was written for
    bool rewrite = false;    // write the pattern playing now again (the material changed)

    // Mixing.
    float engage = 0.0f, engageStep = 0.0f, replaceMix = 1.0f, mixSmoothed = 1.0f, rampStep = 0.0f;
    float dryPower = 0.0f, wetPower = 0.0f, powerCoeff = 0.0f, compGain = 1.0f, compSmooth = 0.0f;
    float dryPeak = 0.0f, wetPeak = 0.0f, peakAttack = 0.0f, peakRelease = 0.0f;
    mimic::Svf sideLow;
    float bassFrequency = -1.0f;

    // Throw: now and then a hit is sent into the Gargoyle's reverb and its
    // tail rings on. The reverb only runs while a tail is alive.
    void flushThrow (float* left, float* right, int start, int count, float returnGain);
    gargoyle::Reverb throwVerb;
    mimic::OnsetDetector throwDetector;
    mimic::Rng throwRng;
    static constexpr int throwChunk = 256;
    std::array<float, throwChunk> sendL {}, sendR {}, verbL {}, verbR {}, duckBuf {};
    int sendCount = 0, sendStart = 0;
    float sendEnv = 0.0f, sendAttack = 0.0f, sendRelease = 0.0f;
    int sendHold = 0, verbAlive = 0, afterFill = 0;
    float throwFlash = 0.0f, lastThrowDecay = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicProcessor)
};
