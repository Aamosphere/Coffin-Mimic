#include "PluginEditor.h"
#include "BinaryData.h"

using namespace mimic;

namespace
{
    // The poison comic: black ink, bruise purple, poison green, paper.
    const juce::Colour ink          { 0xff000000 };
    const juce::Colour panel        { 0xff1a0a28 };
    const juce::Colour panelHi      { 0xff22102f };
    const juce::Colour edge         { 0xff3a2a4a };
    const juce::Colour green        { 0xffa6f04a };
    const juce::Colour greenLight   { 0xffbef870 };
    const juce::Colour purple       { 0xff7a2aa0 };
    const juce::Colour purpleBright { 0xffb06cff };
    const juce::Colour bone         { 0xfff2eedc };
    const juce::Colour dim          { 0xffa898c0 };
    const juce::Colour chipOff      { 0xff1c0e28 };
    const juce::Colour spineGold    { 0xffc9962e };
    const juce::Colour wood         { 0xff3b2a21 };
    const juce::Colour woodLid      { 0xff4a3326 };
    const juce::Colour brass        { 0xff8a7a5a };
    const juce::Colour moon         { 0xffe6e0f0 };

    // One name per mimic::Kind, in its order.
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
    metal = juce::Typeface::createSystemTypefaceFor (BinaryData::MetalManiaRegular_ttf, BinaryData::MetalManiaRegular_ttfSize);

    setColour (juce::Label::textColourId, bone);
    setColour (juce::Slider::textBoxTextColourId, bone);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, bone);
    setColour (juce::ComboBox::outlineColourId, ink);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::arrowColourId, ink);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, bone);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, purple);
    setColour (juce::PopupMenu::headerTextColourId, greenLight);
    setColour (juce::ToggleButton::textColourId, bone);
    setColour (juce::ToggleButton::tickColourId, greenLight);
    setColour (juce::ToggleButton::tickDisabledColourId, dim);
    setColour (juce::TextButton::textColourOffId, greenLight);
    setColour (juce::TextButton::textColourOnId, bone);
    setColour (juce::TooltipWindow::backgroundColourId, panel);
    setColour (juce::TooltipWindow::textColourId, bone);
    setColour (juce::TooltipWindow::outlineColourId, green);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, bone);
    setColour (juce::TextEditor::backgroundColourId, ink);
    setColour (juce::TextEditor::textColourId, bone);
    setDefaultSansSerifTypeface (typewriter);
}

