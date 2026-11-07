#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// The Mimic's face: a treasure chest in a dungeon. Its lid lifts and its
// teeth grow with Bite (wider still during a fill); Bite itself is the big
// gold lock plate. The front page holds what a preset user touches - Bite,
// Wild/Fill, Speed, Mix and what it chews - and ADVANCED opens the rest.
class MimicEditor : public juce::AudioProcessorEditor,
                    public juce::Timer
{
public:
    explicit MimicEditor (MimicProcessor&);
    ~MimicEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    struct Look : juce::LookAndFeel_V4
    {
        Look();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float start, float end, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int height) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getLabelFont (juce::Label&) override;
        juce::Font getPopupMenuFont() override;
        juce::Typeface::Ptr bangers, typewriter, eater;
    };

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Pick
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Switch
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    // The page behind ADVANCED: it paints its own section frames.
    struct Advanced : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };

    void setUpKnob (juce::Component& parent, Knob&, const char* id, const juce::String& name, bool textBox);
    void setUpPick (juce::Component& parent, Pick&, const char* id, const juce::String& name);
    void setUpSwitch (juce::Component& parent, Switch&, const char* id, const juce::String& name, bool chip);
    void refreshPresets();
    void showAdvanced (bool);
    void paintChest (juce::Graphics&);
    void paintAdvanced (juce::Graphics&);
    juce::String valueText (const char* id) const;
    float value (const char* id) const;

    MimicProcessor& mimicProcessor;
    Look look;
    juce::TooltipWindow tooltips { this, 500 };

    // Front page.
    Knob bite, speed, mix;
    Switch contents[mimic::numContent];
    juce::TextButton wildButton { "WILD" }, fillButton { "FILL" }, advancedButton { "ADVANCED" };
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    juce::ComboBox presetBox;
    juce::TextButton previous { "<" }, next { ">" }, save { "SAVE" };

    // Advanced page.
    Advanced advanced;
    Knob probability, anticipation, lure, swing, breaks, throwKnob, output,
         cutoff, resonance, move, monoBass, slicer, shaper, filter, pitcher;
    Pick every, length, variation, throatType, prey, material;
    Switch replace, escalate, keep, inverts[4];
    juce::TextButton reroll { "REROLL" };

    juce::Rectangle<int> infoArea, fillsArea, grooveArea, throatArea, preyArea, teethArea;
    juce::String presetShown;
    float lidOpen = 0.0f;       // smoothed lift, 0..1+
    float eyeGlow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicEditor)
};
