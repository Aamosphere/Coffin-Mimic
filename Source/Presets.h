#pragma once

#include <JuceHeader.h>

namespace mimic
{
    // Presets are the main way to play the Mimic. Factory presets are built
    // in and tagged by what they sound like; user presets are XML files in
    // ~/Library/Audio/Presets/Aamosphere/Mimic. The current preset name is
    // kept in the state so it is saved with the project.
    class PresetManager
    {
    public:
        explicit PresetManager (juce::AudioProcessorValueTreeState&);

        struct Info { juce::String name, tags; bool fill; bool answers; };
        static const juce::Array<Info>& getFactory();
        void loadFactory (int index);

        static juce::File userFolder();
        juce::Array<juce::File> getUserPresets() const;
        void loadUser (const juce::File&);
        bool saveUser (const juce::String& name);

        juce::String getCurrentName() const;
        void step (int delta);   // factory presets, then user presets

    private:
        void resetToDefaults();
        void setName (const juce::String&);

        juce::AudioProcessorValueTreeState& state;
    };
}
