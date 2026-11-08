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
    // What the track is, as a player would say it.
    const char* const materialNames[] { "DRUMS", "A LINE (MELODY, BASS)", "CHORDS & PADS", "A VOICE" };
    const char* const materialShort[] { "DRUMS", "A LINE", "CHORDS", "A VOICE" };

    // One line per trick, in the order of mimic::Trick.
    const char* const trickHelp[] {
        "PARROT: says the phrase back, on wobbly tape. With Mockery up it sometimes squeaks or growls it.",
        "GHOST: the phrase an octave away (or two, or a fifth), layered and shimmering. On drums: tape speed, slow and heavy or quick and small.",
        "CLOUD: the phrase dissolves into a drifting swarm of grains, some backwards, some an octave up.",
        "BACKWARDS: the phrase reversed, swelling into the cave and landing on the grid.",
        "DUB: the last note thrown into a tape echo that spirals: darker, dirtier, and with Mockery up the tape slows down under it.",
        "MOCK: the last syllable stuttered back at you, bouncing left and right - with Mockery up, rising or falling like a laugh.",
        "HUM: the last note frozen into a drone that lingers under your next phrase and slowly darkens.",
        "RADIO: the phrase from a broken radio in the next room: thin, crushed, warbling."
    };
    const char* const trickWords[] {
        "says it back, on tape", "an octave away, layered", "dissolved into grains", "reversed, swelling",
        "thrown into a spiralling echo", "stuttered back, laughing", "frozen into a drone", "from a broken radio"
    };
    const juce::Colour trickColours[] {
        juce::Colour (0xffe8b84a), juce::Colour (0xff9ad0ff), juce::Colour (0xff4ab0a0), juce::Colour (0xff9a5ac8),
        juce::Colour (0xff5a8ae0), juce::Colour (0xffe0352a), juce::Colour (0xffc8a070), juce::Colour (0xffb0b0a0)
    };

    // Where the chest is drawn; its drawing is in a 500 x 450 box, scaled.
    constexpr float chestX = 26.0f, chestY = 100.0f, chestScale = 0.56f;

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

    if (slider.getName() == "big")
    {
        // The answer knobs: a heavy bronze ring around a red arc.
        const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 3.0f;
        g.setColour (ink.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (2 * R + 4, 2 * R + 4).withCentre (c.translated (0, 4)));
        g.setColour (gold);
        g.fillEllipse (juce::Rectangle<float> (2 * R, 2 * R).withCentre (c));
        g.setColour (ink);
        g.drawEllipse (juce::Rectangle<float> (2 * R, 2 * R).withCentre (c), 3.5f);
        g.setColour (panel);
        g.fillEllipse (juce::Rectangle<float> (2 * (R - 9), 2 * (R - 9)).withCentre (c));
        arc (g, c, R - 18, start, end, 7.0f, edge);
        arc (g, c, R - 18, start, juce::jmax (start + 0.01f, angle), 7.0f, redBright);
        g.setColour (juce::Colour (0xff2a1a10));
        g.fillEllipse (juce::Rectangle<float> (2 * (R - 28), 2 * (R - 28)).withCentre (c));
        g.setColour (bone);
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (R - 46, angle), c.getPointOnCircumference (R - 30, angle)), 4.0f);
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

    // Chips: gold when on; red-rimmed while that one is playing. A chip can
    // carry a small second line (what the switch is called when the track's
    // material turns it into its own version).
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const bool lit = (bool) b.getProperties()["lit"];
    const juce::String sub = b.getProperties()["sub"].toString();
    g.setColour (on ? gold : panel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (lit ? redBright : (on ? goldLight : (highlighted ? gold : edge.brighter (0.3f))));
    g.drawRoundedRectangle (r, 5.0f, lit ? 3.0f : 1.5f);
    g.setColour (on ? ink : dim);

    if (sub.isNotEmpty())
    {
        g.setFont (font (bangers, juce::jmin (16.0f, r.getHeight() * 0.5f)));
        g.drawText (b.getButtonText().toUpperCase(), r.withTrimmedBottom (r.getHeight() * 0.36f).translated (0.0f, 2.0f), juce::Justification::centred);
        g.setFont (font (typewriter, 9.5f));
        g.drawText (sub, r.withTrimmedTop (r.getHeight() * 0.6f).translated (0.0f, -2.0f), juce::Justification::centred);
    }
    else
    {
        g.setFont (font (bangers, juce::jmin (16.0f, r.getHeight() * 0.56f)));
        g.drawText (b.getButtonText().toUpperCase(), r, juce::Justification::centred);
    }
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

    // IT LISTENS: what the track is.
    setUpPick (stage, material, ids::material, "YOUR TRACK IS");
    material.box.setTooltip ("What the track this Mimic sits on is: drums, a line (a melody, a riff, a bass), chords and pads, or a voice. "
                             "Auto listens and decides. It changes what the Mimic does: a voice gets ghosts and backmasking, "
                             "a guitar gets octaves and echoes, drums get mocking stutters and tape tricks.");

    // IT ANSWERS.
    hunger.slider.setName ("big");
    mockery.slider.setName ("big");
    setUpKnob (stage, hunger, ids::hunger, {}, false);
    setUpKnob (stage, mockery, ids::mockery, {}, false);
    setUpKnob (stage, space, ids::space, {}, false);
    setUpKnob (stage, heat, ids::heat, {}, false);
    setUpKnob (stage, answerLevel, ids::answer, {}, false);
    hunger.slider.setTooltip ("How hungry the Mimic is: low, it waits for long silences and answers now and then; high, it jumps into every little gap.");
    mockery.slider.setTooltip ("How far it bends your phrase: low, a faithful echo; high, octaves down, laughing stutters and broken radios.");
    space.slider.setTooltip ("The cave the answers live in: its reverb and the tape echoes. Up: bigger, longer, wetter.");
    heat.slider.setTooltip ("Saturation on the answers: warm tape at first, burning at the top.");
    answerLevel.slider.setTooltip ("How loud the answers are. At 0 the Mimic keeps quiet.");

    for (int t = 0; t < numTricks; ++t)
    {
        setUpSwitch (stage, tricks[t], ids::trickIds[t], ids::trickNames[t], true);
        tricks[t].button.setTooltip (trickHelp[t]);
    }

    // SNARES.
    setUpSwitch (stage, snaresOn, ids::snares, "OFF", true);
    snaresOn.button.setTooltip ("Snares: the beat masher. Off, the track passes clean (the answers still come). On, it chops, rolls and breaks the track - mild to wild.");
    snaresOn.button.onStateChange = [this] { updateSnaresLook(); };

    bite.slider.setName ("bite");
    setUpKnob (stage, bite, ids::bite, {}, false);
    bite.slider.setTooltip ("Mild to wild: from a little variation to breakbeat carnage.");
    setUpKnob (stage, speed, ids::speed, {}, false);
    speed.slider.setTooltip ("The grid it chops on: 1/4, 1/8, 1/16 or 1/32.");
    setUpKnob (stage, breaks, ids::breaks, {}, false);
    setUpKnob (stage, throwKnob, ids::throwAmount, {}, false);
    setUpKnob (stage, dynamicsKnob, ids::dynamics, {}, false);
    breaks.slider.setTooltip ("Holes in the beat: silent eighths, mostly on the offbeats.");
    throwKnob.slider.setTooltip ("Now and then a hit is thrown into the reverb and rings on.");
    dynamicsKnob.slider.setTooltip ("Level differences: some repeats come out quieter, the ones on the beat stay strong - ghost notes and accents.");

    for (int i = 0; i < numContent; ++i)
        setUpSwitch (stage, contents[i], ids::contentIds[i], ids::contentNames[i], true);

    contents[0].button.setTooltip ("Stutter: a hit repeats. On a melody it becomes SHARDS (the loop layered an octave up and down), on chords FOG.");
    contents[1].button.setTooltip ("Roll: a drummer's roll. On a melody or a voice it becomes BEND, a loop gliding to a new pitch.");
    contents[2].button.setTooltip ("Reverse: a stretch of the track backwards.");
    contents[3].button.setTooltip ("Scatter: pieces from the last bars, in a new order.");
    contents[4].button.setTooltip ("Tape: the tape stops, or speeds up.");
    contents[5].button.setTooltip ("Gate: the track chopped into a rhythm.");
    contents[6].button.setTooltip ("Grains: the track as a grain cloud. On chords it becomes FOG, a drifting detuned haze.");
    contents[7].button.setTooltip ("Mash: moves pieces of the beat around, so the snare does not always land in the same place.");
    contents[8].button.setTooltip ("Echo: a granular delay in dotted eighths.");

    for (auto* b : { &wildButton, &fillButton })
    {
        stage.addAndMakeVisible (*b);
        b->setClickingTogglesState (false);
    }

    wildButton.setTooltip ("Wild: chopped all the time.");
    fillButton.setTooltip ("Fill: clean, and chopped at the end of each phrase.");

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

    // Values painted on the front page follow their knobs at once.
    for (auto* k : { &hunger, &mockery, &space, &heat, &answerLevel, &bite, &speed, &breaks, &throwKnob, &dynamicsKnob })
        k->slider.onValueChange = [this] { stage.repaint(); };

    // Header.
    stage.addAndMakeVisible (advancedButton);
    advancedButton.getProperties().set ("style", "dashed");
    advancedButton.onClick = [this] { showAdvanced (! drawer.isVisible()); };

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

    // The drawer: the rest of Snares, and the output.
    stage.addChildComponent (drawer);
    drawer.painter = [this] (juce::Graphics& g) { paintDrawer (g); };

    setUpPick (drawer, variation, ids::variation, "NEW PATTERN");
    setUpPick (drawer, loop, ids::loop, "LOOPED BAR");
    setUpPick (drawer, every, ids::fillEvery, "EVERY");
    setUpPick (drawer, length, ids::fillLength, "WHERE");
    setUpPick (drawer, phraseStart, ids::phraseStart, "COUNT FROM");
    setUpKnob (drawer, probability, ids::probability, "CHANCE", true);
    variation.box.setTooltip ("How long a pattern holds. The last bar of each group is a wilder variation.");
    loop.box.setTooltip ("When the DAW loops a bar: play the same pattern every pass, or a new one as if the song went on.");
    phraseStart.box.setTooltip ("The bar your song's phrases start from, so the fills land at the ends of its phrases.");
    probability.slider.setTooltip ("How often a phrase gets its fill.");

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
    reroll.setTooltip ("A new variation of the same preset (Snares and the answers both).");
    anticipation.slider.setTooltip ("A high-pass that rises into each fill.");
    lure.slider.setTooltip ("A little chopping between fills too.");
    replace.button.setTooltip ("On: the fill replaces the track. Off: the fill is laid on top.");
    escalate.button.setTooltip ("Each fill in a row bites harder.");
    keep.button.setTooltip ("Lock the current variation: it repeats exactly.");

    setUpKnob (drawer, swing, ids::swing, "SWING", true);
    setUpKnob (drawer, mix, ids::mix, "SNARES MIX", true);
    setUpKnob (drawer, output, ids::output, "OUTPUT", true);
    setUpKnob (drawer, monoBass, ids::monoBass, "MONO BASS", true);
    mix.slider.setTooltip ("Wild mode: how much of the chopped track against the clean one.");
    monoBass.slider.setTooltip ("Keeps the lows of the chopped track mono below this frequency.");

    refreshPresets();
    modeAttachment->sendInitialUpdate();
    updateSnaresLook();
    updateChipNames();
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

    presetBox.addSectionHeading ("IT ANSWERS");
    for (int i = 0; i < factory.size(); ++i)
        if (factory[i].answers)
            presetBox.addItem (factory[i].name + "   (" + factory[i].tags + ")", i + 1);

    presetBox.addSectionHeading ("SNARES: WILD");
    for (int i = 0; i < factory.size(); ++i)
        if (! factory[i].answers && ! factory[i].fill)
            presetBox.addItem (factory[i].name + "   (" + factory[i].tags + ")", i + 1);

    presetBox.addSectionHeading ("SNARES: FILLS");
    for (int i = 0; i < factory.size(); ++i)
        if (! factory[i].answers && factory[i].fill)
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
    probability.label.setVisible (fill);

    // Riser, Lure, Replace and Escalate only matter between and in fills.
    for (juce::Component* c : { (juce::Component*) &anticipation.slider, (juce::Component*) &lure.slider,
                                (juce::Component*) &replace.button, (juce::Component*) &escalate.button })
        c->setAlpha (fill ? 1.0f : 0.45f);

    resized();
    drawer.repaint();
}