void MimicEditor::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float start, float end, juce::Slider& slider)
{
    // Every knob is drawn in ink, like a comic panel: a black disc with a
    // poison-green arc, a paper cap and a black needle, and a hard shadow.
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const auto c = area.getCentre();
    const float angle = start + pos * (end - start);
    const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 4.0f;
    const bool lock = slider.getName() == "bite";
    const float line = R > 40.0f ? 4.0f : 3.0f;
    const float shadow = R > 40.0f ? 5.0f : 3.0f;
    const auto disc = juce::Rectangle<float> (2 * R, 2 * R).withCentre (c);

    g.setColour (ink);
    g.fillEllipse (disc.translated (shadow, shadow));
    g.fillEllipse (disc);

    const float thick = R * 0.26f;
    const float arcR = R - line - thick * 0.5f;
    arc (g, c, arcR, start, end, thick, edge);
    arc (g, c, arcR, start, juce::jmax (start + 0.01f, angle), thick, green);

    const float capR = R * 0.56f;
    const auto cap = juce::Rectangle<float> (2 * capR, 2 * capR).withCentre (c);
    g.setColour (bone);
    g.fillEllipse (cap);
    g.setColour (ink);
    g.drawEllipse (cap, line);

    if (lock)
    {
        // Mild to wild is the coffin's lock: a keyhole in the cap.
        g.fillEllipse (juce::Rectangle<float> (capR * 0.42f, capR * 0.42f).withCentre (c.translated (0, -capR * 0.16f)));
        g.fillPath (zigzag ({ { c.x - capR * 0.1f, c.y - capR * 0.05f }, { c.x + capR * 0.1f, c.y - capR * 0.05f },
                              { c.x + capR * 0.17f, c.y + capR * 0.45f }, { c.x - capR * 0.17f, c.y + capR * 0.45f } }));
    }

    // The needle reaches from the cap's edge onto the arc.
    g.setColour (ink);
    g.drawLine (juce::Line<float> (c.getPointOnCircumference (lock ? capR * 0.7f : capR * 0.15f, angle),
                                   c.getPointOnCircumference (R - line, angle)), R > 40.0f ? 5.0f : 3.5f);
}
void MimicEditor::Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    if (! (bool) b.getProperties()["chip"])
    {
        // Small checkbox with label.
        const auto box = juce::Rectangle<float> (2.0f, (float) b.getHeight() * 0.5f - 8.0f, 16.0f, 16.0f);
        g.setColour (ink);
        g.fillRect (box.translated (2.0f, 2.0f));
        g.setColour (b.getToggleState() ? green : chipOff);
        g.fillRect (box);
        g.setColour (ink);
        g.drawRect (box, 2.0f);
        g.setColour (highlighted ? bone : dim);
        g.setFont (font (typewriter, 13.0f));
        g.drawText (b.getButtonText(), 24, 0, b.getWidth() - 24, b.getHeight(), juce::Justification::centredLeft);
        return;
    }

    // Chips: inked boxes. On: poison green with a hard shadow. While that
    // one plays: a purple rim. A small second line names what the switch
    // turns into on this material.
    const auto r = b.getLocalBounds().toFloat().reduced (1.5f).withTrimmedRight (2.0f).withTrimmedBottom (2.0f);
    const bool on = b.getToggleState();
    const bool lit = (bool) b.getProperties()["lit"];
    const juce::String sub = b.getProperties()["sub"].toString();

    if (on)
    {
        g.setColour (ink);
        g.fillRect (r.translated (3.0f, 3.0f));
    }

    g.setColour (on ? (highlighted ? greenLight : green) : (highlighted ? panelHi : chipOff));
    g.fillRect (r);
    g.setColour (lit ? purpleBright : ink);
    g.drawRect (r, lit ? 4.0f : 3.0f);
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
        g.setFont (font (bangers, juce::jmin (17.0f, r.getHeight() * 0.58f)));
        g.drawText (b.getButtonText().toUpperCase(), r, juce::Justification::centred);
    }
}
void MimicEditor::Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.5f).withTrimmedRight (3.0f).withTrimmedBottom (3.0f);
    const juce::String style = b.getProperties()["style"].toString();
    const float press = down ? 2.0f : 0.0f;

    if (style == "dashed")
    {
        juce::Path outline;
        outline.addRectangle (r);
        const float dashes[] { 7.0f, 5.0f };
        juce::Path dashed;
        juce::PathStrokeType (3.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (highlighted ? greenLight : green);
        g.fillPath (dashed);
        return;
    }

    // Inked boxes with a hard shadow; on: bruise purple.
    g.setColour (ink);
    g.fillRect (r.translated (3.0f, 3.0f));
    g.setColour (b.getToggleState() ? purple : (highlighted ? panelHi : chipOff));
    g.fillRect (r.translated (press, press));
    g.setColour (ink);
    g.drawRect (r.translated (press, press), 3.0f);
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

    // IT LISTENS: what the track is. You say it (Auto guessed too often
    // wrong); the choice changes what the Mimic does.
    stage.addAndMakeVisible (materialBox);
    materialBox.addItem ("Drums", 1 + ids::materialPercussive);
    materialBox.addItem ("A line: melody, riff, bass", 1 + ids::materialMelodic);
    materialBox.addItem ("Chords & pads", 1 + ids::materialHarmonic);
    materialBox.addItem ("A voice", 1 + ids::materialVoice);
    materialBox.setTooltip ("What the track this Mimic sits on is. It changes what the Mimic does: a voice gets ghosts and backmasking, "
                            "a guitar octaves and echoes, chords clouds and drones, drums mocking stutters and tape tricks.");
    materialLabel.setText ("YOUR TRACK IS", juce::dontSendNotification);
    materialLabel.setColour (juce::Label::textColourId, greenLight);
    materialLabel.setFont (font (look.bangers, 16.0f));
    materialLabel.setBorderSize ({});
    stage.addAndMakeVisible (materialLabel);
    materialAttachment = std::make_unique<juce::ParameterAttachment> (*p.parameters.getParameter (ids::material), [this] (float v)
    {
        const int index = juce::roundToInt (v);

        if (index == ids::materialAuto)
            materialBox.setText ("Auto (old session)", juce::dontSendNotification);
        else
            materialBox.setSelectedId (1 + index, juce::dontSendNotification);
    });
    materialBox.onChange = [this]
    {
        if (materialBox.getSelectedId() > 0)
            materialAttachment->setValueAsCompleteGesture ((float) (materialBox.getSelectedId() - 1));
    };
    materialAttachment->sendInitialUpdate();

    setUpPick (stage, timing, ids::timing, "TIMING");
    timing.box.setTooltip ("Song tempo: the answers land on the song's grid (with the track's own swing). "
                           "Free: no grid at all - for playing without a click. BEAT sets how long a beat is.");
    timing.box.onChange = [this] { updateTimingControls(); };
    setUpKnob (stage, freeBeat, ids::freeBeat, {}, false);
    freeBeat.slider.setTooltip ("In Free timing: how long a beat is, in milliseconds. The echoes, stutters and answer lengths follow it.");

    // IT ANSWERS.
    hunger.slider.setName ("big");
    mockery.slider.setName ("big");
    patience.slider.setName ("big");
    setUpKnob (stage, hunger, ids::hunger, {}, false);
    setUpKnob (stage, patience, ids::patience, {}, false);
    setUpKnob (stage, mockery, ids::mockery, {}, false);
    setUpKnob (stage, space, ids::space, {}, false);
    setUpKnob (stage, heat, ids::heat, {}, false);
    setUpKnob (stage, filterKnob, ids::answerFilter, {}, false);
    setUpKnob (stage, memory, ids::memory, {}, false);
    setUpKnob (stage, answerLevel, ids::answer, {}, false);
    filterKnob.slider.setDoubleClickReturnValue (true, 0.0);
    hunger.slider.setTooltip ("How often it answers: low, now and then; full, every gap you leave.");
    patience.slider.setTooltip ("How long it waits: low, it jumps into the smallest gap; high, it waits until you have really stopped.");
    filterKnob.slider.setTooltip ("A filter on the answers only (their echoes and cave too), like one on an aux return: left closes a low-pass, right opens a high-pass. Double-click: open.");
    memory.slider.setTooltip ("How often a Cloud or a Hum takes its grains from further back - anything you played in the last twenty seconds - instead of the last phrase.");
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
    snaresOn.button.setTooltip ("Gnash: the beat masher. Off, the track passes clean (the answers still come). On, it chops, rolls and breaks the track - mild to wild.");
    snaresOn.button.onStateChange = [this] { updateSnaresLook(); };
    setUpSwitch (stage, answerBreaks, ids::answerBreaks, "ANSWER BREAKS", true);
    answerBreaks.button.setTooltip ("When Gnash bites a hole (Breaks), the Mimic answers into it with what the track just played - the two working together.");

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
    for (auto* k : { &hunger, &patience, &mockery, &space, &heat, &filterKnob, &memory, &answerLevel, &freeBeat,
                     &bite, &speed, &breaks, &throwKnob, &dynamicsKnob })
        k->slider.onValueChange = [this] { stage.repaint(); };

    // Header.
    stage.addAndMakeVisible (advancedButton);
    advancedButton.getProperties().set ("style", "dashed");
    advancedButton.onClick = [this] { showAdvanced (! drawer.isVisible()); };

    presetBar = std::make_unique<MimicPresetBar> (p.presets, look.bangers);
    stage.addAndMakeVisible (*presetBar);

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
    setUpKnob (drawer, mix, ids::mix, "GNASH MIX", true);
    setUpKnob (drawer, output, ids::output, "OUTPUT", true);
    setUpKnob (drawer, monoBass, ids::monoBass, "MONO BASS", true);
    mix.slider.setTooltip ("Wild mode: how much of the chopped track against the clean one.");
    monoBass.slider.setTooltip ("Keeps the lows of the chopped track mono below this frequency.");

    modeAttachment->sendInitialUpdate();
    updateSnaresLook();
    updateChipNames();
    updateTimingControls();
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
    k.label.setColour (juce::Label::textColourId, greenLight);
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
    p.label.setColour (juce::Label::textColourId, greenLight);
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
                                (juce::Component*) &wildButton, (juce::Component*) &fillButton, (juce::Component*) &answerBreaks.button })
        c->setAlpha (on ? 1.0f : 0.35f);

    for (auto& c : contents)
        c.button.setAlpha (on ? 1.0f : 0.35f);

    stage.repaint();
}

