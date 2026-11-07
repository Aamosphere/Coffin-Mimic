#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// The Mimic's face: a treasure chest in a dungeon. Its lid lifts and its
// teeth grow with Bite (wider still during a fill); Bite itself is the big
// gold lock plate. The front page holds what a preset user touches - Bite,
// Wild/Fill and their settings, Speed, Mix and what it chews - and
// ADVANCED opens a drawer below it with the rest, so the front page always
// stays in view.
class MimicEditor : public juce::AudioProcessorEditor,
                    public juce::Timer
{
public:
    explicit MimicEditor (MimicProcessor&);
    ~MimicEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    static constexpr int frontHeight = 620, drawerHeight = 236;
    // The Aamosphere spine on the left, then the plugin.
    static constexpr int spineWidth = 56, contentWidth = 1010;

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

    // The drawer behind ADVANCED: it paints its own section frames.
    struct Drawer : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };

    void setUpKnob (juce::Component& parent, Knob&, const char* id, const juce::String& name, bool textBox);
    void setUpPick (juce::Component& parent, Pick&, const char* id, const juce::String& name);
    void setUpSwitch (juce::Component& parent, Switch&, const char* id, const juce::String& name, bool chip);
    void refreshPresets();
    void showAdvanced (bool);
    void updateModeControls();
    void updateTeethNames();
    void paintChest (juce::Graphics&);
    void paintModePanel (juce::Graphics&);
    void paintPattern (juce::Graphics&, juce::Rectangle<float>);
    void paintDrawer (juce::Graphics&);
    void paintFront (juce::Graphics&);
    juce::String valueText (const char* id) const;
    float value (const char* id) const;
    bool fillMode() const;

    MimicProcessor& mimicProcessor;
    Look look;
    juce::TooltipWindow tooltips { this, 500 };

    // Everything but the spine sits on the stage.
    Drawer stage;

    // Front page.
    Knob bite, speed, mix;
    Knob breaks, throwKnob, dynamicsKnob;   // the groove column
    Switch contents[mimic::numContent];
    juce::TextButton wildButton { "WILD" }, fillButton { "FILL" }, advancedButton { "ADVANCED" };
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    juce::ComboBox presetBox;
    juce::TextButton previous { "<" }, next { ">" }, save { "SAVE" };

    // The mode panel: Wild's pattern settings or Fill's schedule.
    Pick variation, loop, every, length, phraseStart;
    Knob probability;

    // The drawer.
    Drawer drawer;
    Knob swing, output, monoBass;
    Pick throatType;
    Knob cutoff, resonance, move, glide;
    Knob anticipation, lure;
    Switch replace, escalate, keep;
    juce::TextButton reroll { "REROLL" };
    Pick prey, material;
    static constexpr int numTeeth = 6;
    Knob slicer, shaper, filter, pitcher, voiceTooth, possess;
    Switch inverts[numTeeth];

    juce::Rectangle<int> chewsArea, modeArea, patternArea, grooveColumn, globalArea;
    juce::Rectangle<int> grooveArea, throatArea, fillsArea, preyArea, preyMeter;
    juce::String presetShown;
    float lidOpen = 0.0f;       // smoothed lift, 0..1+
    float eyeGlow = 0.0f;
    bool fillLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicEditor)
};