void MimicEditor::updateSnaresLook()
{
    const bool on = snaresOn.button.getToggleState();
    snaresOn.button.setButtonText (on ? "ON" : "OFF");

    for (juce::Component* c : { (juce::Component*) &bite.slider, (juce::Component*) &speed.slider, (juce::Component*) &breaks.slider,
                                (juce::Component*) &throwKnob.slider, (juce::Component*) &dynamicsKnob.slider,
                                (juce::Component*) &wildButton, (juce::Component*) &fillButton })
        c->setAlpha (on ? 1.0f : 0.35f);

    for (auto& c : contents)
        c.button.setAlpha (on ? 1.0f : 0.35f);

    stage.repaint();
}

int MimicEditor::heardMaterial() const
{
    const int setTo = juce::roundToInt (value (ids::material));
    return setTo == ids::materialAuto ? juce::jlimit (0, 3, mimicProcessor.materialOut.load()) : juce::jlimit (0, 3, setTo - 1);
}

void MimicEditor::updateChipNames()
{
    // The switches show the version the track's material turns them into.
    const int m = heardMaterial();

    if (m == chipMaterial)
        return;

    chipMaterial = m;

    for (int i = 0; i < numContent; ++i)
    {
        juce::String name = ids::contentNames[i], sub;

        if (i == 0 && m == melodic)        { name = "Shards"; sub = "stutter"; }
        else if (i == 0 && m == harmonic)  { name = "Fog";    sub = "stutter"; }
        else if (i == 1 && (m == melodic || m == vocal)) { name = "Bend"; sub = "roll"; }
        else if (i == 6 && m == harmonic)  { name = "Fog";    sub = "grains"; }

        contents[i].button.setButtonText (name);
        contents[i].button.getProperties().set ("sub", sub);
        contents[i].button.repaint();
    }
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
    }

    updateChipNames();

    // What is playing lights its chip.
    const int state = mimicProcessor.answerStateOut.load();
    const int trick = mimicProcessor.answerTrickOut.load();

    for (int t = 0; t < numTricks; ++t)
    {
        const bool lit = state == Answerer::answering && trick == t;

        if ((bool) tricks[t].button.getProperties()["lit"] != lit)
        {
            tricks[t].button.getProperties().set ("lit", lit);
            tricks[t].button.repaint();
        }
    }

    const bool snares = snaresOn.button.getToggleState();
    const auto kind = (Kind) juce::jlimit (0, 13, mimicProcessor.kindOut.load());

    for (int i = 0; i < numContent; ++i)
    {
        const bool lit = snares && kind != Kind::pass && belongsTo (kind, contentKinds[i]);

        if ((bool) contents[i].button.getProperties()["lit"] != lit)
        {
            contents[i].button.getProperties().set ("lit", lit);
            contents[i].button.repaint();
        }
    }

    // The lid opens as it answers (and as Snares chews); the eyes light up
    // while it waits for its moment.
    const bool answering = state == Answerer::answering;
    const bool waiting = state == Answerer::waiting;
    const bool chewing = snares && kind != Kind::pass;
    const float b = value (ids::bite);
    float target = 0.06f;

    if (answering)
        target = 0.55f + 0.5f * value (ids::mockery);
    else if (waiting)
        target = 0.12f + 0.2f * mimicProcessor.answerWaitOut.load();

    if (chewing)
        target = juce::jmax (target, b);

    lidOpen += 0.3f * (target - lidOpen);
    eyeGlow += 0.25f * ((answering || chewing ? 1.0f : (waiting ? 0.4f + 0.6f * mimicProcessor.answerWaitOut.load() : 0.0f)) - eyeGlow);

    stage.repaint (listensArea);
    stage.repaint (answersArea.withTrimmedTop (400));
    stage.repaint (patternArea.expanded (4, 4).withBottom (snaresArea.getBottom()));
    stage.repaint (0, 568, stage.getWidth(), 52);
}