void MimicEditor::updateTimingControls()
{
    freeBeat.slider.setVisible (timing.box.getSelectedItemIndex() == ids::timingFree);
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
    presetBar->refresh();

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
    // A comic panel (inked frame, hard shadow) and the narrator's caption
    // box hanging over its top edge, as in the old comics: "Meanwhile..."
    juce::ignoreUnused (subtitle);
    const auto area = r.toFloat();
    g.setColour (ink);
    g.fillRect (area.translated (6.0f, 6.0f));
    g.setColour (lit ? panelHi : panel);
    g.fillRect (area);
    g.setColour (ink);
    g.drawRect (area, 4.0f);

    const auto f = font (look.typewriter, 17.0f);
    const float w = juce::GlyphArrangement::getStringWidth (f, title) + 26.0f;
    const auto box = juce::Rectangle<float> (area.getX() + 14.0f, area.getY() - 12.0f, w, 32.0f);
    g.setColour (ink);
    g.fillRect (box.translated (5.0f, 5.0f));
    g.setColour (lit ? bone : dim);
    g.fillRect (box);
    g.setColour (ink);
    g.drawRect (box, 3.0f);
    g.setFont (f);
    g.drawText (title, box, juce::Justification::centred);
}

void MimicEditor::knobCaption (juce::Graphics& g, const Knob& k, const juce::String& name, const char* id, float nameSize, const juce::String& help)
{
    const auto s = k.slider.getBounds();
    const int w = juce::jmax (s.getWidth() + 40, 110);
    const int x = s.getCentreX() - w / 2;
    const float alpha = k.slider.getAlpha();
    g.setFont (font (look.bangers, nameSize));
    g.setColour (ink.withAlpha (alpha));
    g.drawText (name, x + 2, s.getBottom() + 4, w, (int) nameSize + 2, juce::Justification::centred);
    g.setColour (green.withAlpha (alpha));
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
    paintSection (g, listensArea, "Meanwhile, it listens...", {}, true);
    paintChest (g);

    // The free beat's caption.
    if (freeBeat.slider.isVisible())
    {
        g.setColour (greenLight);
        g.setFont (font (look.bangers, 15.0f));
        g.drawText ("BEAT", freeBeat.slider.getX() - 20, freeBeat.slider.getBottom() + 1, freeBeat.slider.getWidth() + 40, 16, juce::Justification::centred);
        g.setColour (bone);
        g.setFont (font (look.typewriter, 11.0f));
        g.drawText (valueText (ids::freeBeat), freeBeat.slider.getX() - 20, freeBeat.slider.getBottom() + 16, freeBeat.slider.getWidth() + 40, 14, juce::Justification::centred);
    }

    // Its feel: the swing it hears in the track, which the answers follow.
    const bool free = juce::roundToInt (value (ids::timing)) == ids::timingFree;
    const float heard = mimicProcessor.swingOut.load();
    const int percent = juce::roundToInt (heard * 100.0f);
    g.setColour (greenLight);
    g.setFont (font (look.bangers, 16.0f));
    g.drawText ("ITS FEEL", 30, 494, 120, 16, juce::Justification::centredLeft);
    g.setColour (free ? dim : bone);
    g.setFont (font (look.typewriter, 13.0f));
    g.drawText (free ? juce::String ("free: no grid") : percent <= 51 ? juce::String ("straight") : "swung " + juce::String (percent) + " %",
                30, 512, 268, 18, juce::Justification::centredLeft);

    // The input, so you see it listening.
    g.setColour (edge);
    g.fillRoundedRectangle (meterArea.toFloat(), 3.0f);
    const float level = juce::jlimit (0.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (mimicProcessor.inputLevelOut.load(), -60.0f) / 60.0f);
    g.setColour (dim);
    g.fillRoundedRectangle (meterArea.toFloat().withWidth ((float) meterArea.getWidth() * level), 3.0f);
}

