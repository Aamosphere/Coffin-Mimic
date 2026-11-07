#include "PluginEditor.h"
#include "BinaryData.h"

using namespace mimic;

namespace
{
    const juce::Colour ink       { 0xff0b0605 };
    const juce::Colour panel     { 0xff1c110c };
    const juce::Colour edge      { 0xff3a2a1c };
    const juce::Colour gold      { 0xffc9962e };
    const juce::Colour goldLight { 0xffe8b84a };
    const juce::Colour red       { 0xffc0281e };
    const juce::Colour redBright { 0xffe0352a };
    const juce::Colour bone      { 0xfff1e6bf };
    const juce::Colour dim       { 0xffb9a98a };
    const juce::Colour wood      { 0xff6b3d1c };
    const juce::Colour woodLid   { 0xff7d4722 };
    const juce::Colour woodLine  { 0xff4a2810 };
    const juce::Colour mouth     { 0xff2a0507 };

    // One name per mimic::Kind, in its order.
    const char* const kindNames[] { "-", "STUTTER", "ROLL", "REVERSE", "SCATTER", "TAPE", "GATE", "GRAINS", "BREAK", "MASH" };
    const char* const materialNames[] { "PERCUSSIVE", "MELODIC", "HARMONIC" };
    const char* const toothNames[] { "SLICER", "SHAPER", "FILTER", "PITCHER" };
    const char* const invertIds[] { ids::slicerInvert, ids::shaperInvert, ids::filterInvert, ids::pitcherInvert };
    const char* const toothHelp[] {
        "The prey's hits restart the slices.",
        "The prey's loudness shapes the level.",
        "The prey's brightness opens the filter.",
        "MIDI notes set the pitch (C3 = unchanged)."
    };

    // Where the chest is drawn; its drawing is in a 500 x 450 box.
    constexpr float chestX = 20.0f, chestY = 80.0f;

    juce::Font font (const juce::Typeface::Ptr& face, float height)
    {
        return juce::Font (juce::FontOptions (face).withHeight (height));
    }

    juce::Path zigzag (std::initializer_list<juce::Point<float>> points)
    {
        juce::Path p;
        bool first = true;

        for (auto pt : points)
        {
            if (first) p.startNewSubPath (pt);
            else       p.lineTo (pt);
            first = false;
        }

        p.closeSubPath();
        return p;
    }

    // A row of fangs along y = 0, pointing up (sign -1) or down (+1).
    juce::Path fangs (float sign)
    {
        juce::Path p;
        p.startNewSubPath (78.0f, 0.0f);
        const float xs[] { 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 262, 278, 294, 310, 326, 342, 358, 374, 390, 406, 422 };
        const float hs[] { 22, 0, 22, 0, 26, 0, 22, 0, 22, 0, 0, 22, 0, 22, 0, 26, 0, 22, 0, 22, 0 };

        for (size_t i = 0; i < std::size (xs); ++i)
            p.lineTo (xs[i], sign * hs[i]);

        p.closeSubPath();
        return p;
    }

    void arc (juce::Graphics& g, juce::Point<float> c, float r, float a0, float a1, float width, juce::Colour colour)
    {
        juce::Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
MimicEditor::Look::Look()
{
    bangers = juce::Typeface::createSystemTypefaceFor (BinaryData::BangersRegular_ttf, BinaryData::BangersRegular_ttfSize);
    typewriter = juce::Typeface::createSystemTypefaceFor (BinaryData::SpecialEliteRegular_ttf, BinaryData::SpecialEliteRegular_ttfSize);
    eater = juce::Typeface::createSystemTypefaceFor (BinaryData::EaterRegular_ttf, BinaryData::EaterRegular_ttfSize);

    setColour (juce::Label::textColourId, bone);
    setColour (juce::Slider::textBoxTextColourId, bone);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::outlineColourId, gold);
    setColour (juce::ComboBox::textColourId, bone);
    setColour (juce::ComboBox::arrowColourId, goldLight);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, bone);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, red);
    setColour (juce::PopupMenu::headerTextColourId, goldLight);
    setColour (juce::ToggleButton::textColourId, bone);
    setColour (juce::ToggleButton::tickColourId, goldLight);
    setColour (juce::ToggleButton::tickDisabledColourId, dim);
    setColour (juce::TextButton::textColourOffId, goldLight);
    setColour (juce::TextButton::textColourOnId, bone);
    setColour (juce::TooltipWindow::backgroundColourId, panel);
    setColour (juce::TooltipWindow::textColourId, bone);
    setColour (juce::TooltipWindow::outlineColourId, gold);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, bone);
    setColour (juce::TextEditor::backgroundColourId, ink);
    setColour (juce::TextEditor::textColourId, bone);
    setDefaultSansSerifTypeface (typewriter);
}