//==============================================================================
void MimicEditor::paintSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& subtitle, bool lit)
{
    const auto area = r.toFloat();
    g.setColour (ink.withAlpha (0.55f));
    g.fillRoundedRectangle (area.translated (0, 4), 10.0f);
    g.setColour (panel);
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (lit ? gold : edge);
    g.drawRoundedRectangle (area, 10.0f, lit ? 2.5f : 2.0f);

    // A title band, so each section reads as its own thing.
    const auto band = area.withHeight (34.0f);
    g.setColour (ink.withAlpha (0.45f));
    g.fillRoundedRectangle (band.reduced (2.0f), 8.0f);
    g.setColour (lit ? goldLight : dim);
    g.setFont (font (look.bangers, 24.0f));
    g.drawText (title, band.reduced (12.0f, 0.0f), juce::Justification::centredLeft);

    if (subtitle.isNotEmpty())
    {
        g.setColour (dim);
        g.setFont (font (look.typewriter, 11.0f));
        const float titleWidth = juce::GlyphArrangement::getStringWidth (font (look.bangers, 24.0f), title);
        g.drawText (subtitle, band.reduced (12.0f, 0.0f).withTrimmedLeft (titleWidth + 10.0f), juce::Justification::centredLeft);
    }
}

void MimicEditor::knobCaption (juce::Graphics& g, const Knob& k, const juce::String& name, const char* id, float nameSize, const juce::String& help)
{
    const auto s = k.slider.getBounds();
    const int w = juce::jmax (s.getWidth() + 40, 110);
    const int x = s.getCentreX() - w / 2;
    const float alpha = k.slider.getAlpha();
    g.setColour (goldLight.withAlpha (alpha));
    g.setFont (font (look.bangers, nameSize));
    g.drawText (name, x, s.getBottom() + 2, w, (int) nameSize + 2, juce::Justification::centred);
    g.setColour (bone.withAlpha (alpha));
    g.setFont (font (look.typewriter, 12.0f));
    g.drawText (valueText (id), x, s.getBottom() + (int) nameSize + 3, w, 14, juce::Justification::centred);

    if (help.isNotEmpty())
    {
        g.setColour (dim.withAlpha (alpha));
        g.setFont (font (look.typewriter, 11.0f));
        g.drawText (help, x - 30, s.getBottom() + (int) nameSize + 17, w + 60, 14, juce::Justification::centred);
    }
}

