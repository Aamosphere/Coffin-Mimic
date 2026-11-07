#pragma once

#include <JuceHeader.h>
#include "Parameters.h"
#include "Engine.h"
#include "Presets.h"

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

    const juce::String getName() const override { return "Mimic"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

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
    std::atomic<float> toothOut[4] { 0.0f, 0.0f, 0.0f, 0.0f };   // slicer, shaper, filter, pitcher
    std::atomic<bool> sidechainMissing { false };
    std::atomic<bool> transportPlaying { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float value (const char* id) const { return parameters.getRawParameterValue (id)->load(); }
    int choice (const char* id) const { return (int) value (id); }
    bool flag (const char* id) const { return value (id) > 0.5f; }

    uint64_t spanSeed (int64_t key) const;
    void startSpan (int64_t key, double start, const mimic::GenSettings&, bool keep);
    void discontinuity();

    double sr = 44100.0;
    int64_t now = 0;

    mimic::History history;
    mimic::OnsetList mainOnsets;
    mimic::OnsetDetector mainDetector, preyDetector;
    mimic::Classifier classifier;
    mimic::Player player;
    mimic::PitchShifter shifter;
    mimic::Script script, previousScript;

    // Timeline.
    double freePpq = 0.0, expectedPpq = -1.0;
    bool wasPlaying = false;
    int64_t spanKey = std::numeric_limits<int64_t>::min();
    double spanStart = 0.0;
    int segmentIndex = -1;
    bool wasInFill = false;
    int fillsInRow = 0;
    int64_t lastFillKey = 0;
    std::atomic<int64_t> keptKey { 0 };
    bool keepWasOn = false;

    // Manual fill from a MIDI note.
    bool manualActive = false;
    double manualStart = 0.0;
    int64_t manualCount = 0;

    // Prey.
    float preyEnv = 0.0f, preyPeak = 0.0f, preyHp = 0.0f, preyHpX = 0.0f, preyBrightEnv = 0.0f, preyBright = 0.0f;
    float preyAtt = 0.0f, preyRel = 0.0f, preyPeakRel = 0.0f, brightSmooth = 0.0f, hpCoeff = 0.0f;
    bool preyHigh = false;
    int midiNote = -1, midiHeld = 0;
    float midiVelocity = 0.0f;
    mimic::Rng slicerRng;

    // Teeth.
    mimic::Svf toothFilter, riser;
    int filterCountdown = 0;
    float pitchRatio = 1.0f, riserLevel = 0.0f, riserCoeff = 0.0f, glideCoeff = 0.0f, strikeFlash = 0.0f;
    int seedValue = 0;

    // Mixing.
    float engage = 0.0f, engageStep = 0.0f, replaceMix = 1.0f, mixSmoothed = 1.0f, rampStep = 0.0f;
    float dryPower = 0.0f, wetPower = 0.0f, powerCoeff = 0.0f, compGain = 1.0f, compSmooth = 0.0f;
    float dryPeak = 0.0f, wetPeak = 0.0f, peakAttack = 0.0f, peakRelease = 0.0f;
    mimic::Svf sideLow;
    float bassFrequency = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicProcessor)
};
