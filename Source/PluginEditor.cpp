#include "PluginEditor.h"
#include "BinaryData.h"

using namespace mimic;

namespace
{
    const juce::Colour ink      { 0xff0e090c };
    const juce::Colour panel    { 0xff1d1418 };
    const juce::Colour edge     { 0xff3a2a2c };
    const juce::Colour gold     { 0xffe0b04a };
    const juce::Colour tongue   { 0xffc0281e };
    const juce::Colour bone     { 0xfff1e6bf };
    const juce::Colour dim      { 0xff8c7d70 };

    const char* const kindNames[] { "-", "STUTTER", "ROLL", "REVERSE", "SCATTER", "TAPE", "GATE", "GRAINS" };
    const char* const materialNames[] { "PERCUSSIVE", "MELODIC", "HARMONIC" };
    const char* const invertIds[] { ids::slicerInvert, ids::shaperInvert, ids::filterInvert, ids::pitcherInvert };
}

MimicEditor::Look::Look()
{
    bangers = juce::Typeface::createSystemTypefaceFor (BinaryData::BangersRegular_ttf, BinaryData::BangersRegular_ttfSize);
    typewriter = juce::Typeface::createSystemTypefaceFor (BinaryData::SpecialEliteRegular_ttf, BinaryData::SpecialEliteRegular_ttfSize);

    setColour (juce::Slider::textBoxTextColourId, bone);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, bone);
    setColour (juce::ComboBox::backgroundColourId, ink);
    setColour (juce::ComboBox::outlineColourId, edge);
    setColour (juce::ComboBox::textColourId, bone);
    setColour (juce::ComboBox::arrowColourId, gold);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, bone);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, tongue);
    setColour (juce::PopupMenu::headerTextColourId, gold);
    setColour (juce::ToggleButton::textColourId, bone);
    setColour (juce::ToggleButton::tickColourId, gold);
    setColour (juce::ToggleButton::tickDisabledColourId, dim);
    setColour (juce::TextButton::buttonColourId, ink);
    setColour (juce::TextButton::textColourOffId, gold);
    setColour (juce::TextButton::buttonOnColourId, tongue);
    setDefaultSansSerifTypeface (typewriter);
}

void MimicEditor::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float start, float end, juce::Slider&)
{
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto centre = area.getCentre();
    const float angle = start + pos * (end - start);

    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
    g.setColour (edge);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (gold);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (ink);
    g.fillEllipse (juce::Rectangle<float> (radius * 1.4f, radius * 1.4f).withCentre (centre));
    g.setColour (bone);
    g.drawLine (juce::Line<float> (centre, centre.getPointOnCircumference (radius * 0.68f, angle)), 2.5f);
}

juce::Font MimicEditor::Look::getComboBoxFont (juce::ComboBox&) { return juce::Font (juce::FontOptions (typewriter).withHeight (14.0f)); }
juce::Font MimicEditor::Look::getLabelFont (juce::Label&)       { return juce::Font (juce::FontOptions (typewriter).withHeight (13.0f)); }

//==============================================================================
MimicEditor::MimicEditor (MimicProcessor& p) : AudioProcessorEditor (p), mimicProcessor (p)
{
    setLookAndFeel (&look);

    setUpKnob (bite, ids::bite, "BITE");
    setUpKnob (speed, ids::speed, "SPEED");
    setUpKnob (mix, ids::mix, "MIX");
    setUpKnob (output, ids::output, "OUTPUT");
    setUpKnob (probability, ids::probability, "CHANCE");
    setUpKnob (anticipation, ids::anticipation, "RISER");
    setUpKnob (lure, ids::lure, "LURE");
    setUpKnob (slicer, ids::slicer, "SLICER");
    setUpKnob (shaper, ids::shaper, "SHAPER");
    setUpKnob (filter, ids::filter, "FILTER");
    setUpKnob (pitcher, ids::pitcher, "PITCHER");
    setUpKnob (monoBass, ids::monoBass, "MONO BASS");

    setUpPick (mode, ids::mode, "MODE");
    setUpPick (every, ids::fillEvery, "EVERY");
    setUpPick (length, ids::fillLength, "WHERE");
    setUpPick (prey, ids::prey, "PREY");
    setUpPick (material, ids::material, "MATERIAL");

    setUpSwitch (replace, ids::replace, "Replace");
    setUpSwitch (escalate, ids::escalate, "Escalate");
    setUpSwitch (keep, ids::keep, "Keep");

    for (int i = 0; i < numContent; ++i)
        setUpSwitch (contents[i], ids::contentIds[i], ids::contentNames[i]);

    for (int i = 0; i < 4; ++i)
        setUpSwitch (inverts[i], invertIds[i], "Invert");

    addAndMakeVisible (presetBox);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        const auto& factory = PresetManager::getFactory();

        if (id > 0 && id <= factory.size())
            mimicProcessor.presets.loadFactory (id - 1);
        else if (id > 1000)
        {
            const auto user = mimicProcessor.presets.getUserPresets();

            if (juce::isPositiveAndBelow (id - 1001, user.size()))
                mimicProcessor.presets.loadUser (user[id - 1001]);
        }
    };

    for (auto* b : { &previous, &next, &save, &reroll })
        addAndMakeVisible (*b);

    previous.onClick = [this] { mimicProcessor.presets.step (-1); };
    next.onClick = [this] { mimicProcessor.presets.step (1); };
    reroll.onClick = [this] { mimicProcessor.reroll(); };
    save.onClick = [this]
    {
        auto* window = new juce::AlertWindow ("Save preset", "Name the preset:", juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", mimicProcessor.presets.getCurrentName());
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            if (result == 1)
                mimicProcessor.presets.saveUser (window->getTextEditorContents ("name"));

            refreshPresets();
        }), true);
    };

    refreshPresets();
    setSize (900, 610);
    startTimerHz (30);
}