void MimicEditor::paintAnswers (juce::Graphics& g)
{
    const bool quiet = value (ids::answer) <= 0.0f;
    paintSection (g, answersArea, quiet ? "...it keeps quiet (Answer is at 0)." : "...then it answers back!", {}, ! quiet);

    knobCaption (g, hunger, "HUNGER", ids::hunger, 22.0f, "how often");
    knobCaption (g, patience, "PATIENCE", ids::patience, 22.0f, "how long it waits");
    knobCaption (g, mockery, "MOCKERY", ids::mockery, 22.0f, "how far it bends");
    knobCaption (g, space, "SPACE", ids::space, 16.0f);
    knobCaption (g, heat, "HEAT", ids::heat, 16.0f);
    knobCaption (g, filterKnob, "FILTER", ids::answerFilter, 16.0f);
    knobCaption (g, memory, "MEMORY", ids::memory, 16.0f);
    knobCaption (g, answerLevel, "ANSWER", ids::answer, 16.0f);

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

    juce::ignoreUnused (state);

    if (trick >= 0 && ! quiet)
    {
        // The last answer, held: it does not flicker away.
        text.append ("LAST: ", font (look.typewriter, 12.0f), dim);
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
    paintSection (g, snaresArea, on ? "...and GNASH bites!" : "GNASH sleeps...", {}, on);

    knobCaption (g, bite, "MILD - WILD", ids::bite, 20.0f);
    knobCaption (g, speed, "SPEED", ids::speed, 15.0f);
    knobCaption (g, breaks, "BREAKS", ids::breaks, 15.0f);
    knobCaption (g, throwKnob, "THROW", ids::throwAmount, 15.0f);
    knobCaption (g, dynamicsKnob, "DYNAMICS", ids::dynamics, 15.0f);

    g.setColour (dim.withAlpha (on ? 1.0f : 0.5f));
    g.setFont (font (look.typewriter, 11.0f));
    g.drawText ("ITS TEETH", chewsArea.getX(), chewsArea.getY() - 15, 200, 13, juce::Justification::centredLeft);

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
    g.setColour (spineGold);
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
    // A purple night between the comic panels.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a1240), 0.0f, 0.0f, juce::Colour (0xff050208), 300.0f, (float) frontHeight + 300.0f, false));
    g.fillRect (0, 0, stage.getWidth(), frontHeight);

    // The masthead, slanted, inked all round; and its tag.
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font (look.metal, 50.0f), "COFFIN MIMIC", 0.0f, 0.0f);
        juce::Path letters;
        glyphs.createPath (letters);
        letters.applyTransform (juce::AffineTransform::translation (22.0f, 52.0f).rotated (juce::degreesToRadians (-3.0f), 22.0f, 40.0f));
        juce::Path outline;
        juce::PathStrokeType (7.0f, juce::PathStrokeType::mitered).createStrokedPath (outline, letters);
        g.setColour (ink);
        g.fillPath (outline, juce::AffineTransform::translation (4.0f, 4.0f));
        g.fillPath (outline);
        g.setColour (green);
        g.fillPath (letters);
    }

    {
        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (4.0f), 470.0f, 32.0f));
        const juce::Rectangle<float> tag (400.0f, 20.0f, 150.0f, 28.0f);
        g.setColour (ink);
        g.fillRect (tag.translated (3.0f, 3.0f));
        g.setColour (bone);
        g.fillRect (tag);
        g.setColour (ink);
        g.drawRect (tag, 3.0f);
        g.setFont (font (look.bangers, 17.0f));
        g.drawText ("IT ANSWERS BACK!", tag, juce::Justification::centred);
        g.restoreState();
    }

    paintListens (g);
    paintAnswers (g);
    paintSnares (g);

    // Footer: one sentence of what is going on.
    g.setColour (ink);
    g.fillRect (0, 568, stage.getWidth(), 52);
    g.setColour (edge);
    g.fillRect (0, 568, stage.getWidth(), 2);

    // A calm line: no running commentary.
    juce::AttributedString status;
    status.append ("Play, then leave it a gap.", font (look.typewriter, 13.0f), dim);
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
        g.setColour (greenLight);
        g.setFont (font (look.bangers, 20.0f));
        g.drawText (title, r.getX() + 12, r.getY() + 4, r.getWidth() - 24, 22, juce::Justification::centredLeft);
    };

    section (patternBox, fillMode() ? "GNASH: FILL SCHEDULE" : "GNASH: PATTERN");
    section (throatBox, "GNASH: THE THROAT");
    section (fillsBox, fillMode() ? "GNASH: FILLS" : "GNASH: FILLS (FILL MODE)");
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
    // The preset unit is laid out at full size and drawn at 75 %, tilted
    // like a sound effect (as in the Stalker).
    presetBar->setBounds (560, 4, 560, 70);
    presetBar->setTransform (juce::AffineTransform::scale (0.75f, 0.75f, 560.0f, 4.0f)
                                 .rotated (juce::degreesToRadians (-3.0f), 560.0f + 210.0f, 4.0f + 26.0f));
    advancedButton.setBounds (880, 576, 110, 36);

    // The three sections.
    listensArea = { 16, 80, 300, 480 };
    answersArea = { 326, 80, 372, 480 };
    snaresArea = { 708, 80, 286, 480 };

    // IT LISTENS: the chest, what the track is, the timing, its feel.
    materialLabel.setBounds (30, 350, 200, 16);
    materialBox.setBounds (30, 368, 272, 28);
    timing.label.setBounds (30, 410, 160, 16);
    timing.box.setBounds (30, 428, 170, 28);
    freeBeat.slider.setBounds (228, 404, 50, 50);
    meterArea = { 30, 540, 272, 6 };

    // IT ANSWERS: three big knobs, five small ones, the tricks.
    hunger.slider.setBounds (346, 118, 88, 88);
    patience.slider.setBounds (468, 118, 88, 88);
    mockery.slider.setBounds (590, 118, 88, 88);

    {
        Knob* small[] { &space, &heat, &filterKnob, &memory, &answerLevel };

        for (int k = 0; k < 5; ++k)
            small[k]->slider.setBounds (350 + k * 69, 290, 48, 48);
    }

    tricksArea = { 340, 410, 344, 78 };

    for (int t = 0; t < numTricks; ++t)
        tricks[t].button.setBounds (tricksArea.getX() + (t % 4) * 87, tricksArea.getY() + (t / 4) * 40, 83, 36);

    // GNASH: the switch, mild to wild and the mode, the small knobs, the
    // chips, the pattern.
    snaresOn.button.setBounds (904, 85, 82, 26);
    bite.slider.setBounds (724, 124, 104, 104);
    wildButton.setBounds (842, 126, 140, 32);
    fillButton.setBounds (842, 162, 140, 32);
    answerBreaks.button.setBounds (842, 202, 140, 28);

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
    // The coffin under the moon: its lid slides aside as it answers, and
    // eyes light up in the gap while it waits. Drawn in a 300 x 270 box.
    const float open = juce::jlimit (0.0f, 1.3f, lidOpen);
    const float glow = juce::jlimit (0.0f, 1.0f, eyeGlow);
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (0.82f).translated (43.0f, 112.0f));

    auto path = [] (std::initializer_list<juce::Point<float>> points)
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
    };

    // The moon.
    g.setColour (moon.withAlpha (0.15f));
    g.fillEllipse (136.0f, -4.0f, 148.0f, 148.0f);
    g.setColour (moon);
    g.fillEllipse (156.0f, 16.0f, 108.0f, 108.0f);
    g.setColour (juce::Colour (0xffc8c0dc));
    g.fillEllipse (218.0f, 48.0f, 20.0f, 20.0f);
    g.fillEllipse (189.0f, 85.0f, 14.0f, 14.0f);
    g.setColour (ink);
    g.drawEllipse (156.0f, 16.0f, 108.0f, 108.0f, 3.0f);

    g.setColour (ink.withAlpha (0.7f));
    g.fillEllipse (30.0f, 240.0f, 240.0f, 24.0f);

    // The body, its dark inside, the eyes and the teeth.
    const auto body = path ({ { 110, 30 }, { 190, 30 }, { 216, 82 }, { 196, 250 }, { 104, 250 }, { 84, 82 } });
    g.setColour (wood);
    g.fillPath (body);
    g.setColour (ink);
    g.strokePath (body, juce::PathStrokeType (6.0f, juce::PathStrokeType::mitered));
    const auto inside = path ({ { 118, 44 }, { 182, 44 }, { 202, 84 }, { 186, 236 }, { 114, 236 }, { 98, 84 } });
    g.setColour (juce::Colour (0xff0b0510));
    g.fillPath (inside);

    if (glow > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (green.withAlpha (0.45f * glow), 150.0f, 120.0f, green.withAlpha (0.0f), 150.0f, 200.0f, true));
        g.fillPath (inside);
    }

    for (float ex : { 135.0f, 167.0f })
    {
        const float a = 0.25f + 0.75f * glow;
        g.setColour (green.withAlpha (0.18f * a));
        g.fillEllipse (ex - 18.0f, 100.0f, 36.0f, 36.0f);
        g.setColour (green.withAlpha (a));
        g.fillEllipse (ex - 9.0f, 112.0f, 18.0f, 12.0f);
        g.setColour (ink);
        g.fillEllipse (ex - 2.0f, 113.0f, 4.0f, 10.0f);
    }

    auto teeth = [&] (std::initializer_list<juce::Point<float>> points)
    {
        juce::Path p;
        bool first = true;

        for (auto pt : points)
        {
            if (first) p.startNewSubPath (pt);
            else       p.lineTo (pt);
            first = false;
        }

        g.setColour (bone);
        g.strokePath (p, juce::PathStrokeType (4.0f, juce::PathStrokeType::mitered));
    };

    teeth ({ { 118, 150 }, { 126, 166 }, { 134, 150 }, { 142, 168 }, { 150, 150 }, { 158, 168 }, { 166, 150 }, { 174, 166 }, { 182, 150 } });
    teeth ({ { 120, 186 }, { 128, 172 }, { 136, 186 }, { 144, 170 }, { 152, 186 }, { 160, 170 }, { 168, 186 }, { 176, 172 }, { 182, 186 } });

    // The lid, slid aside; wider while it answers.
    {
        const float angle = juce::degreesToRadians (6.0f + 9.0f * juce::jmin (1.0f, open));
        const auto turn = juce::AffineTransform::rotation (angle, 196.0f, 250.0f).translated (36.0f + 10.0f * juce::jmin (1.0f, open), 4.0f);
        auto lid = path ({ { 150, 20 }, { 232, 36 }, { 244, 92 }, { 206, 256 }, { 148, 250 }, { 132, 86 } });
        lid.applyTransform (turn);
        g.setColour (woodLid);
        g.fillPath (lid);
        g.setColour (ink);
        g.strokePath (lid, juce::PathStrokeType (6.0f, juce::PathStrokeType::mitered));

        juce::Path cross;
        cross.startNewSubPath (176.0f, 82.0f);
        cross.lineTo (212.0f, 88.0f);
        cross.startNewSubPath (195.0f, 66.0f);
        cross.lineTo (192.0f, 116.0f);
        cross.applyTransform (turn);
        g.setColour (ink);
        g.strokePath (cross, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (brass);
        g.strokePath (cross, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Nails.
    g.setColour (brass);
    g.fillEllipse (89.0f, 83.0f, 6.0f, 6.0f);
    g.fillEllipse (107.0f, 241.0f, 6.0f, 6.0f);
    g.restoreState();
}
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
            g.setColour (purpleBright.withAlpha (0.8f));
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
        g.setColour (purpleBright);
        g.fillRect (inner.getX() + juce::jlimit (0.0f, span, position) * pxPerPpq - 1.0f, r.getY(), 2.0f, r.getHeight());
    }
}