void MimicEditor::paintListens (juce::Graphics& g)
{
    const int state = mimicProcessor.answerStateOut.load();
    paintSection (g, listensArea, "IT LISTENS", "to your track", state != Answerer::idle);
    paintChest (g);

    // The ladder: what it is doing now, step by step.
    struct Step { const char* name; const char* words; };
    const Step steps[] { { "HEARS", "you play; it keeps quiet" },
                         { "WAITS", "for a gap" },
                         { "ANSWERS", "" },
                         { "STEPS BACK", "you came back in" } };
    const int lit = state == Answerer::hearing ? 0 : state == Answerer::waiting ? 1
                  : state == Answerer::answering ? 2 : state == Answerer::retreating ? 3 : -1;
    const int trick = mimicProcessor.answerTrickOut.load();

    for (int i = 0; i < 4; ++i)
    {
        const auto row = juce::Rectangle<float> ((float) ladderArea.getX(), (float) ladderArea.getY() + (float) i * 34.0f,
                                                 (float) ladderArea.getWidth(), 30.0f);
        const bool on = i == lit;
        g.setColour (on ? juce::Colour (0xff3a1c12) : ink.withAlpha (0.35f));
        g.fillRoundedRectangle (row, 6.0f);

        // The lamp.
        const auto lamp = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ row.getX() + 16.0f, row.getCentreY() });
        g.setColour (on ? redBright : edge);
        g.fillEllipse (lamp);

        if (on)
        {
            g.setColour (redBright.withAlpha (0.3f));
            g.fillEllipse (lamp.expanded (5.0f));
        }

        g.setColour (on ? bone : dim.withAlpha (0.7f));
        g.setFont (font (look.bangers, 18.0f));
        g.drawText (steps[i].name, row.withTrimmedLeft (32.0f).withWidth (96.0f), juce::Justification::centredLeft);

        auto words = row.withTrimmedLeft (124.0f).reduced (4.0f, 0.0f);
        g.setFont (font (look.typewriter, 11.0f));

        if (i == 1 && on)
        {
            // How long it has waited, against its patience.
            const auto bar = words.withSizeKeepingCentre (words.getWidth(), 8.0f);
            g.setColour (edge);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (goldLight);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, mimicProcessor.answerWaitOut.load())), 3.0f);
        }
        else if (i == 2 && trick >= 0)
        {
            g.setColour (on ? trickColours[trick] : dim.withAlpha (0.6f));
            g.setFont (font (look.bangers, 17.0f));
            g.drawText (juce::String (ids::trickNames[trick]).toUpperCase(), words, juce::Justification::centredLeft);

            if (on)
            {
                const auto bar = words.removeFromBottom (4.0f);
                g.setColour (trickColours[trick].withAlpha (0.8f));
                g.fillRect (bar.withWidth (bar.getWidth() * mimicProcessor.answerProgressOut.load()));
            }
        }
        else
        {
            g.setColour (on ? bone : dim.withAlpha (0.6f));
            g.drawText (steps[i].words, words, juce::Justification::centredLeft);
        }
    }

    // What it hears.
    const int setTo = juce::roundToInt (value (ids::material));
    const int heard = juce::jlimit (0, 3, mimicProcessor.materialOut.load());
    g.setFont (font (look.typewriter, 11.0f));
    g.setColour (dim);
    g.drawText (setTo == ids::materialAuto ? "it hears" : "auto would hear", material.box.getRight() + 10, material.box.getY() - 2, 120, 14, juce::Justification::centredLeft);
    g.setColour (setTo == ids::materialAuto ? goldLight : dim);
    g.setFont (font (look.bangers, 18.0f));
    g.drawText (materialShort[heard], material.box.getRight() + 10, material.box.getY() + 10, 120, 20, juce::Justification::centredLeft);

    // The input, so you see it listening.
    g.setColour (edge);
    g.fillRoundedRectangle (meterArea.toFloat(), 3.0f);
    const float level = juce::jlimit (0.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (mimicProcessor.inputLevelOut.load(), -60.0f) / 60.0f);
    g.setColour (state == Answerer::hearing || state == Answerer::retreating ? goldLight : dim);
    g.fillRoundedRectangle (meterArea.toFloat().withWidth ((float) meterArea.getWidth() * level), 3.0f);
}

