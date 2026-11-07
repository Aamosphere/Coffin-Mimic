#pragma once

#include <JuceHeader.h>

// Parameter identifiers shared by the processor, the presets and the editor.
//
// LOCKED: these IDs and the order of every list below are saved in user
// projects and automation. Never rename or reorder them; only append.
namespace mimic::ids
{
    // The hunt
    inline constexpr const char* mode         = "mode";
    inline constexpr const char* bite         = "bite";
    inline constexpr const char* speed        = "speed";
    inline constexpr const char* mix          = "mix";
    inline constexpr const char* output       = "output";

    // Fill scheduling
    inline constexpr const char* fillEvery    = "fillEvery";
    inline constexpr const char* fillLength   = "fillLength";
    inline constexpr const char* probability  = "probability";
    inline constexpr const char* replace      = "replace";
    inline constexpr const char* anticipation = "anticipation";
    inline constexpr const char* escalate     = "escalate";
    inline constexpr const char* lure         = "lure";
    inline constexpr const char* seed         = "seed";
    inline constexpr const char* keep         = "keep";
    inline constexpr const char* triggerNote  = "triggerNote";

    // Fill content
    inline constexpr const char* useStutter   = "useStutter";
    inline constexpr const char* useRoll      = "useRoll";
    inline constexpr const char* useReverse   = "useReverse";
    inline constexpr const char* useScatter   = "useScatter";
    inline constexpr const char* useTape      = "useTape";
    inline constexpr const char* useGate      = "useGate";
    inline constexpr const char* useGrains    = "useGrains";

    // Prey and teeth
    inline constexpr const char* prey         = "prey";
    inline constexpr const char* material     = "material";
    inline constexpr const char* slicer       = "slicer";
    inline constexpr const char* shaper       = "shaper";
    inline constexpr const char* filter       = "filter";
    inline constexpr const char* pitcher      = "pitcher";
    inline constexpr const char* slicerInvert = "slicerInvert";
    inline constexpr const char* shaperInvert = "shaperInvert";
    inline constexpr const char* filterInvert = "filterInvert";
    inline constexpr const char* pitcherInvert = "pitcherInvert";

    // Safety
    inline constexpr const char* monoBass     = "monoBass";

    enum Mode { modeWild, modeFill };
    inline const juce::StringArray modeNames { "Wild", "Fill" };

    inline const juce::StringArray fillEveryNames { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars" };
    inline constexpr int fillEveryBars[] { 1, 2, 4, 8, 16, 32 };

    enum FillLength { lastBeat, lastHalfBar, lastBar };
    inline const juce::StringArray fillLengthNames { "Last beat", "Last half bar", "Last bar" };

    enum Prey { preySelf, preySidechain, preyMidi };
    inline const juce::StringArray preyNames { "Self", "Sidechain", "MIDI" };

    enum Material { materialAuto, materialPercussive, materialMelodic, materialHarmonic };
    inline const juce::StringArray materialNames { "Auto", "Percussive", "Melodic", "Harmonic" };

    // The content switches, in the order of mimic::Kind (after pass).
    inline const char* const contentIds[] { useStutter, useRoll, useReverse, useScatter, useTape, useGate, useGrains };
    inline const juce::StringArray contentNames { "Stutter", "Roll", "Reverse", "Scatter", "Tape", "Gate", "Grains" };
}