MimicEditor::~MimicEditor()
{
    setLookAndFeel (nullptr);
}

void MimicEditor::setUpKnob (Knob& k, const char* id, const juce::String& name)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, gold);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (mimicProcessor.parameters, id, k.slider);
}

void MimicEditor::setUpPick (Pick& p, const char* id, const juce::String& name)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (mimicProcessor.parameters.getParameter (id)))
        p.box.addItemList (param->choices, 1);

    p.label.setText (name, juce::dontSendNotification);
    p.label.setColour (juce::Label::textColourId, gold);
    addAndMakeVisible (p.box);
    addAndMakeVisible (p.label);
    p.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (mimicProcessor.parameters, id, p.box);
}

void MimicEditor::setUpSwitch (Switch& s, const char* id, const juce::String& name)
{
    s.button.setButtonText (name);
    addAndMakeVisible (s.button);
    s.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (mimicProcessor.parameters, id, s.button);
}

void MimicEditor::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    const auto& factory = PresetManager::getFactory();

    presetBox.addSectionHeading ("FILL MACHINES");
    for (int i = 0; i < factory.size(); ++i)
        if (factory[i].fill)
            presetBox.addItem (factory[i].name + "   (" + factory[i].tags + ")", i + 1);

    presetBox.addSectionHeading ("WILD EFFECTS");
    for (int i = 0; i < factory.size(); ++i)
        if (! factory[i].fill)
            presetBox.addItem (factory[i].name + "   (" + factory[i].tags + ")", i + 1);

    const auto user = mimicProcessor.presets.getUserPresets();

    if (! user.isEmpty())
    {
        presetBox.addSectionHeading ("YOURS");

        for (int i = 0; i < user.size(); ++i)
            presetBox.addItem (user[i].getFileNameWithoutExtension(), 1001 + i);
    }

    presetShown = {};
}

void MimicEditor::updateVisibility()
{
    const bool fillMode = (int) mimicProcessor.parameters.getRawParameterValue (ids::mode)->load() == ids::modeFill;

    for (juce::Component* c : { (juce::Component*) &every.box, (juce::Component*) &length.box, (juce::Component*) &probability.slider,
                                (juce::Component*) &anticipation.slider, (juce::Component*) &lure.slider, (juce::Component*) &replace.button,
                                (juce::Component*) &escalate.button })
        c->setEnabled (fillMode);

    mix.slider.setEnabled (! fillMode);
    bite.label.setText (fillMode ? "BITE (PEAK)" : "BITE", juce::dontSendNotification);
}

void MimicEditor::timerCallback()
{
    const auto name = mimicProcessor.presets.getCurrentName();

    if (name != presetShown)
    {
        presetShown = name;
        int found = 0;

        for (int i = 0; i < presetBox.getNumItems(); ++i)
            if (presetBox.getItemText (i).upToFirstOccurrenceOf ("   (", false, false) == name)
                found = presetBox.getItemId (i);

        if (found != 0)
            presetBox.setSelectedId (found, juce::dontSendNotification);
        else
            presetBox.setText (name, juce::dontSendNotification);
    }

    updateVisibility();
    repaint (statusArea);
    repaint (teethArea);
}