//==============================================================================
void MimicSfxButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto fill = isHighlighted ? greenLight : green;
    const float press = isDown ? 1.5f : 0.0f;

    if (kind == Kind::name)
    {
        // The preset name as a sound effect. Long names shrink.
        const auto text = getButtonText().toUpperCase();
        const float maxWidth = bounds.getWidth() - 16.0f;
        auto f = font (bangers, 50.0f).withExtraKerningFactor (0.06f);
        const float width = juce::GlyphArrangement::getStringWidth (f, text);

        if (width > maxWidth)
            f = font (bangers, juce::jmax (22.0f, 50.0f * maxWidth / width)).withExtraKerningFactor (0.06f);

        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (f, text, 0.0f, 0.0f);
        juce::Path letters;
        glyphs.createPath (letters);

        const auto box = letters.getBounds();
        letters.applyTransform (juce::AffineTransform::translation (bounds.getCentreX() - box.getCentreX() - 3.0f + press,
                                                                    bounds.getCentreY() - box.getCentreY() - 3.0f + press));

        juce::Path outline;
        juce::PathStrokeType (6.0f, juce::PathStrokeType::mitered).createStrokedPath (outline, letters);
        outline.addPath (letters);

        g.setColour (ink);
        g.fillPath (outline, juce::AffineTransform::translation (7.0f, 7.0f));
        g.setColour (purple);
        g.fillPath (outline, juce::AffineTransform::translation (5.0f, 5.0f));
        g.setColour (ink);
        g.fillPath (outline);
        g.setColour (fill);
        g.fillPath (letters);
        return;
    }

    // Arrows and SAVE: an ink box leaning forward, a green rim and a purple
    // hard shadow.
    const auto box = bounds.reduced (5.0f, 3.0f).translated (-1.5f + press, -1.5f + press);
    const auto lean = juce::AffineTransform::translation (-box.getCentreX(), -box.getCentreY())
                          .sheared (-0.14f, 0.0f)
                          .translated (box.getCentreX(), box.getCentreY());
    juce::Path shape;
    shape.addRectangle (box);
    shape.applyTransform (lean);

    g.setColour (purple);
    g.fillPath (shape, juce::AffineTransform::translation (3.0f - press, 3.0f - press));
    g.setColour (ink);
    g.fillPath (shape);
    g.setColour (fill);
    g.strokePath (shape, juce::PathStrokeType (3.0f));

    if (kind == Kind::save)
    {
        g.setFont (font (bangers, 17.0f).withExtraKerningFactor (0.08f));
        g.drawText ("SAVE", box.translated (0.0f, 1.0f), juce::Justification::centred);
        return;
    }

    const auto c = box.getCentre();
    const float sz = juce::jmin (box.getWidth(), box.getHeight()) * 0.26f;
    const float dir = kind == Kind::next ? 1.0f : -1.0f;
    juce::Path arrow;
    arrow.addTriangle (c.x + dir * sz, c.y, c.x - dir * sz * 0.8f, c.y - sz, c.x - dir * sz * 0.8f, c.y + sz);
    g.setColour (fill);
    g.fillPath (arrow);
    g.setColour (ink);
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