void MimicEditor::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float start, float end, juce::Slider& slider)
{
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const auto c = area.getCentre();
    const float angle = start + pos * (end - start);

    if (slider.getName() == "bite")
    {
        // The lock plate: a gold medallion with a keyhole.
        const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 3.0f;
        g.setColour (ink.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (2 * R + 4, 2 * R + 4).withCentre (c.translated (0, 6)));
        g.setColour (gold);
        g.fillEllipse (juce::Rectangle<float> (2 * R, 2 * R).withCentre (c));
        g.setColour (ink);
        g.drawEllipse (juce::Rectangle<float> (2 * R, 2 * R).withCentre (c), 5.0f);
        g.setColour (panel);
        g.fillEllipse (juce::Rectangle<float> (2 * (R - 12), 2 * (R - 12)).withCentre (c));
        arc (g, c, R - 24, start, end, 10.0f, edge);
        arc (g, c, R - 24, start, juce::jmax (start + 0.01f, angle), 10.0f, redBright);
        g.setColour (juce::Colour (0xff2a1a10));
        g.fillEllipse (juce::Rectangle<float> (2 * (R - 38), 2 * (R - 38)).withCentre (c));
        g.setColour (gold);
        g.drawEllipse (juce::Rectangle<float> (2 * (R - 38), 2 * (R - 38)).withCentre (c), 3.0f);

        juce::Path pointer;
        pointer.addTriangle (c.x, c.y - (R - 42), c.x + 8, c.y - (R - 66), c.x - 8, c.y - (R - 66));
        g.setColour (bone);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle, c.x, c.y));

        g.setColour (ink);
        g.fillEllipse (juce::Rectangle<float> (18, 18).withCentre (c.translated (0, -8)));
        g.fillPath (zigzag ({ { c.x - 5, c.y - 4 }, { c.x + 5, c.y - 4 }, { c.x + 8, c.y + 20 }, { c.x - 8, c.y + 20 } }));
        return;
    }

    // Small gold-rimmed knobs.
    const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 4.0f;
    g.setColour (juce::Colour (0xff2a1a10));
    g.fillEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (c));
    g.setColour (gold);
    g.drawEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (c), 2.5f);
    arc (g, c, r - 6, start, end, 4.0f, edge);
    arc (g, c, r - 6, start, juce::jmax (start + 0.01f, angle), 4.0f, goldLight);
    g.setColour (bone);
    g.drawLine (juce::Line<float> (c, c.getPointOnCircumference (r - 9, angle)), 3.0f);
}

void MimicEditor::Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    if (! (bool) b.getProperties()["chip"])
    {
        // Small checkbox with label.
        const auto box = juce::Rectangle<float> (2.0f, (float) b.getHeight() * 0.5f - 8.0f, 16.0f, 16.0f);
        g.setColour (panel);
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (b.getToggleState() ? goldLight : edge.brighter (0.4f));
        g.drawRoundedRectangle (box, 3.0f, 1.5f);

        if (b.getToggleState())
            g.fillRoundedRectangle (box.reduced (4.0f), 1.5f);

        g.setColour (highlighted ? bone : dim);
        g.setFont (font (typewriter, 13.0f));
        g.drawText (b.getButtonText(), 24, 0, b.getWidth() - 24, b.getHeight(), juce::Justification::centredLeft);
        return;
    }

    // Content chips: gold when the Mimic chews it.
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? gold : panel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? goldLight : (highlighted ? gold : edge.brighter (0.3f)));
    g.drawRoundedRectangle (r, 5.0f, 1.5f);
    g.setColour (on ? ink : dim);
    g.setFont (font (bangers, 19.0f));
    g.drawText (b.getButtonText().toUpperCase(), r, juce::Justification::centred);
}

