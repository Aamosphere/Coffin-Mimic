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
    const char* const kindNames[] { "-", "STUTTER", "ROLL", "REVERSE", "SCATTER", "TAPE", "GATE", "GRAINS", "BREAK", "MASH",
                                    "SHARDS", "BEND", "FOG", "ECHO" };
    const char* const shortKindNames[] { "", "STU", "ROLL", "REV", "SCAT", "TAPE", "GATE", "GRN", "", "MASH",
                                         "SHRD", "BEND", "FOG", "ECHO" };
    const juce::Colour kindColours[] {
        juce::Colour (0xff3a2a1c),   // pass
        juce::Colour (0xffe8b84a),   // stutter
        juce::Colour (0xffe0352a),   // roll
        juce::Colour (0xff9a5ac8),   // reverse
        juce::Colour (0xffe07a2a),   // scatter
        juce::Colour (0xff8a6a4a),   // tape
        juce::Colour (0xfff1e6bf),   // gate
        juce::Colour (0xff4ab0a0),   // grains
        juce::Colour (0xff000000),   // rest
        juce::Colour (0xffb06a2a),   // mash
        juce::Colour (0xfff0d878),   // shards (a brighter stutter)
        juce::Colour (0xffff6a50),   // bend (a brighter roll)
        juce::Colour (0xff7ad0c4),   // fog (a paler grain)
        juce::Colour (0xff5a8ae0)    // echo
    };
    const char* const materialNames[] { "PERCUSSIVE", "MELODIC", "HARMONIC", "VOICE" };

    // The teeth by what they do; flipped, they do the opposite.
    const char* const toothNames[] { "CHOP", "FOLLOW", "BRIGHT", "PITCH", "VOICE", "POSSESS" };
    const char* const flippedNames[] { "GAPS", "DUCK", "DARK", "MIRROR", "CARVE", "HAUNT" };
    const char* const invertIds[] { ids::slicerInvert, ids::shaperInvert, ids::filterInvert, ids::pitcherInvert,
                                    ids::voiceInvert, ids::possessInvert };
    const char* const toothHelp[] {
        "CHOP: every hit of the prey restarts the chewing, so the track takes on the prey's rhythm. Flipped (GAPS): the prey's silences do it. Needs Sidechain or MIDI.",
        "FOLLOW: the track's level follows the prey's loudness. Flipped (DUCK): the track ducks under the prey - a sidechain pump.",
        "BRIGHT: the prey's brightness opens a low-pass on the track. Flipped (DARK): a bright prey closes it.",
        "PITCH: MIDI notes pitch the track (C3 = unchanged). Flipped (MIRROR): the intervals turn upside down.",
        "VOICE: the track speaks with the prey's voice - a vocoder. Route a vocal (or anything) to PREY IN and the track takes on its words and rhythm. Flipped (CARVE): the track makes room wherever the prey is loud, band by band. Needs Sidechain.",
        "POSSESS: the prey plays the Mimic. Every hit of the prey fires a stutter, roll, reverse... (whatever it chews) right there. Works with Self too: the track's own hits. Flipped (HAUNT): the prey's silences fire them."
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

        // The teeth's narrow Flip switches get a smaller label.
        const bool narrow = b.getWidth() < 64;
        g.setColour (highlighted ? bone : dim);
        g.setFont (font (typewriter, narrow ? 11.0f : 13.0f));
        g.drawText (b.getButtonText(), narrow ? 21 : 24, 0, b.getWidth() - (narrow ? 21 : 24), b.getHeight(), juce::Justification::centredLeft);
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
    g.setFont (font (bangers, 16.0f));
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
    addAndMakeVisible (stage);
    stage.painter = [this] (juce::Graphics& g) { paintFront (g); };

    // Front page.
    bite.slider.setName ("bite");
    setUpKnob (stage, bite, ids::bite, {}, false);
    bite.slider.setTooltip ("How hard the Mimic bites: from a little variation to breakbeat carnage.");
    setUpKnob (stage, speed, ids::speed, "SPEED", false);
    speed.slider.setTooltip ("The grid it chews on: 1/4, 1/8, 1/16 or 1/32. The pattern below shows the grid.");
    setUpKnob (stage, mix, ids::mix, "MIX", false);

    for (int i = 0; i < numContent; ++i)
        setUpSwitch (stage, contents[i], ids::contentIds[i], ids::contentNames[i], true);

    contents[7].button.setTooltip ("Mash: moves pieces of the beat around, so the snare does not always land in the same place.");
    contents[0].button.setTooltip ("Stutter: a hit repeats. On a melody it may become SHARDS (the loop layered an octave up and down), on pads FOG.");
    contents[1].button.setTooltip ("Roll: a drummer's roll. On a melody or a voice it may become BEND, a loop gliding to a new pitch.");
    contents[6].button.setTooltip ("Grains: the track as a grain cloud. On pads it may become FOG, a drifting detuned haze.");
    contents[8].button.setTooltip ("Echo: a granular delay in dotted eighths. Melodies and pads shimmer an octave up, voices go backwards now and then.");

    for (auto* b : { &wildButton, &fillButton })
    {
        stage.addAndMakeVisible (*b);
        b->setClickingTogglesState (false);
    }

    wildButton.setTooltip ("Wild: the track is chewed all the time.");
    fillButton.setTooltip ("Fill: the track stays clean and the Mimic strikes at the end of each phrase.");

    // The mode panel.
    setUpPick (stage, variation, ids::variation, "NEW PATTERN");
    setUpPick (stage, loop, ids::loop, "LOOPED BAR");
    setUpPick (stage, every, ids::fillEvery, "EVERY");
    setUpPick (stage, length, ids::fillLength, "WHERE");
    setUpPick (stage, phraseStart, ids::phraseStart, "COUNT FROM");
    setUpKnob (stage, probability, ids::probability, {}, false);
    variation.box.setTooltip ("How long a pattern holds. The last bar of each group is a wilder variation.");
    loop.box.setTooltip ("When the DAW loops a bar: play the same pattern every pass, or a new one as if the song went on.");
    phraseStart.box.setTooltip ("The bar your song's phrases start from, so the fills land at the ends of its phrases.");
    probability.slider.setTooltip ("Chance: how often a phrase gets its fill.");

    modeAttachment = std::make_unique<juce::ParameterAttachment> (*p.parameters.getParameter (ids::mode), [this] (float v)
    {
        const bool fill = juce::roundToInt (v) == ids::modeFill;
        fillLayout = fill;
        wildButton.setToggleState (! fill, juce::dontSendNotification);
        fillButton.setToggleState (fill, juce::dontSendNotification);
        updateModeControls();
        repaint();
    });
    wildButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture ((float) ids::modeWild); };
    fillButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture ((float) ids::modeFill); };

    stage.addAndMakeVisible (advancedButton);
    advancedButton.getProperties().set ("style", "dashed");
    advancedButton.onClick = [this] { showAdvanced (! drawer.isVisible()); };

    // Presets.
    stage.addAndMakeVisible (presetBox);
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
        stage.addAndMakeVisible (*b);

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

    // The drawer.
    stage.addChildComponent (drawer);
    drawer.painter = [this] (juce::Graphics& g) { paintDrawer (g); };

    setUpKnob (drawer, swing, ids::swing, "SWING", true);
    setUpKnob (drawer, output, ids::output, "OUTPUT", true);
    setUpKnob (drawer, monoBass, ids::monoBass, "MONO BASS", true);
    // The groove column on the front page.
    setUpKnob (stage, breaks, ids::breaks, "BREAKS", false);
    setUpKnob (stage, throwKnob, ids::throwAmount, "THROW", false);
    setUpKnob (stage, dynamicsKnob, ids::dynamics, "DYNAMICS", false);
    breaks.slider.setTooltip ("Holes in the beat: silent eighths, mostly on the offbeats. Works at any Bite.");
    throwKnob.slider.setTooltip ("Now and then a hit is thrown into the reverb and rings on.");
    dynamicsKnob.slider.setTooltip ("Level differences: some events and repeats come out quieter, the ones on the beat stay strong - like a drummer's ghost notes and accents.");

    // Values painted on the front page follow their knobs at once.
    for (auto* k : { &speed, &mix, &breaks, &throwKnob, &dynamicsKnob })
        k->slider.onValueChange = [this] { stage.repaint (0, 72, contentWidth, 496); };
    monoBass.slider.setTooltip ("Keeps the lows mono below this frequency.");

    setUpPick (drawer, throatType, ids::filterType, "TYPE");
    setUpKnob (drawer, cutoff, ids::cutoff, "CUTOFF", true);
    setUpKnob (drawer, resonance, ids::resonance, "RESO", true);
    setUpKnob (drawer, move, ids::filterMove, "MOVE", true);
    setUpKnob (drawer, glide, ids::filterGlide, "GLIDE", true);
    move.slider.setTooltip ("How far the cutoff travels: a new place on every grid step, and a sweep through each fill.");
    glide.slider.setTooltip ("Steps: the cutoff jumps to each new place. Up: it slides there, all the way at 100 %.");

    setUpKnob (drawer, anticipation, ids::anticipation, "RISER", true);
    setUpKnob (drawer, lure, ids::lure, "LURE", true);
    setUpSwitch (drawer, replace, ids::replace, "Replace", false);
    setUpSwitch (drawer, escalate, ids::escalate, "Escalate", false);
    setUpSwitch (drawer, keep, ids::keep, "Keep", false);
    drawer.addAndMakeVisible (reroll);
    reroll.onClick = [this] { mimicProcessor.reroll(); };
    reroll.setTooltip ("A new variation of the same preset.");
    anticipation.slider.setTooltip ("A high-pass that rises into each fill.");
    lure.slider.setTooltip ("A little imitation between fills too.");
    replace.button.setTooltip ("On: the fill replaces the track. Off: the fill is laid on top.");
    escalate.button.setTooltip ("Each fill in a row bites harder.");
    keep.button.setTooltip ("Lock the current variation: it repeats exactly.");

    setUpPick (drawer, prey, ids::prey, "LISTENS TO");
    setUpPick (drawer, material, ids::material, "MATERIAL");
    prey.box.setTooltip ("What the teeth listen to. Self: the track itself. Sidechain: another track routed to PREY IN. MIDI: the notes you play.");
    material.box.setTooltip ("What the Mimic thinks the track is. Auto listens; the footer shows what it hears.");

    Knob* teeth[] { &slicer, &shaper, &filter, &pitcher, &voiceTooth, &possess };
    const char* teethIds[] { ids::slicer, ids::shaper, ids::filter, ids::pitcher, ids::voice, ids::possess };

    for (int i = 0; i < numTeeth; ++i)
    {
        setUpKnob (drawer, *teeth[i], teethIds[i], toothNames[i], true);
        teeth[i]->slider.setTooltip (toothHelp[i]);
        setUpSwitch (drawer, inverts[i], invertIds[i], "Flip", false);
        inverts[i].button.setTooltip (toothHelp[i]);
        inverts[i].button.onStateChange = [this] { updateTeethNames(); };
    }

    prey.box.onChange = [this] { updateTeethNames(); repaint(); };

    refreshPresets();
    modeAttachment->sendInitialUpdate();
    updateTeethNames();
    drawer.setVisible (mimicProcessor.advancedOpen);
    advancedButton.setButtonText (mimicProcessor.advancedOpen ? "HIDE" : "ADVANCED");
    setSize (spineWidth + contentWidth, frontHeight + (mimicProcessor.advancedOpen ? drawerHeight : 0));
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
    k.label.setFont (font (look.bangers, 16.0f));
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
    p.label.setFont (font (look.bangers, 16.0f));
    p.label.setBorderSize ({});
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
    mimicProcessor.advancedOpen = show;
    drawer.setVisible (show);
    advancedButton.setButtonText (show ? "HIDE" : "ADVANCED");
    setSize (spineWidth + contentWidth, frontHeight + (show ? drawerHeight : 0));
    repaint();
}