MimicPresetBar::MimicPresetBar (mimic::PresetManager& p, juce::Typeface::Ptr bangers)
    : presets (p), bangersFace (bangers),
      previous (MimicSfxButton::Kind::previous, bangers), next (MimicSfxButton::Kind::next, bangers),
      name (MimicSfxButton::Kind::name, bangers), save (MimicSfxButton::Kind::save, bangers)
{
    for (auto* b : { &previous, &next, &name, &save })
        addAndMakeVisible (b);

    previous.onClick = [this] { presets.step (-1); refresh(); };
    next.onClick = [this] { presets.step (1); refresh(); };
    name.onClick = [this] { showMenu(); };
    save.onClick = [this] { showSaveDialog(); };

    previous.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    name.setTooltip ("Choose a preset");
    save.setTooltip ("Save the current sound as a preset");
    refresh();
}

void MimicPresetBar::resized()
{
    // SAVE sits at the right; the arrows hug the name, however long it is.
    auto area = getLocalBounds();
    save.setBounds (area.removeFromRight (62).withSizeKeepingCentre (62, 38));
    area.removeFromRight (4);

    const auto text = name.getButtonText().toUpperCase();
    const float textWidth = juce::GlyphArrangement::getStringWidth (font (bangersFace, 50.0f).withExtraKerningFactor (0.06f), text);
    const int nameWidth = juce::jlimit (110, area.getWidth() - 80, (int) textWidth + 30);
    next.setBounds (area.removeFromRight (40).withSizeKeepingCentre (40, 42));
    name.setBounds (area.removeFromRight (nameWidth));
    previous.setBounds (area.removeFromRight (40).withSizeKeepingCentre (40, 42));
}