void MimicEditor::Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const juce::String style = b.getProperties()["style"].toString();
    juce::Colour fill = panel;

    if (style == "gold")
        fill = highlighted ? goldLight : gold;
    else if (b.getToggleState())
        fill = red;
    else if (highlighted)
        fill = panel.brighter (0.15f);

    if (down)
        fill = fill.darker (0.2f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);

    if (style == "dashed")
    {
        juce::Path outline;
        outline.addRoundedRectangle (r, 6.0f);
        const float dashes[] { 6.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (gold);
        g.fillPath (dashed);
    }
    else if (style != "gold")
    {
        g.setColour (gold);
        g.drawRoundedRectangle (r, 6.0f, 2.0f);
    }
}

juce::Font MimicEditor::Look::getTextButtonFont (juce::TextButton&, int height)
{
    return font (bangers, juce::jmin (26.0f, (float) height * 0.55f));
}

juce::Font MimicEditor::Look::getComboBoxFont (juce::ComboBox&) { return font (typewriter, 14.0f); }
juce::Font MimicEditor::Look::getLabelFont (juce::Label&)       { return font (typewriter, 12.0f); }
juce::Font MimicEditor::Look::getPopupMenuFont()                 { return font (typewriter, 14.0f); }

//==============================================================================
MimicEditor::MimicEditor (MimicProcessor& p) : AudioProcessorEditor (p), mimicProcessor (p)
{
    setLookAndFeel (&look);

    // Front page.
    bite.slider.setName ("bite");
    setUpKnob (*this, bite, ids::bite, {}, false);
    bite.slider.setTooltip ("How hard the Mimic bites: from a little variation to breakbeat carnage.");
    setUpKnob (*this, speed, ids::speed, "SPEED", false);
    speed.slider.setTooltip ("The grid it chews on: 1/4, 1/8, 1/16 or 1/32.");
    setUpKnob (*this, mix, ids::mix, "MIX", false);

    for (int i = 0; i < numContent; ++i)
        setUpSwitch (*this, contents[i], ids::contentIds[i], ids::contentNames[i], true);

    contents[7].button.setTooltip ("Mash: moves pieces of the beat around, so the snare does not always land in the same place.");

    for (auto* b : { &wildButton, &fillButton })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (false);
    }

    wildButton.setTooltip ("Wild: the track is chewed all the time.");
    fillButton.setTooltip ("Fill: the track stays clean and the Mimic strikes at the end of each phrase.");

    modeAttachment = std::make_unique<juce::ParameterAttachment> (*p.parameters.getParameter (ids::mode), [this] (float v)
    {
        const bool fillMode = juce::roundToInt (v) == ids::modeFill;
        wildButton.setToggleState (! fillMode, juce::dontSendNotification);
        fillButton.setToggleState (fillMode, juce::dontSendNotification);
        repaint();
    });
    modeAttachment->sendInitialUpdate();
    wildButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture ((float) ids::modeWild); };
    fillButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture ((float) ids::modeFill); };

    addAndMakeVisible (advancedButton);
    advancedButton.getProperties().set ("style", "dashed");
    advancedButton.onClick = [this] { showAdvanced (! advanced.isVisible()); };

    // Presets.
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

    for (auto* b : { &previous, &next, &save })
        addAndMakeVisible (*b);

    save.getProperties().set ("style", "gold");
    save.setColour (juce::TextButton::textColourOffId, ink);
    previous.onClick = [this] { mimicProcessor.presets.step (-1); };
    next.onClick = [this] { mimicProcessor.presets.step (1); };
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

    // Advanced page.
    addChildComponent (advanced);
    advanced.painter = [this] (juce::Graphics& g) { paintAdvanced (g); };

    setUpPick (advanced, every, ids::fillEvery, "EVERY");
    setUpPick (advanced, length, ids::fillLength, "WHERE");
    setUpKnob (advanced, probability, ids::probability, "CHANCE", true);
    setUpKnob (advanced, anticipation, ids::anticipation, "RISER", true);
    setUpKnob (advanced, lure, ids::lure, "LURE", true);
    setUpSwitch (advanced, replace, ids::replace, "Replace", false);
    setUpSwitch (advanced, escalate, ids::escalate, "Escalate", false);
    setUpSwitch (advanced, keep, ids::keep, "Keep", false);
    advanced.addAndMakeVisible (reroll);
    reroll.onClick = [this] { mimicProcessor.reroll(); };
    reroll.setTooltip ("A new variation of the same preset.");
    keep.button.setTooltip ("Lock the current variation: it repeats exactly.");
    lure.slider.setTooltip ("A little imitation between fills too.");

    setUpKnob (advanced, swing, ids::swing, "SWING", true);
    setUpKnob (advanced, breaks, ids::breaks, "BREAKS", true);
    setUpKnob (advanced, throwKnob, ids::throwAmount, "THROW", true);
    setUpKnob (advanced, output, ids::output, "OUTPUT", true);
    setUpPick (advanced, variation, ids::variation, "NEW PATTERN");
    breaks.slider.setTooltip ("A rest after the chewing; often silence right before the downbeat.");
    throwKnob.slider.setTooltip ("Now and then a hit is thrown into the reverb and rings on.");

    setUpPick (advanced, throatType, ids::filterType, "TYPE");
    setUpKnob (advanced, cutoff, ids::cutoff, "CUTOFF", true);
    setUpKnob (advanced, resonance, ids::resonance, "RESO", true);
    setUpKnob (advanced, move, ids::filterMove, "MOVE", true);
    move.slider.setTooltip ("Jumps the cutoff in time with the grid and sweeps it through fills. Bite sets how far.");

    setUpPick (advanced, prey, ids::prey, "PREY");
    setUpPick (advanced, material, ids::material, "MATERIAL");
    setUpKnob (advanced, monoBass, ids::monoBass, "MONO BASS", true);
    prey.box.setTooltip ("What the Mimic listens to. Self = its own track.");

    Knob* teeth[] { &slicer, &shaper, &filter, &pitcher };
    const char* teethIds[] { ids::slicer, ids::shaper, ids::filter, ids::pitcher };

    for (int i = 0; i < 4; ++i)
    {
        setUpKnob (advanced, *teeth[i], teethIds[i], toothNames[i], true);
        teeth[i]->slider.setTooltip (toothHelp[i]);
        setUpSwitch (advanced, inverts[i], invertIds[i], "Invert", false);
    }

    refreshPresets();
    setSize (900, 620);
    startTimerHz (30);
}