void MimicEditor::updateModeControls()
{
    // The parameter's raw value may not have caught up yet: use the
    // attachment's.
    const bool fill = fillLayout;

    for (auto* pick : { &variation, &loop })
    {
        pick->box.setVisible (! fill);
        pick->label.setVisible (! fill);
    }

    for (auto* pick : { &every, &length, &phraseStart })
    {
        pick->box.setVisible (fill);
        pick->label.setVisible (fill);
    }

    probability.slider.setVisible (fill);

    // Riser, Lure, Replace and Escalate only matter between and in fills.
    for (juce::Component* c : { (juce::Component*) &anticipation.slider, (juce::Component*) &lure.slider,
                                (juce::Component*) &replace.button, (juce::Component*) &escalate.button })
        c->setAlpha (fill ? 1.0f : 0.45f);

    resized();
    drawer.repaint();
}

void MimicEditor::updateTeethNames()
{
    Knob* teeth[] { &slicer, &shaper, &filter, &pitcher, &voiceTooth, &possess };
    const int source = prey.box.getSelectedItemIndex();

    for (int i = 0; i < numTeeth; ++i)
    {
        teeth[i]->label.setText (inverts[i].button.getToggleState() ? flippedNames[i] : toothNames[i], juce::dontSendNotification);

        // CHOP needs a prey of its own; PITCH needs MIDI notes; VOICE
        // needs audio at PREY IN.
        const bool works = (i == 0 && source == ids::preySelf) ? false
                         : (i == 3 && source != ids::preyMidi) ? false
                         : (i == 4 && source != ids::preySidechain) ? false : true;
        teeth[i]->slider.setAlpha (works ? 1.0f : 0.35f);
        teeth[i]->label.setAlpha (works ? 1.0f : 0.35f);
        inverts[i].button.setAlpha (works ? 1.0f : 0.35f);
    }

    drawer.repaint();
}

