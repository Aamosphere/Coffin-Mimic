#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// The Mimic's face: a treasure chest in a dungeon that listens to your
// track and answers back. The front page has three sections, each in its own
// frame:
//   IT LISTENS  what it hears, and what it is doing about it right now
//               (hearing, waiting for a gap, answering, stepping back)
//   IT ANSWERS  Hunger and Mockery, the space and heat, and its tricks
//   SNARES      the beat masher, off until switched on: mild to wild
// ADVANCED opens a drawer below with the rest of Snares and the output.
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

    // A component that paints with a function (the stage, the drawer).
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
    void updateSnaresLook();
    void updateChipNames();
    void paintChest (juce::Graphics&);
    void paintPattern (juce::Graphics&, juce::Rectangle<float>);
    void paintSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, const juce::String& subtitle, bool lit);
    void paintListens (juce::Graphics&);
    void paintAnswers (juce::Graphics&);
    void paintSnares (juce::Graphics&);
    void paintDrawer (juce::Graphics&);
    void paintFront (juce::Graphics&);
    void knobCaption (juce::Graphics&, const Knob&, const juce::String& name, const char* id, float nameSize, const juce::String& help = {});
    juce::String valueText (const char* id) const;
    float value (const char* id) const;
    bool fillMode() const;
    int heardMaterial() const;

    MimicProcessor& mimicProcessor;
    Look look;
    juce::TooltipWindow tooltips { this, 500 };

    // Everything but the spine sits on the stage.
    Drawer stage;

    // Header.
    juce::ComboBox presetBox;
    juce::TextButton previous { "<" }, next { ">" }, save { "SAVE" }, advancedButton { "ADVANCED" };

    // IT LISTENS.
    Pick material;

    // IT ANSWERS.
    Knob hunger, mockery, space, heat, answerLevel;
    Switch tricks[mimic::numTricks];

    // SNARES.
    Switch snaresOn;
    Knob bite, speed, breaks, throwKnob, dynamicsKnob;
    Switch contents[mimic::numContent];
    juce::TextButton wildButton { "WILD" }, fillButton { "FILL" };
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;

    // The drawer.
    Drawer drawer;
    Pick variation, loop, every, length, phraseStart;
    Knob probability;
    Knob swing, mix, output, monoBass;
    Pick throatType;
    Knob cutoff, resonance, move, glide;
    Knob anticipation, lure;
    Switch replace, escalate, keep;
    juce::TextButton reroll { "REROLL" };

    juce::Rectangle<int> listensArea, answersArea, snaresArea, ladderArea, chewsArea, patternArea, tricksArea, meterArea;
    juce::Rectangle<int> patternBox, throatBox, fillsBox, outBox;
    juce::String presetShown;
    float lidOpen = 0.0f;       // smoothed lift, 0..1+
    float eyeGlow = 0.0f;
    bool fillLayout = false;
    int chipMaterial = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicEditor)
};