MimicEditor::~MimicEditor()
{
    setLookAndFeel (nullptr);
}

void MimicEditor::setUpKnob (juce::Component& parent, Knob& k, const char* id, const juce::String& name, bool textBox)
{
    if (textBox)
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, goldLight);
    k.label.setFont (font (look.bangers, 17.0f));
    parent.addAndMakeVisible (k.slider);

    if (name.isNotEmpty() && textBox)
        parent.addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (mimicProcessor.parameters, id, k.slider);
}

void MimicEditor::setUpPick (juce::Component& parent, Pick& p, const char* id, const juce::String& name)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (mimicProcessor.parameters.getParameter (id)))
        p.box.addItemList (param->choices, 1);

    p.label.setText (name, juce::dontSendNotification);
    p.label.setColour (juce::Label::textColourId, goldLight);
    p.label.setFont (font (look.bangers, 17.0f));
    parent.addAndMakeVisible (p.box);
    parent.addAndMakeVisible (p.label);
    p.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (mimicProcessor.parameters, id, p.box);
}

void MimicEditor::setUpSwitch (juce::Component& parent, Switch& s, const char* id, const juce::String& name, bool chip)
{
    s.button.setButtonText (name);
    s.button.getProperties().set ("chip", chip);
    parent.addAndMakeVisible (s.button);
    s.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (mimicProcessor.parameters, id, s.button);
}

void MimicEditor::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    const auto& factory = PresetManager::getFactory();

    presetBox.addSectionHeading ("WILD EFFECTS");
    for (int i = 0; i < factory.size(); ++i)
        if (! factory[i].fill)
            presetBox.addItem (factory[i].name + "   (" + factory[i].tags + ")", i + 1);

    presetBox.addSectionHeading ("FILL MACHINES");
    for (int i = 0; i < factory.size(); ++i)
        if (factory[i].fill)
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

void MimicEditor::showAdvanced (bool show)
{
    advanced.setVisible (show);
    advancedButton.setButtonText (show ? "BACK" : "ADVANCED");
    advancedButton.toFront (false);
    repaint();
}

float MimicEditor::value (const char* id) const
{
    return mimicProcessor.parameters.getRawParameterValue (id)->load();
}

