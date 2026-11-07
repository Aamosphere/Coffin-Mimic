#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// v0.1 working editor: every control, grouped, plus live status. The
// chest-monster UI replaces it once the sound is settled.
class MimicEditor : public juce::AudioProcessorEditor,
                    public juce::Timer
{
public:
    explicit MimicEditor (MimicProcessor&);
    ~MimicEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Look : juce::LookAndFeel_V4
    {
        Look();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float start, float end, juce::Slider&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getLabelFont (juce::Label&) override;
        juce::Typeface::Ptr bangers, typewriter;
    };

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
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

public:
    void timerCallback() override;
private:
    void setUpKnob (Knob&, const char* id, const juce::String& name);
    void setUpPick (Pick&, const char* id, const juce::String& name);
    void setUpSwitch (Switch&, const char* id, const juce::String& name);
    void refreshPresets();
    void updateVisibility();

    MimicProcessor& mimicProcessor;
    Look look;

    Knob bite, speed, mix, output, probability, anticipation, lure, slicer, shaper, filter, pitcher, monoBass, swing, breaks, throwKnob, cutoff, resonance, move;
    Pick mode, every, length, prey, material, variation, throatType;
    Switch replace, escalate, keep, contents[mimic::numContent], inverts[4];

    juce::ComboBox presetBox;
    juce::TextButton previous { "<" }, next { ">" }, save { "SAVE" }, reroll { "REROLL" };

    juce::Rectangle<int> huntArea, fillArea, contentArea, throatArea, preyArea, teethArea, statusArea, timelineArea;
    juce::String presetShown;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicEditor)
};