bool MimicEditor::fillMode() const
{
    return fillLayout;
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

        updateTeethNames();
    }

    // The lid follows Bite and opens wider during a fill; the eyes light up
    // while it chews.
    const float b = value (ids::bite);
    const bool inFill = mimicProcessor.inFillOut.load();
    const bool chewing = mimicProcessor.kindOut.load() != (int) Kind::pass;
    const float target = fillMode() ? (inFill ? juce::jmin (1.25f, b + 0.35f) : 0.08f + 0.25f * b) : b;
    lidOpen += 0.3f * (target - lidOpen);
    eyeGlow += 0.25f * ((chewing ? 1.0f : juce::jlimit (0.0f, 1.0f, (lidOpen - 0.2f) / 0.4f)) - eyeGlow);

    stage.repaint (0, 72, 560, 496);
    stage.repaint (modeArea);
    stage.repaint (0, 568, stage.getWidth(), 52);

    if (drawer.isVisible())
        drawer.repaint (preyMeter);
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

// The script playing now (or the last fill): one coloured block per
// segment on the grid Speed sets, with the playhead.
void MimicEditor::paintPattern (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (ink);
    g.fillRoundedRectangle (r, 4.0f);

    const int count = mimicProcessor.shownCount.load (std::memory_order_acquire);
    const float span = juce::jmax (0.25f, mimicProcessor.shownSpan.load());
    const float position = mimicProcessor.shownPosition.load();
    const bool live = position >= 0.0f && mimicProcessor.transportPlaying.load();
    const auto inner = r.reduced (3.0f);
    const float pxPerPpq = inner.getWidth() / span;

    for (int i = 0; i < count; ++i)
    {
        const auto packed = mimicProcessor.shownSegments[(size_t) i].load (std::memory_order_relaxed);
        const float start = (float) (packed & 0xfff) / 96.0f, len = (float) ((packed >> 12) & 0xfff) / 96.0f;
        const int kind = juce::jlimit (0, (int) std::size (kindColours) - 1, (int) (packed >> 24));

        if (kind == (int) Kind::pass)
            continue;

        auto block = juce::Rectangle<float> (inner.getX() + start * pxPerPpq, inner.getY(), len * pxPerPpq, inner.getHeight()).reduced (0.5f, 0.0f);
        const bool now = live && position >= start && position < start + len;

        if (kind == (int) Kind::rest)
        {
            g.setColour (juce::Colours::black);
            g.fillRect (block);
            g.setColour (redBright.withAlpha (0.8f));
            g.drawLine (block.getX() + 2, block.getY() + 3, block.getRight() - 2, block.getBottom() - 3, 1.5f);
            g.drawLine (block.getX() + 2, block.getBottom() - 3, block.getRight() - 2, block.getY() + 3, 1.5f);
            continue;
        }

        g.setColour (kindColours[kind].withAlpha (live ? (now ? 1.0f : 0.75f) : 0.4f));
        g.fillRect (block);

        if (block.getWidth() > 26.0f && shortKindNames[kind][0] != 0)
        {
            g.setColour (ink);
            g.setFont (font (look.typewriter, 10.0f));
            g.drawText (shortKindNames[kind], block.reduced (2.0f, 0.0f), juce::Justification::centredLeft);
        }
    }

    // The grid Speed sets; beats brighter.
    const float step = (float) std::exp2 (-(double) juce::jlimit (0, 3, (int) (value (ids::speed) * 3.999f)));

    for (float x = 0.0f; x < span - 1.0e-3f; x += step)
    {
        const bool beat = std::abs (x - std::round (x)) < 1.0e-3f;
        g.setColour (bone.withAlpha (beat ? 0.55f : 0.18f));
        g.fillRect (inner.getX() + x * pxPerPpq, beat ? inner.getY() : inner.getBottom() - 8.0f, 1.0f, beat ? inner.getHeight() : 8.0f);
    }

    if (live)
    {
        g.setColour (redBright);
        g.fillRect (inner.getX() + juce::jlimit (0.0f, span, position) * pxPerPpq - 1.0f, r.getY(), 2.0f, r.getHeight());
    }
}

