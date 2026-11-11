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

    // v0.2: groove, rests and the reverb throw
    inline constexpr const char* swing        = "swing";
    inline constexpr const char* breaks       = "breaks";
    inline constexpr const char* throwAmount  = "throw";

    // v0.3: the beat masher, pattern length and the filter
    inline constexpr const char* useMash      = "useMash";
    inline constexpr const char* variation    = "variation";
    inline constexpr const char* filterType   = "filterType";
    inline constexpr const char* cutoff       = "cutoff";
    inline constexpr const char* resonance    = "resonance";
    inline constexpr const char* filterMove   = "filterMove";

    // v0.5: looped bars, where phrases are counted from, the Throat glide
    inline constexpr const char* loop         = "loop";
    inline constexpr const char* phraseStart  = "phraseStart";
    inline constexpr const char* filterGlide  = "filterGlide";

    // v0.6: two more teeth. Voice: the track speaks with the prey's voice
    // (flipped, Carve: the prey carves itself out of the track). Possess:
    // the prey's hits fire the Mimic's events (flipped, Haunt: its silences).
    inline constexpr const char* voice        = "voice";
    inline constexpr const char* voiceInvert  = "voiceInvert";
    inline constexpr const char* possess      = "possess";
    inline constexpr const char* possessInvert = "possessInvert";

    // v0.7: the granular delay switch and Dynamics.
    inline constexpr const char* useEcho      = "useEcho";
    inline constexpr const char* dynamics     = "dynamics";

    // v0.8: the Mimic answers. It waits for a gap in the playing and fills
    // it with the last phrase, bent. The beat masher (all of the above)
    // became Snares, an optional section of its own.
    inline constexpr const char* answer       = "answer";      // v0.10: a mix. 0 = track only, 50 % = both, 100 % = answers only
    inline constexpr const char* hunger       = "hunger";      // how soon and how often it answers
    inline constexpr const char* mockery      = "mockery";     // how far an answer is bent
    inline constexpr const char* space        = "space";       // the cave and the echoes
    inline constexpr const char* heat         = "heat";        // saturation
    inline constexpr const char* snares       = "snares";      // the beat masher on / off

    inline constexpr const char* trickParrot    = "trickParrot";
    inline constexpr const char* trickGhost     = "trickGhost";
    inline constexpr const char* trickCloud     = "trickCloud";
    inline constexpr const char* trickBackwards = "trickBackwards";
    inline constexpr const char* trickDub       = "trickDub";
    inline constexpr const char* trickMock      = "trickMock";
    inline constexpr const char* trickHum       = "trickHum";
    inline constexpr const char* trickRadio     = "trickRadio";

    // v0.9: Hunger split in two (how often / how soon), a master filter on
    // the answers, memory further back, free timing, answers in the breaks.
    inline constexpr const char* patience     = "patience";    // how long it waits for a gap
    inline constexpr const char* answerFilter = "answerFilter"; // -1 low-pass .. 0 open .. +1 high-pass
    inline constexpr const char* memory       = "memory";      // how often it reaches further back
    inline constexpr const char* timing       = "timing";      // song tempo or free
    inline constexpr const char* freeBeat     = "freeBeat";    // ms, the beat when free
    inline constexpr const char* answerBreaks = "answerBreaks"; // answers in the breaks the masher bites

    // v0.10: how long the cave and the echoes ring.
    inline constexpr const char* spaceLength  = "spaceLength";
    inline const juce::StringArray spaceLengthNames { "Short", "Medium", "Long", "Endless" };

    enum Timing { timingSong, timingFree };
    inline const juce::StringArray timingNames { "Song tempo", "Free (seconds)" };

    // In the order of mimic::Trick.
    inline const char* const trickIds[] { trickParrot, trickGhost, trickCloud, trickBackwards, trickDub, trickMock, trickHum, trickRadio };
    inline const juce::StringArray trickNames { "Parrot", "Ghost", "Cloud", "Backwards", "Dub", "Mock", "Hum", "Radio" };

    enum Loop { loopRepeat, loopEvolve };
    inline const juce::StringArray loopNames { "Same every pass", "New every pass" };

    // Phrases (fills and pattern groups) are counted from this bar.
    inline const juce::StringArray phraseStartNames { "Bar 1", "Bar 2", "Bar 3", "Bar 4", "Bar 5", "Bar 6", "Bar 7", "Bar 8",
                                                      "Bar 9", "Bar 10", "Bar 11", "Bar 12", "Bar 13", "Bar 14", "Bar 15", "Bar 16" };

    inline const juce::StringArray variationNames { "Every bar", "Every 2 bars", "Every 4 bars", "Every 8 bars" };
    inline constexpr int variationBars[] { 1, 2, 4, 8 };

    enum FilterType { filterOff, filterLow, filterBand, filterHigh };
    inline const juce::StringArray filterTypeNames { "Off", "Low-pass", "Band-pass", "High-pass" };

    enum Mode { modeWild, modeFill };
    inline const juce::StringArray modeNames { "Wild", "Fill" };

    inline const juce::StringArray fillEveryNames { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars" };
    inline constexpr int fillEveryBars[] { 1, 2, 4, 8, 16, 32 };

    enum FillLength { lastBeat, lastHalfBar, lastBar };
    inline const juce::StringArray fillLengthNames { "Last beat", "Last half bar", "Last bar" };

    enum Prey { preySelf, preySidechain, preyMidi };
    inline const juce::StringArray preyNames { "Self", "Sidechain", "MIDI" };

    enum Material { materialAuto, materialPercussive, materialMelodic, materialHarmonic, materialVoice };
    inline const juce::StringArray materialNames { "Auto", "Percussive", "Melodic", "Harmonic", "Voice" };

    // The content switches, in the order of mimic::contentKinds.
    inline const char* const contentIds[] { useStutter, useRoll, useReverse, useScatter, useTape, useGate, useGrains, useMash, useEcho };
    inline const juce::StringArray contentNames { "Stutter", "Roll", "Reverse", "Scatter", "Tape", "Gate", "Grains", "Mash", "Echo" };
}