void MimicEditor::paintAnswers (juce::Graphics& g)
{
    const bool quiet = value (ids::answer) <= 0.0f;
    paintSection (g, answersArea, "IT ANSWERS", quiet ? "silenced: turn Answer up" : "in the gaps you leave", ! quiet);

    knobCaption (g, hunger, "HUNGER", ids::hunger, 24.0f, "how soon, how often");
    knobCaption (g, mockery, "MOCKERY", ids::mockery, 24.0f, "how far it bends you");
    knobCaption (g, space, "SPACE", ids::space, 18.0f);
    knobCaption (g, heat, "HEAT", ids::heat, 18.0f);
    knobCaption (g, answerLevel, "ANSWER", ids::answer, 18.0f);

    g.setColour (dim);
    g.setFont (font (look.typewriter, 11.0f));
    g.drawText ("ITS TRICKS", tricksArea.getX(), tricksArea.getY() - 16, 200, 14, juce::Justification::centredLeft);

    // The trick now, in words.
    const int state = mimicProcessor.answerStateOut.load();
    const int trick = mimicProcessor.answerTrickOut.load();
    const auto line = juce::Rectangle<int> (answersArea.getX() + 14, tricksArea.getBottom() + 10, answersArea.getWidth() - 28, 40);
    g.setColour (ink.withAlpha (0.4f));
    g.fillRoundedRectangle (line.toFloat(), 6.0f);

    juce::AttributedString text;

    if (trick >= 0 && (state == Answerer::answering || state == Answerer::retreating))
    {
        text.append (juce::String (ids::trickNames[trick]).toUpperCase(), font (look.bangers, 19.0f), trickColours[trick]);
        text.append ("  " + juce::String (trickWords[trick]), font (look.typewriter, 13.0f), bone);
    }
    else if (quiet)
    {
        text.append ("Answer is at 0: it only listens.", font (look.typewriter, 13.0f), dim);
    }
    else
    {
        text.append ("Leave it a gap: stop playing, and it answers.", font (look.typewriter, 13.0f), dim);
    }

    text.setJustification (juce::Justification::centred);
    text.draw (g, line.toFloat().reduced (8.0f, 0.0f));
}