juce::String MimicEditor::valueText (const char* id) const
{
    if (auto* p = mimicProcessor.parameters.getParameter (id))
        return p->getCurrentValueAsText();

    return {};
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

    // The lid follows Bite and opens wider during a fill; the eyes light up
    // while it chews.
    const float b = value (ids::bite);
    const bool fillMode = juce::roundToInt (value (ids::mode)) == ids::modeFill;
    const bool inFill = mimicProcessor.inFillOut.load();
    const bool chewing = mimicProcessor.kindOut.load() != (int) Kind::pass;
    const float target = fillMode ? (inFill ? juce::jmin (1.25f, b + 0.35f) : 0.08f + 0.25f * b) : b;
    lidOpen += 0.3f * (target - lidOpen);
    eyeGlow += 0.25f * ((chewing ? 1.0f : juce::jlimit (0.0f, 1.0f, (lidOpen - 0.2f) / 0.4f)) - eyeGlow);

    repaint (0, 72, 560, 496);
    repaint (infoArea);
    repaint (0, 568, getWidth(), 52);
}

//==============================================================================
void MimicEditor::paintChest (juce::Graphics& g)
{
    const float open = juce::jlimit (0.0f, 1.3f, lidOpen);
    const float lift = open * 96.0f;
    const float teethScale = 0.15f + 0.85f * juce::jmin (1.0f, open);
    const auto at = juce::AffineTransform::translation (chestX, chestY);

    g.saveState();
    g.addTransform (at);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (40.0f, 382.0f, 420.0f, 36.0f);

    // Inside the mouth.
    g.setColour (mouth);
    g.fillRect (70.0f, 240.0f - lift, 360.0f, lift + 4.0f);

    if (eyeGlow > 0.01f)
    {
        const float eyeY = 240.0f - lift * 0.55f;

        for (float ex : { 180.0f, 320.0f })
        {
            g.setColour (juce::Colour (0xffffd23f).withAlpha (juce::jmin (1.0f, eyeGlow) * 0.35f));
            g.fillEllipse (ex - 16.0f, eyeY - 16.0f, 32.0f, 32.0f);
            g.setColour (juce::Colour (0xffffd23f).withAlpha (juce::jmin (1.0f, eyeGlow)));
            g.fillEllipse (ex - 9.0f, eyeY - 9.0f, 18.0f, 18.0f);
            g.setColour (mouth);
            g.fillEllipse (ex - 3.0f, eyeY - 7.0f, 6.0f, 14.0f);
        }
    }

    g.setColour (red);
    g.fillEllipse (160.0f, 236.0f - (4.0f + open * 18.0f), 180.0f, 2.0f * (4.0f + open * 18.0f));

    // Lower fangs, then the body.
    auto lower = fangs (-1.0f);
    lower.applyTransform (juce::AffineTransform::scale (1.0f, teethScale).translated (0.0f, 240.0f));
    g.setColour (bone);
    g.fillPath (lower);
    g.setColour (ink);
    g.strokePath (lower, juce::PathStrokeType (2.0f));

    const juce::Rectangle<float> body (60.0f, 240.0f, 380.0f, 150.0f);
    g.setColour (wood);
    g.fillRoundedRectangle (body, 8.0f);
    g.setColour (woodLine);

    for (float y : { 280.0f, 320.0f, 360.0f })
        g.drawLine (62.0f, y, 438.0f, y, 3.0f);

    g.setColour (gold);
    g.fillRect (92.0f, 240.0f, 22.0f, 150.0f);
    g.fillRect (386.0f, 240.0f, 22.0f, 150.0f);
    g.setColour (ink);
    g.drawRect (92.0f, 240.0f, 22.0f, 150.0f, 3.0f);
    g.drawRect (386.0f, 240.0f, 22.0f, 150.0f, 3.0f);
    g.drawRoundedRectangle (body, 8.0f, 4.0f);

    // The lid, lifted.
    g.saveState();
    g.addTransform (juce::AffineTransform::translation (0.0f, -lift));

    auto upper = fangs (1.0f);
    upper.applyTransform (juce::AffineTransform::scale (1.0f, teethScale).translated (0.0f, 240.0f));
    g.setColour (bone);
    g.fillPath (upper);
    g.setColour (ink);
    g.strokePath (upper, juce::PathStrokeType (2.0f));

    juce::Path lid;
    lid.startNewSubPath (56.0f, 240.0f);
    lid.lineTo (56.0f, 180.0f);
    lid.quadraticTo (250.0f, 104.0f, 444.0f, 180.0f);
    lid.lineTo (444.0f, 240.0f);
    lid.closeSubPath();
    g.setColour (woodLid);
    g.fillPath (lid);

    juce::Path grain;
    grain.startNewSubPath (58.0f, 205.0f);
    grain.quadraticTo (250.0f, 132.0f, 442.0f, 205.0f);
    g.setColour (woodLine);
    g.strokePath (grain, juce::PathStrokeType (3.0f));

    g.setColour (gold);
    g.fillPath (zigzag ({ { 90, 240 }, { 90, 168 }, { 100, 160 }, { 114, 156 }, { 114, 240 } }));
    g.fillPath (zigzag ({ { 386, 240 }, { 386, 156 }, { 400, 160 }, { 410, 168 }, { 410, 240 } }));
    g.fillRect (56.0f, 232.0f, 388.0f, 10.0f);
    g.setColour (ink);
    g.strokePath (lid, juce::PathStrokeType (4.0f));
    g.drawRect (56.0f, 232.0f, 388.0f, 10.0f, 3.0f);
    g.restoreState();

    g.restoreState();
}