//==============================================================================
void MimicEditor::paint (juce::Graphics& g)
{
    g.fillAll (ink);

    g.setColour (tongue);
    g.setFont (juce::Font (juce::FontOptions (look.bangers).withHeight (58.0f)));
    g.drawText ("MIMIC", 20, 6, 220, 64, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (look.typewriter).withHeight (13.0f)));
    g.drawText ("it learns your sound. then it bites.", 190, 34, 320, 20, juce::Justification::centredLeft);

    auto drawPanel = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.5f);
        g.setColour (gold);
        g.setFont (juce::Font (juce::FontOptions (look.bangers).withHeight (22.0f)));
        g.drawText (title, r.getX() + 12, r.getY() + 4, 300, 24, juce::Justification::centredLeft);
    };

    drawPanel (huntArea, "THE HUNT");
    drawPanel (fillArea, "FILLS");
    drawPanel (contentArea, "WHAT IT CHEWS");
    drawPanel (preyArea, "THE PREY");
    drawPanel (teethArea, "TEETH");
    drawPanel (statusArea, "");

    // Teeth light up while they bite.
    juce::Slider* teethKnobs[] { &slicer.slider, &shaper.slider, &filter.slider, &pitcher.slider };

    for (int i = 0; i < 4; ++i)
    {
        const float level = juce::jlimit (0.0f, 1.0f, mimicProcessor.toothOut[i].load());
        const auto b = teethKnobs[i]->getBounds();
        juce::Path tooth;
        const float cx = (float) b.getCentreX(), top = (float) b.getY() - 30.0f;
        tooth.addTriangle (cx - 9.0f, top, cx + 9.0f, top, cx, top + 16.0f);
        g.setColour (bone.withAlpha (0.25f + 0.75f * level));
        g.fillPath (tooth);
    }

    // Status line and the timeline to the next fill.
    const auto s = statusArea.reduced (14, 8);
    const bool wildMode = (int) mimicProcessor.parameters.getRawParameterValue (ids::mode)->load() == ids::modeWild;
    const int heard = juce::jlimit (0, 2, mimicProcessor.materialOut.load());
    const int kind = juce::jlimit (0, 7, mimicProcessor.kindOut.load());
    const int bars = mimicProcessor.barsToFillOut.load();

    juce::String text;
    text << "HEARS: " << materialNames[heard]
         << "   |   BITING: " << kindNames[kind];

    if (! wildMode)
        text << "   |   " << (mimicProcessor.inFillOut.load() ? juce::String ("FILL!")
                             : ! mimicProcessor.transportPlaying.load() ? juce::String ("press play (or the trigger note)")
                             : bars < 0 ? juce::String ("no fill this time")
                             : bars == 0 ? juce::String ("fill this bar") : "next fill in " + juce::String (bars) + (bars == 1 ? " bar" : " bars"));

    if (mimicProcessor.sidechainMissing.load())
        text << "   |   NO SIDECHAIN: route a track to PREY IN";

    g.setColour (bone);
    g.setFont (juce::Font (juce::FontOptions (look.typewriter).withHeight (14.0f)));
    g.drawText (text, s.withHeight (20), juce::Justification::centredLeft);

    const auto bar = s.withTrimmedTop (28).withHeight (14).toFloat();
    g.setColour (edge);
    g.fillRoundedRectangle (bar, 4.0f);
    const float progress = mimicProcessor.fillProgressOut.load();
    g.setColour (mimicProcessor.inFillOut.load() ? tongue : gold.withAlpha (0.7f));
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (wildMode ? mimicProcessor.engageOut.load() : progress)), 4.0f);
}

void MimicEditor::resized()
{
    huntArea    = { 16, 80, 420, 190 };
    fillArea    = { 446, 80, 438, 190 };
    contentArea = { 16, 280, 868, 64 };
    preyArea    = { 16, 354, 300, 182 };
    teethArea   = { 326, 354, 558, 182 };
    statusArea  = { 16, 544, 868, 56 };

    presetBox.setBounds (520, 22, 260, 28);
    previous.setBounds (488, 22, 28, 28);
    next.setBounds (784, 22, 28, 28);
    save.setBounds (818, 22, 66, 28);

    auto placeKnob = [] (Knob& k, int x, int y, int size)
    {
        k.label.setBounds (x - 20, y, size + 40, 16);
        k.slider.setBounds (x, y + 16, size, size + 18);
    };

    auto placePick = [] (Pick& p, int x, int y, int w)
    {
        p.label.setBounds (x, y, w, 16);
        p.box.setBounds (x, y + 18, w, 26);
    };

    placePick (mode, 30, 112, 110);
    placeKnob (bite, 160, 110, 76);
    placeKnob (speed, 256, 110, 64);
    placeKnob (mix, 340, 110, 64);
    placeKnob (output, 40, 172, 56);

    placePick (every, 460, 112, 100);
    placePick (length, 460, 166, 130);
    placeKnob (probability, 606, 110, 58);
    placeKnob (anticipation, 690, 110, 58);
    placeKnob (lure, 774, 110, 58);
    replace.button.setBounds (606, 228, 90, 24);
    escalate.button.setBounds (700, 228, 96, 24);
    keep.button.setBounds (796, 196, 80, 24);
    reroll.setBounds (796, 228, 80, 26);

    for (int i = 0; i < numContent; ++i)
        contents[i].button.setBounds (30 + i * 120, 312, 116, 24);

    placePick (prey, 30, 388, 130);
    placePick (material, 170, 388, 130);
    placeKnob (monoBass, 40, 440, 56);

    Knob* teeth[] { &slicer, &shaper, &filter, &pitcher };

    for (int i = 0; i < 4; ++i)
    {
        const int x = 360 + i * 132;
        placeKnob (*teeth[i], x, 402, 66);
        inverts[i].button.setBounds (x - 4, 506, 80, 22);
    }
}