void MimicEditor::paintModePanel (juce::Graphics& g)
{
    const auto area = modeArea.toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (area, 8.0f, 2.0f);
    const auto x = modeArea.getX() + 14;
    const auto w = modeArea.getWidth() - 28;

    if (fillMode())
    {
        // The phrase: clean bars, then the fill.
        const int bars = ids::fillEveryBars[juce::jlimit (0, 5, juce::roundToInt (value (ids::fillEvery)))];
        const int where = juce::roundToInt (value (ids::fillLength));
        const float fillShare = (where == ids::lastBeat ? 0.25f : where == ids::lastHalfBar ? 0.5f : 1.0f) / (float) bars;
        const int cells = 16;
        const int fillCells = juce::jmax (1, juce::roundToInt (fillShare * cells));
        const auto strip = juce::Rectangle<float> ((float) x, (float) patternArea.getY(), (float) patternArea.getWidth(), 14.0f);
        const float cellW = (strip.getWidth() - 2.0f * (cells - 1)) / (float) cells;
        const bool inFill = mimicProcessor.inFillOut.load();

        for (int i = 0; i < cells; ++i)
        {
            const bool isFill = i >= cells - fillCells;
            g.setColour (isFill ? (inFill ? goldLight : redBright) : juce::Colour (0xff4a3424));
            g.fillRect (strip.getX() + i * (cellW + 2.0f), strip.getY(), cellW, strip.getHeight());
        }

        paintPattern (g, patternArea.toFloat().withTrimmedTop (20.0f));

        g.setColour (goldLight);
        g.setFont (font (look.bangers, 15.0f));
        g.drawText ("CHANCE " + valueText (ids::probability), probability.slider.getX() - 14, probability.slider.getBottom(), probability.slider.getWidth() + 28, 16, juce::Justification::centred);

        const int toFill = mimicProcessor.barsToFillOut.load();
        g.setColour (dim);
        g.setFont (font (look.typewriter, 12.0f));
        const juce::String upcoming = ! mimicProcessor.transportPlaying.load() ? juce::String ("press play, or play C1 for a fill now")
                                : inFill ? juce::String ("FILL!")
                                : toFill < 0 ? juce::String ("no fill this phrase")
                                : toFill == 0 ? juce::String ("fill this bar")
                                : "next fill in " + juce::String (toFill) + (toFill == 1 ? " bar" : " bars");
        g.drawText (upcoming, x, modeArea.getBottom() - 26, w, 16, juce::Justification::centredLeft);
    }
    else
    {
        paintPattern (g, patternArea.toFloat());

        // The pattern group: the same pattern, then a wilder last bar.
        const int group = ids::variationBars[juce::jlimit (0, 3, juce::roundToInt (value (ids::variation)))];
        const int phraseBar = mimicProcessor.shownPhraseBar.load();
        const int barInGroup = ((phraseBar % group) + group) % group;
        const float cellW = ((float) w - 4.0f * (group - 1)) / (float) group;
        const float y = (float) patternArea.getBottom() + 6.0f;

        for (int i = 0; i < group; ++i)
        {
            const bool now = mimicProcessor.transportPlaying.load() && i == barInGroup;
            g.setColour ((group > 1 && i == group - 1 ? redBright : juce::Colour (0xff8a5a2c)).withAlpha (now ? 1.0f : 0.45f));
            g.fillRect ((float) x + i * (cellW + 4.0f), y, cellW, 8.0f);
        }

        g.setColour (dim);
        g.setFont (font (look.typewriter, 12.0f));
        g.drawText (group > 1 ? "same pattern " + juce::String (group - 1) + (group == 2 ? " bar" : " bars") + ", then a wilder bar"
                              : juce::String ("a new pattern every bar"),
                    x, modeArea.getBottom() - 26, w, 16, juce::Justification::centredLeft);
    }
}