void MimicEditor::paint (juce::Graphics& g)
{
    // Dungeon dark with a torch glow on the chest.
    juce::ColourGradient glow (juce::Colour (0xff3a1f12), 270.0f, 280.0f, juce::Colour (0xff0b0605), 820.0f, 280.0f, true);
    glow.addColour (0.55, juce::Colour (0xff160c08));
    g.setGradientFill (glow);
    g.fillAll();

    // Header.
    g.setFont (font (look.eater, 70.0f));
    g.setColour (ink);
    g.drawText ("MIMIC", 26, 0, 360, 62, juce::Justification::centredLeft);
    g.setColour (red);
    g.drawText ("MIMIC", 22, -3, 360, 62, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (font (look.typewriter, 12.0f));
    g.drawText ("it learns your sound. then it bites.", 24, 56, 300, 16, juce::Justification::centredLeft);

    paintChest (g);

    // BITE value under the lock plate.
    g.setColour (goldLight);
    g.setFont (font (look.bangers, 34.0f));
    g.drawText ("BITE " + valueText (ids::bite), 120, 506, 300, 36, juce::Justification::centred);

    // Speed and Mix panels.
    for (auto* k : { &speed, &mix })
    {
        const auto box = k->slider.getBounds().expanded (44, 8).withHeight (104);
        g.setColour (panel);
        g.fillRoundedRectangle (box.toFloat(), 8.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (box.toFloat(), 8.0f, 2.0f);
        g.setColour (goldLight);
        g.setFont (font (look.bangers, 19.0f));
        g.drawText (k == &speed ? "SPEED" : "MIX", box.getX(), k->slider.getBottom() + 2, box.getWidth(), 18, juce::Justification::centred);
        g.setColour (bone);
        g.setFont (font (look.typewriter, 12.0f));
        g.drawText (valueText (k == &speed ? ids::speed : ids::mix), box.getX(), k->slider.getBottom() + 18, box.getWidth(), 14, juce::Justification::centred);
    }

    g.setColour (dim);
    g.setFont (font (look.typewriter, 12.0f));
    g.drawText ("WHAT IT CHEWS", 560, 270, 316, 14, juce::Justification::centredLeft);

    // What happens next: the fill timeline, or the pattern groups.
    const bool fillMode = juce::roundToInt (value (ids::mode)) == ids::modeFill;
    const auto info = infoArea.toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (info, 8.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (info, 8.0f, 2.0f);
    g.setFont (font (look.typewriter, 13.0f));
    const auto inner = infoArea.reduced (14, 10);

    if (fillMode)
    {
        g.setColour (bone);
        g.drawText ("EVERY " + valueText (ids::fillEvery).toUpperCase(), inner.withHeight (18), juce::Justification::centredLeft);
        g.drawText (valueText (ids::fillLength).toUpperCase(), inner.withHeight (18), juce::Justification::centredRight);

        const int cells = 16;
        const int bars = ids::fillEveryBars[juce::jlimit (0, 5, juce::roundToInt (value (ids::fillEvery)))];
        const int where = juce::roundToInt (value (ids::fillLength));
        const float fillShare = (where == ids::lastBeat ? 0.25f : where == ids::lastHalfBar ? 0.5f : 1.0f) / (float) bars;
        const int fillCells = juce::jmax (1, juce::roundToInt (fillShare * cells));
        const float cellW = ((float) inner.getWidth() - 3.0f * (cells - 1)) / (float) cells;
        const bool inFill = mimicProcessor.inFillOut.load();

        for (int i = 0; i < cells; ++i)
        {
            const bool isFill = i >= cells - fillCells;
            g.setColour (isFill ? (inFill ? goldLight : redBright) : juce::Colour (0xff4a3424));
            g.fillRect ((float) inner.getX() + i * (cellW + 3.0f), (float) inner.getY() + 28.0f, cellW, 18.0f);
        }

        const int toFill = mimicProcessor.barsToFillOut.load();
        g.setColour (dim);
        g.setFont (font (look.typewriter, 12.0f));
        const juce::String upcoming = ! mimicProcessor.transportPlaying.load() ? juce::String ("press play (or C1 for a fill now)")
                                : inFill ? juce::String ("FILL!")
                                : toFill < 0 ? juce::String ("no fill this time")
                                : toFill == 0 ? juce::String ("fill this bar")
                                : "next fill in " + juce::String (toFill) + (toFill == 1 ? " bar" : " bars");
        g.drawText (upcoming + "  ·  chance " + valueText (ids::probability), inner.getX(), inner.getY() + 54, inner.getWidth(), 16, juce::Justification::centredLeft);
    }
    else
    {
        const int group = ids::variationBars[juce::jlimit (0, 3, juce::roundToInt (value (ids::variation)))];
        g.setColour (bone);
        g.drawText ("NEW PATTERN " + valueText (ids::variation).toUpperCase(), inner.withHeight (18), juce::Justification::centredLeft);
        const float cellW = ((float) inner.getWidth() - 6.0f * (group - 1)) / (float) group;

        for (int i = 0; i < group; ++i)
        {
            g.setColour (group > 1 && i == group - 1 ? redBright : juce::Colour (0xff8a5a2c));
            g.fillRect ((float) inner.getX() + i * (cellW + 6.0f), (float) inner.getY() + 28.0f, cellW, 18.0f);
        }

        g.setColour (dim);
        g.setFont (font (look.typewriter, 12.0f));
        g.drawText (group > 1 ? "same mash " + juce::String (group - 1) + (group == 2 ? " bar" : " bars") + ", wilder last bar"
                              : juce::String ("a new mash every bar"),
                    inner.getX(), inner.getY() + 54, inner.getWidth(), 16, juce::Justification::centredLeft);
    }

    // Footer: what it hears and what it does, and the teeth.
    g.setColour (ink);
    g.fillRect (0, 568, getWidth(), 52);
    g.setColour (edge);
    g.fillRect (0, 568, getWidth(), 2);

    const int heard = juce::jlimit (0, 2, mimicProcessor.materialOut.load());
    const int kind = juce::jlimit (0, (int) std::size (kindNames) - 1, mimicProcessor.kindOut.load());
    juce::AttributedString status;
    status.append ("HEARS: ", font (look.typewriter, 13.0f), bone);
    status.append (materialNames[heard], font (look.typewriter, 13.0f), goldLight);
    status.append ("   BITING: ", font (look.typewriter, 13.0f), bone);
    status.append (kindNames[kind], font (look.typewriter, 13.0f), kind == 0 ? dim : redBright);

    if (mimicProcessor.sidechainMissing.load())
        status.append ("   NO PREY: route a track to PREY IN", font (look.typewriter, 13.0f), redBright);

    status.draw (g, juce::Rectangle<float> (24.0f, 578.0f, 460.0f, 32.0f));

    for (int i = 0; i < 4; ++i)
    {
        const float level = juce::jlimit (0.0f, 1.0f, mimicProcessor.toothOut[i].load() * 3.0f);
        const float x = 470.0f + (float) i * 72.0f;
        juce::Path tooth;
        tooth.addTriangle (x, 588.0f, x + 12.0f, 588.0f, x + 6.0f, 602.0f);
        g.setColour (juce::Colour (0xff5a4a3a).interpolatedWith (bone, level));
        g.fillPath (tooth);
        g.setColour (level > 0.05f ? bone : dim);
        g.setFont (font (look.typewriter, 11.0f));
        g.drawText (toothNames[i], (int) x + 16, 586, 56, 16, juce::Justification::centredLeft);
    }
}

void MimicEditor::paintAdvanced (juce::Graphics& g)
{
    g.fillAll (ink.withAlpha (0.96f));

    auto section = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.toFloat(), 8.0f, 2.0f);
        g.setColour (goldLight);
        g.setFont (font (look.bangers, 22.0f));
        g.drawText (title, r.getX() + 12, r.getY() + 4, r.getWidth() - 24, 24, juce::Justification::centredLeft);
    };

    section (fillsArea, "FILLS");
    section (grooveArea, "GROOVE & ECHO");
    section (throatArea, "THE THROAT");
    section (preyArea, "THE PREY");
    section (teethArea, "TEETH");

    g.setColour (dim);
    g.setFont (font (look.typewriter, 11.0f));
    g.drawFittedText ("The teeth listen to the prey: Self is the track itself, Sidechain another track (PREY IN), MIDI the notes you play.",
                      teethArea.getX() + 14, teethArea.getBottom() - 40, teethArea.getWidth() - 28, 32, juce::Justification::centredLeft, 2);
}

void MimicEditor::resized()
{
    // Header.
    save.setBounds (806, 14, 70, 44);
    next.setBounds (756, 14, 44, 44);
    presetBox.setBounds (520, 14, 230, 44);
    previous.setBounds (470, 14, 44, 44);

    // Front page.
    bite.slider.setBounds (172, 308, 196, 196);
    wildButton.setBounds (560, 92, 158, 52);
    fillButton.setBounds (718, 92, 158, 52);
    speed.slider.setBounds (604, 164, 64, 64);
    mix.slider.setBounds (768, 164, 64, 64);

    for (int i = 0; i < numContent; ++i)
        contents[i].button.setBounds (560 + (i % 2) * 162, 288 + (i / 2) * 34, 154, 30);

    infoArea = { 560, 432, 316, 112 };
    advancedButton.setBounds (770, 576, 110, 36);

    // Advanced page, between the header and the footer.
    advanced.setBounds (0, 72, getWidth(), 496);
    fillsArea = { 16, 10, 430, 228 };
    grooveArea = { 456, 10, 428, 228 };
    throatArea = { 16, 248, 300, 238 };
    preyArea = { 326, 248, 200, 238 };
    teethArea = { 536, 248, 348, 238 };

    auto placeKnob = [] (Knob& k, int x, int y, int size)
    {
        k.label.setBounds (x - 16, y, size + 32, 16);
        k.slider.setBounds (x, y + 16, size, size + 16);
    };

    auto placePick = [] (Pick& p, int x, int y, int w)
    {
        p.label.setBounds (x, y, w, 16);
        p.box.setBounds (x, y + 18, w, 28);
    };

    placePick (every, 30, 44, 140);
    placePick (length, 30, 104, 140);
    placeKnob (probability, 200, 40, 56);
    placeKnob (anticipation, 278, 40, 56);
    placeKnob (lure, 356, 40, 56);
    replace.button.setBounds (30, 166, 110, 24);
    escalate.button.setBounds (150, 166, 110, 24);
    keep.button.setBounds (30, 196, 110, 24);
    reroll.setBounds (300, 170, 130, 40);

    placeKnob (swing, 476, 40, 56);
    placeKnob (breaks, 576, 40, 56);
    placeKnob (throwKnob, 676, 40, 56);
    placeKnob (output, 776, 40, 56);
    placePick (variation, 476, 150, 170);

    placePick (throatType, 30, 282, 140);
    placeKnob (cutoff, 34, 352, 56);
    placeKnob (resonance, 128, 352, 56);
    placeKnob (move, 222, 352, 56);

    placePick (prey, 340, 282, 170);
    placePick (material, 340, 338, 170);
    placeKnob (monoBass, 398, 392, 56);

    Knob* teeth[] { &slicer, &shaper, &filter, &pitcher };

    for (int i = 0; i < 4; ++i)
    {
        const int x = 556 + i * 82;
        placeKnob (*teeth[i], x, 280, 56);
        inverts[i].button.setBounds (x - 6, 370, 76, 22);
    }
}