void MimicEditor::paintSnares (juce::Graphics& g)
{
    const bool on = snaresOn.button.getToggleState();
    paintSection (g, snaresArea, "SNARES", "beat masher", on);

    knobCaption (g, bite, "MILD - WILD", ids::bite, 20.0f);
    knobCaption (g, speed, "SPEED", ids::speed, 15.0f);
    knobCaption (g, breaks, "BREAKS", ids::breaks, 15.0f);
    knobCaption (g, throwKnob, "THROW", ids::throwAmount, 15.0f);
    knobCaption (g, dynamicsKnob, "DYNAMICS", ids::dynamics, 15.0f);

    g.setColour (dim.withAlpha (on ? 1.0f : 0.5f));
    g.setFont (font (look.typewriter, 11.0f));
    g.drawText ("WHAT IT CHOPS WITH", chewsArea.getX(), chewsArea.getY() - 15, 200, 13, juce::Justification::centredLeft);

    if (! on)
    {
        g.setColour (ink.withAlpha (0.45f));
        g.fillRoundedRectangle (patternArea.toFloat(), 4.0f);
        g.setColour (dim);
        g.setFont (font (look.typewriter, 12.0f));
        g.drawText ("off: the track passes clean", patternArea, juce::Justification::centred);
        return;
    }

    paintPattern (g, patternArea.toFloat());

    g.setColour (dim);
    g.setFont (font (look.typewriter, 11.0f));
    juce::String status;

    if (fillMode())
    {
        const int toFill = mimicProcessor.barsToFillOut.load();
        status = ! mimicProcessor.transportPlaying.load() ? juce::String ("press play for the fills")
               : mimicProcessor.inFillOut.load() ? juce::String ("FILL!")
               : toFill < 0 ? juce::String ("no fill this phrase")
               : toFill == 0 ? juce::String ("fill this bar")
               : "next fill in " + juce::String (toFill) + (toFill == 1 ? " bar" : " bars");
    }
    else
    {
        const int group = ids::variationBars[juce::jlimit (0, 3, juce::roundToInt (value (ids::variation)))];
        status = group > 1 ? "same pattern " + juce::String (group - 1) + (group == 2 ? " bar" : " bars") + ", then a wilder one"
                           : juce::String ("a new pattern every bar");
    }

    g.drawText (status, patternArea.getX(), patternArea.getBottom() + 3, patternArea.getWidth(), 14, juce::Justification::centredLeft);
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
    juce::ColourGradient glow (juce::Colour (0xff3a1f12), 170.0f, 240.0f, juce::Colour (0xff0b0605), 900.0f, 300.0f, true);
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
    g.drawText ("it listens. it waits. it answers back.", 24, 56, 320, 16, juce::Justification::centredLeft);

    paintListens (g);
    paintAnswers (g);
    paintSnares (g);

    // Footer: one sentence of what is going on.
    g.setColour (ink);
    g.fillRect (0, 568, stage.getWidth(), 52);
    g.setColour (edge);
    g.fillRect (0, 568, stage.getWidth(), 2);

    const int state = mimicProcessor.answerStateOut.load();
    const int trick = mimicProcessor.answerTrickOut.load();
    const int kind = juce::jlimit (0, (int) std::size (kindNames) - 1, mimicProcessor.kindOut.load());
    const auto f = font (look.typewriter, 13.0f);
    juce::AttributedString status;
    status.append ("HEARS ", f, bone);
    status.append (materialNames[heardMaterial()], f, goldLight);
    status.append ("   ", f, bone);

    switch (state)
    {
        case Answerer::hearing:    status.append ("listening while you play", f, dim); break;
        case Answerer::waiting:    status.append ("waiting for its moment...", f, goldLight); break;
        case Answerer::answering:  status.append ("ANSWERS: ", f, bone);
                                   status.append (trick >= 0 ? juce::String (ids::trickNames[trick]).toUpperCase() : juce::String(), f, trick >= 0 ? trickColours[trick] : bone);
                                   break;
        case Answerer::retreating: status.append ("steps back: you are playing", f, dim); break;
        default:                   status.append ("quiet", f, dim); break;
    }

    if (snaresOn.button.getToggleState() && kind != 0)
    {
        status.append ("   SNARES: ", f, bone);
        status.append (kindNames[kind], f, redBright);
    }

    status.draw (g, juce::Rectangle<float> (24.0f, 578.0f, 820.0f, 32.0f));
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

    section (patternBox, fillMode() ? "SNARES: FILL SCHEDULE" : "SNARES: PATTERN");
    section (throatBox, "SNARES: THE THROAT");
    section (fillsBox, fillMode() ? "SNARES: FILLS" : "SNARES: FILLS (FILL MODE)");
    section (outBox, "MIX & OUTPUT");

    g.setColour (dim);
    g.setFont (font (look.typewriter, 11.0f));
    g.drawFittedText (fillMode() ? "Clean bars, then a fill at the end of each phrase."
                                 : "A pattern holds for a group of bars; the last bar is a wilder one.",
                      patternBox.getX() + 14, patternBox.getBottom() - 50, patternBox.getWidth() - 28, 40, juce::Justification::centredLeft, 3);
    g.drawFittedText ("A resonant filter that moves with the grid.",
                      throatBox.getX() + 124, throatBox.getY() + 34, throatBox.getWidth() - 132, 50, juce::Justification::centredLeft, 4);
}