void MimicEditor::paint (juce::Graphics& g)
{
    // The spine: the maker's name down the left edge, the same on every
    // Aamosphere plugin.
    const auto spine = juce::Rectangle<float> (0.0f, 0.0f, (float) spineWidth, (float) getHeight());
    g.setColour (gold);
    g.fillRect (spine);
    g.setColour (ink);
    g.fillRect (spine.withX ((float) spineWidth - 4.0f).withWidth (4.0f));

    const juce::String maker ("AAMOSPHERE");
    const float letterHeight = 40.0f, height = (float) frontHeight;
    const float gap = (height - 16.0f - letterHeight * (float) maker.length()) / (float) (maker.length() + 1);
    g.setFont (font (look.bangers, letterHeight));

    for (int i = 0; i < maker.length(); ++i)
        g.drawText (juce::String::charToString (maker[i]),
                    juce::Rectangle<float> (0.0f, 8.0f + gap * (float) (i + 1) + letterHeight * (float) i, (float) spineWidth - 4.0f, letterHeight),
                    juce::Justification::centred);
}

void MimicEditor::paintFront (juce::Graphics& g)
{
    // Dungeon dark with a torch glow on the chest.
    juce::ColourGradient glow (juce::Colour (0xff3a1f12), 270.0f, 280.0f, juce::Colour (0xff0b0605), 920.0f, 280.0f, true);
    glow.addColour (0.55, juce::Colour (0xff160c08));
    g.setGradientFill (glow);
    g.fillRect (0, 0, stage.getWidth(), frontHeight);

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
        const auto box = k->slider.getBounds().expanded (44, 8).withHeight (100);
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

    // The groove column: Breaks, Throw and Dynamics.
    g.setColour (panel);
    g.fillRoundedRectangle (grooveColumn.toFloat(), 8.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (grooveColumn.toFloat(), 8.0f, 2.0f);

    for (auto [k, name, id] : { std::tuple<Knob*, const char*, const char*> { &breaks, "BREAKS", ids::breaks },
                                { &throwKnob, "THROW", ids::throwAmount }, { &dynamicsKnob, "DYNAMICS", ids::dynamics } })
    {
        g.setColour (goldLight);
        g.setFont (font (look.bangers, 19.0f));
        g.drawText (name, grooveColumn.getX(), k->slider.getBottom() + 2, grooveColumn.getWidth(), 18, juce::Justification::centred);
        g.setColour (bone);
        g.setFont (font (look.typewriter, 12.0f));
        g.drawText (valueText (id), grooveColumn.getX(), k->slider.getBottom() + 18, grooveColumn.getWidth(), 14, juce::Justification::centred);
    }

    g.setColour (dim);
    g.setFont (font (look.typewriter, 12.0f));
    g.drawText ("WHAT IT CHEWS", chewsArea.getX(), chewsArea.getY() - 16, 316, 14, juce::Justification::centredLeft);

    paintModePanel (g);

    // Footer: what it hears and what it does, and the teeth.
    g.setColour (ink);
    g.fillRect (0, 568, stage.getWidth(), 52);
    g.setColour (edge);
    g.fillRect (0, 568, stage.getWidth(), 2);

    const int setTo = juce::roundToInt (value (ids::material));
    const int heard = setTo == ids::materialAuto ? juce::jlimit (0, 3, mimicProcessor.materialOut.load()) : juce::jlimit (0, 3, setTo - 1);
    const int kind = juce::jlimit (0, (int) std::size (kindNames) - 1, mimicProcessor.kindOut.load());
    juce::AttributedString status;
    status.append (setTo == ids::materialAuto ? "HEARS: " : "SET TO: ", font (look.typewriter, 13.0f), bone);
    status.append (materialNames[heard], font (look.typewriter, 13.0f), goldLight);
    status.append ("   BITING: ", font (look.typewriter, 13.0f), bone);
    status.append (kindNames[kind], font (look.typewriter, 13.0f), kind == 0 ? dim : redBright);

    if (mimicProcessor.sidechainMissing.load())
        status.append ("   NO PREY: route a track to PREY IN", font (look.typewriter, 13.0f), redBright);

    status.draw (g, juce::Rectangle<float> (24.0f, 578.0f, 550.0f, 32.0f));

    for (int i = 0; i < numTeeth; ++i)
    {
        const float level = juce::jlimit (0.0f, 1.0f, mimicProcessor.toothOut[i].load() * 3.0f);
        const float x = 586.0f + (float) (i % 3) * 96.0f;
        const float y = 577.0f + (float) (i / 3) * 20.0f;
        juce::Path tooth;
        tooth.addTriangle (x, y + 2.0f, x + 11.0f, y + 2.0f, x + 5.5f, y + 14.0f);
        g.setColour (juce::Colour (0xff5a4a3a).interpolatedWith (bone, level));
        g.fillPath (tooth);
        g.setColour (level > 0.05f ? bone : dim);
        g.setFont (font (look.typewriter, 11.0f));
        g.drawText (value (invertIds[i]) > 0.5f ? flippedNames[i] : toothNames[i], (int) x + 15, (int) y, 76, 16, juce::Justification::centredLeft);
    }
}

void MimicEditor::paintDrawer (juce::Graphics& g)
{
    g.fillAll (ink);
    g.setColour (edge);
    g.fillRect (0, 0, drawer.getWidth(), 2);

    auto section = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.toFloat(), 8.0f, 2.0f);
        g.setColour (goldLight);
        g.setFont (font (look.bangers, 20.0f));
        g.drawText (title, r.getX() + 12, r.getY() + 4, r.getWidth() - 24, 22, juce::Justification::centredLeft);
    };

    section (grooveArea, "GROOVE");
    section (throatArea, "THE THROAT");
    section (fillsArea, fillMode() ? "FILLS" : "FILLS (FILL MODE)");
    section (preyArea, "THE PREY & ITS TEETH");

    // Throat help beside the type.
    g.setColour (dim);
    g.setFont (font (look.typewriter, 11.0f));
    g.drawFittedText ("A resonant filter that moves with the grid.",
                      throatArea.getX() + 124, throatArea.getY() + 34, throatArea.getWidth() - 132, 50, juce::Justification::centredLeft, 4);

    // What the prey is doing.
    const int source = juce::roundToInt (value (ids::prey));
    const juce::String hint = source == ids::preySelf ? "the track itself"
                            : source == ids::preySidechain ? (mimicProcessor.sidechainMissing.load() ? "nothing yet: route a track to PREY IN" : "the track at PREY IN")
                                                           : "your MIDI notes";
    g.setColour (dim);
    g.drawText ("hears " + hint, preyArea.getX() + 12, preyMeter.getY() - 16, preyArea.getWidth() - 24, 14, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff4a3424));
    g.fillRoundedRectangle (preyMeter.toFloat(), 3.0f);
    g.setColour (redBright);
    g.fillRoundedRectangle (preyMeter.toFloat().withWidth ((float) preyMeter.getWidth() * juce::jlimit (0.0f, 1.0f, mimicProcessor.preyOut.load())), 3.0f);
}