void MimicPresetBar::refresh()
{
    const auto current = presets.getCurrentName();

    if (name.getButtonText() != current)
    {
        name.setButtonText (current);
        resized();
        repaint();
    }
}

void MimicPresetBar::showMenu()
{
    juce::PopupMenu menu;
    const auto& factory = mimic::PresetManager::getFactory();
    const auto user = presets.getUserPresets();
    const auto current = presets.getCurrentName();

    auto section = [&] (const char* title, std::function<bool (const mimic::PresetManager::Info&)> wanted)
    {
        menu.addSectionHeader (title);

        for (int i = 0; i < factory.size(); ++i)
            if (wanted (factory[i]))
                menu.addItem (1 + i, factory[i].name + "   (" + factory[i].tags + ")", true, factory[i].name == current);
    };

    section ("It answers", [] (const auto& info) { return info.answers; });
    section ("Gnash: wild", [] (const auto& info) { return ! info.answers && ! info.fill; });
    section ("Gnash: fills", [] (const auto& info) { return ! info.answers && info.fill; });

    menu.addSectionHeader ("Yours");

    if (user.isEmpty())
        menu.addItem (-1, "(none yet - press SAVE)", false);

    for (int i = 0; i < user.size(); ++i)
        menu.addItem (1000 + i, user[i].getFileNameWithoutExtension(), true, user[i].getFileNameWithoutExtension() == current);

    menu.addSeparator();
    menu.addItem (2000, "Show preset folder");

    juce::Component::SafePointer<MimicPresetBar> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name),
                        [safe, user] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;

                            if (result >= 2000)
                            {
                                mimic::PresetManager::userFolder().createDirectory();
                                mimic::PresetManager::userFolder().revealToUser();
                            }
                            else if (result >= 1000)
                            {
                                safe->presets.loadUser (user[result - 1000]);
                            }
                            else
                            {
                                safe->presets.loadFactory (result - 1);
                            }

                            safe->refresh();
                        });
}

void MimicPresetBar::showSaveDialog()
{
    auto* window = new juce::AlertWindow ("Save preset", "Name the preset:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", presets.getCurrentName());
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MimicPresetBar> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result)
    {
        if (safe != nullptr && result == 1)
        {
            safe->presets.saveUser (window->getTextEditorContents ("name"));
            safe->refresh();
        }
    }), true);
}