void MimicEditor::resized()
{
    stage.setBounds (spineWidth, 0, contentWidth, getHeight());

    // Header.
    save.setBounds (916, 14, 70, 44);
    next.setBounds (866, 14, 44, 44);
    presetBox.setBounds (630, 14, 230, 44);
    previous.setBounds (580, 14, 44, 44);
    advancedButton.setBounds (880, 576, 110, 36);

    // The three sections.
    listensArea = { 16, 80, 300, 480 };
    answersArea = { 326, 80, 372, 480 };
    snaresArea = { 708, 80, 286, 480 };

    // IT LISTENS: the chest, the ladder, what the track is, the meter.
    ladderArea = { 30, 338, 272, 132 };
    material.label.setBounds (30, 478, 150, 16);
    material.box.setBounds (30, 496, 150, 28);
    meterArea = { 30, 538, 272, 6 };

    // IT ANSWERS: the two big knobs, three small ones, the tricks.
    hunger.slider.setBounds (363, 118, 112, 112);
    mockery.slider.setBounds (549, 118, 112, 112);
    space.slider.setBounds (360, 296, 56, 56);
    heat.slider.setBounds (484, 296, 56, 56);
    answerLevel.slider.setBounds (608, 296, 56, 56);
    tricksArea = { 340, 410, 344, 78 };

    for (int t = 0; t < numTricks; ++t)
        tricks[t].button.setBounds (tricksArea.getX() + (t % 4) * 87, tricksArea.getY() + (t / 4) * 40, 83, 36);

    // SNARES: the switch, mild to wild and the mode, the small knobs, the
    // chips, the pattern.
    snaresOn.button.setBounds (904, 85, 82, 26);
    bite.slider.setBounds (724, 124, 104, 104);
    wildButton.setBounds (842, 130, 140, 36);
    fillButton.setBounds (842, 170, 140, 36);

    for (auto [k, i] : { std::pair<Knob*, int> { &speed, 0 }, { &breaks, 1 }, { &throwKnob, 2 }, { &dynamicsKnob, 3 } })
        k->slider.setBounds (722 + i * 67, 274, 46, 46);

    chewsArea = { 720, 374, 262, 92 };

    for (int i = 0; i < numContent; ++i)
        contents[i].button.setBounds (chewsArea.getX() + (i % 3) * 88, chewsArea.getY() + (i / 3) * 31, 85, 28);

    patternArea = { 720, 474, 262, 52 };

    // The drawer, below the front page.
    drawer.setBounds (0, frontHeight, stage.getWidth(), drawerHeight);
    patternBox = { 8, 10, 300, 218 };
    throatBox = { 314, 10, 250, 218 };
    fillsBox = { 570, 10, 250, 218 };
    outBox = { 826, 10, 176, 218 };

    auto placePick = [] (Pick& p, int x, int y, int w)
    {
        p.label.setBounds (x, y, w, 16);
        p.box.setBounds (x, y + 18, w, 26);
    };

    auto placeKnob = [] (Knob& k, int x, int y, int size)
    {
        k.label.setBounds (x - 14, y, size + 28, 16);
        k.slider.setBounds (x, y + 16, size, size + 16);
    };

    placePick (variation, 22, 38, 150);
    placePick (loop, 180, 38, 116);
    placePick (every, 22, 38, 80);
    placePick (length, 108, 38, 116);
    placePick (phraseStart, 230, 38, 66);
    placeKnob (probability, 30, 96, 44);

    placePick (throatType, 326, 38, 110);
    placeKnob (cutoff, 328, 128, 42);
    placeKnob (resonance, 386, 128, 42);
    placeKnob (move, 444, 128, 42);
    placeKnob (glide, 502, 128, 42);

    placeKnob (anticipation, 590, 38, 44);
    placeKnob (lure, 670, 38, 44);
    replace.button.setBounds (582, 124, 150, 22);
    escalate.button.setBounds (582, 148, 150, 22);
    keep.button.setBounds (582, 172, 150, 22);
    reroll.setBounds (582, 196, 150, 26);

    placeKnob (mix, 846, 38, 44);
    placeKnob (swing, 924, 38, 44);
    placeKnob (output, 846, 128, 44);
    placeKnob (monoBass, 924, 128, 44);
}

//==============================================================================
void MimicEditor::paintChest (juce::Graphics& g)
{
    const float open = juce::jlimit (0.0f, 1.3f, lidOpen);
    const float lift = open * 96.0f;
    const float teethScale = 0.15f + 0.85f * juce::jmin (1.0f, open);
    const auto at = juce::AffineTransform::scale (chestScale).translated (chestX, chestY);

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