void MimicEditor::resized()
{
    stage.setBounds (spineWidth, 0, contentWidth, getHeight());

    // Header.
    save.setBounds (916, 14, 70, 44);
    next.setBounds (866, 14, 44, 44);
    presetBox.setBounds (630, 14, 230, 44);
    previous.setBounds (580, 14, 44, 44);

    // Front page: the chest and Bite, the groove column, then the rest.
    bite.slider.setBounds (172, 308, 196, 196);

    grooveColumn = { 562, 80, 100, 480 };

    for (auto [k, y] : { std::pair<Knob*, int> { &breaks, 96 }, { &throwKnob, 256 }, { &dynamicsKnob, 416 } })
        k->slider.setBounds (grooveColumn.getX() + 12, y, 76, 76);

    wildButton.setBounds (670, 88, 158, 50);
    fillButton.setBounds (828, 88, 158, 50);
    speed.slider.setBounds (714, 156, 60, 60);
    mix.slider.setBounds (878, 156, 60, 60);

    chewsArea = { 670, 284, 316, 64 };

    // Nine switches: five on top, four below.
    for (int i = 0; i < numContent; ++i)
        contents[i].button.setBounds (chewsArea.getX() + (i % 5) * 63 + (i >= 5 ? 32 : 0), chewsArea.getY() + (i / 5) * 34, 60, 30);

    modeArea = { 670, 360, 316, 200 };
    const int mx = modeArea.getX() + 14, my = modeArea.getY() + 10;

    auto placePick = [] (Pick& p, int x, int y, int w)
    {
        p.label.setBounds (x, y, w, 16);
        p.box.setBounds (x, y + 18, w, 26);
    };

    placePick (variation, mx, my, 150);
    placePick (loop, mx + 158, my, 130);
    placePick (every, mx, my, 86);
    placePick (length, mx + 92, my, 116);
    placePick (phraseStart, mx + 214, my, 74);
    probability.slider.setBounds (mx + 236, my + 56, 52, 52);

    patternArea = fillMode() ? juce::Rectangle<int> (mx, my + 58, 220, 70)
                             : juce::Rectangle<int> (mx, my + 58, 288, 64);

    advancedButton.setBounds (880, 576, 110, 36);

    // The drawer, below the front page.
    drawer.setBounds (0, frontHeight, stage.getWidth(), drawerHeight);
    grooveArea = { 8, 10, 150, 218 };
    throatArea = { 164, 10, 250, 218 };
    fillsArea = { 420, 10, 170, 218 };
    preyArea = { 596, 10, 406, 218 };

    auto placeKnob = [] (Knob& k, int x, int y, int size)
    {
        k.label.setBounds (x - 14, y, size + 28, 16);
        k.slider.setBounds (x, y + 16, size, size + 16);
    };

    placeKnob (swing, 61, 38, 44);
    placeKnob (output, 26, 128, 44);
    placeKnob (monoBass, 94, 128, 44);

    placePick (throatType, 176, 38, 110);
    placeKnob (cutoff, 178, 128, 42);
    placeKnob (resonance, 236, 128, 42);
    placeKnob (move, 294, 128, 42);
    placeKnob (glide, 352, 128, 42);

    placeKnob (anticipation, 440, 38, 44);
    placeKnob (lure, 520, 38, 44);
    replace.button.setBounds (432, 124, 150, 22);
    escalate.button.setBounds (432, 148, 150, 22);
    keep.button.setBounds (432, 172, 150, 22);
    reroll.setBounds (432, 196, 150, 26);

    placePick (prey, 610, 36, 184);
    placePick (material, 802, 36, 188);
    preyMeter = { 610, 104, 380, 6 };

    Knob* teeth[] { &slicer, &shaper, &filter, &pitcher, &voiceTooth, &possess };

    for (int i = 0; i < numTeeth; ++i)
    {
        const int x = 618 + i * 64;
        placeKnob (*teeth[i], x, 116, 44);
        inverts[i].button.setBounds (x - 4, 192, 56, 20);
    }
}
