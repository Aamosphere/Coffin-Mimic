#include "PluginProcessor.h"

#if ! MIMIC_HEADLESS
 #include "PluginEditor.h"
#endif

using namespace mimic;

namespace
{
    float coeff (double seconds, double sampleRate)
    {
        return 1.0f - std::exp (-1.0f / (float) (seconds * sampleRate));
    }

    int64_t floorDiv (int64_t a, int64_t b)
    {
        return a >= 0 ? a / b : -((-a + b - 1) / b);
    }

    float unit (uint64_t h)
    {
        return (float) (h >> 40) * (1.0f / 16777216.0f);
    }

    // Span keys: the low two bits say what kind of span it is, so a Wild bar
    // and a fill never share a seed by accident.
    enum SpanTag { tagWild = 0, tagFill = 1, tagManual = 2 };

    int64_t spanKeyFor (int64_t index, SpanTag tag) { return index * 4 + tag; }
}

MimicProcessor::MimicProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",   juce::AudioChannelSet::stereo(), true)
        .withInput  ("PREY IN", juce::AudioChannelSet::stereo(), false)
        .withOutput ("Output",  juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "MIMIC_STATE", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout MimicProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? String ("Off") : String (roundToInt (v)) + " Hz"; });
    auto speedText = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        constexpr const char* names[] { "1/4", "1/8", "1/16", "1/32" };
        return String (names[jlimit (0, 3, (int) (v * 3.999f))]);
    });

    auto add = [&] (const char* id, const char* name, NormalisableRange<float> range, float def, AudioParameterFloatAttributes attr)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def, attr));
    };

    auto toggle = [&] (const char* id, const char* name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };

    auto pick = [&] (const char* id, const char* name, const StringArray& names, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, names, def));
    };

    pick (ids::mode, "Mode", ids::modeNames, ids::modeWild);
    add (ids::bite, "Bite", { 0.0f, 1.0f }, 0.1f, percent);
    add (ids::speed, "Speed", { 0.0f, 1.0f }, 0.6f, speedText);
    add (ids::mix, "Mix", { 0.0f, 1.0f }, 1.0f, percent);
    add (ids::output, "Output", { -24.0f, 12.0f }, 0.0f, db);

    pick (ids::fillEvery, "Fill Every", ids::fillEveryNames, 2);
    pick (ids::fillLength, "Fill Length", ids::fillLengthNames, ids::lastBeat);
    add (ids::probability, "Probability", { 0.0f, 1.0f }, 1.0f, percent);
    toggle (ids::replace, "Replace", true);
    add (ids::anticipation, "Anticipation", { 0.0f, 1.0f }, 0.0f, percent);
    toggle (ids::escalate, "Escalate", false);
    add (ids::lure, "Lure", { 0.0f, 1.0f }, 0.0f, percent);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::seed, 1 }, "Seed", 0, 9999, 0));
    toggle (ids::keep, "Keep", false);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::triggerNote, 1 }, "Trigger Note", 0, 127, 36,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 3); })));

    // Out of the box the Mimic mashes, stutters and rolls. (Echo, added
    // later, is registered at the end of the list.)
    for (int i = 0; i < numContent - 1; ++i)
    {
        const auto kind = contentKinds[i];
        toggle (ids::contentIds[i], ids::contentNames[i].toRawUTF8(),
                kind == Kind::slice || kind == Kind::stutter || kind == Kind::roll);
    }

    pick (ids::prey, "Prey", ids::preyNames, ids::preySelf);
    pick (ids::material, "Material", ids::materialNames, ids::materialAuto);
    // The teeth rest until they are turned up.
    add (ids::slicer, "Slicer", { 0.0f, 1.0f }, 0.0f, percent);
    add (ids::shaper, "Shaper", { 0.0f, 1.0f }, 0.0f, percent);
    add (ids::filter, "Filter", { 0.0f, 1.0f }, 0.0f, percent);
    add (ids::pitcher, "Pitcher", { 0.0f, 1.0f }, 0.0f, percent);
    toggle (ids::slicerInvert, "Slicer Invert", false);
    toggle (ids::shaperInvert, "Shaper Invert", false);
    toggle (ids::filterInvert, "Filter Invert", false);
    toggle (ids::pitcherInvert, "Pitcher Invert", false);

    NormalisableRange<float> bassRange (0.0f, 300.0f);
    bassRange.setSkewForCentre (100.0f);
    add (ids::monoBass, "Mono Bass", bassRange, 120.0f, hz);

    auto swingText = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (50.0f + 25.0f * v)) + " %"; });
    add (ids::swing, "Swing", { 0.0f, 1.0f }, 0.0f, swingText);
    add (ids::breaks, "Breaks", { 0.0f, 1.0f }, 0.0f, percent);
    add (ids::throwAmount, "Throw", { 0.0f, 1.0f }, 0.0f, percent);

    pick (ids::variation, "Variation", ids::variationNames, 2);
    pick (ids::filterType, "Filter Type", ids::filterTypeNames, ids::filterOff);
    NormalisableRange<float> cutoffRange (30.0f, 18000.0f);
    cutoffRange.setSkewForCentre (1000.0f);
    add (ids::cutoff, "Cutoff", cutoffRange, 1200.0f,
         AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; }));
    add (ids::resonance, "Resonance", { 0.0f, 1.0f }, 0.3f, percent);
    add (ids::filterMove, "Filter Move", { 0.0f, 1.0f }, 0.5f, percent);

    pick (ids::loop, "Loop", ids::loopNames, ids::loopEvolve);
    pick (ids::phraseStart, "Phrase Start", ids::phraseStartNames, 0);
    add (ids::filterGlide, "Filter Glide", { 0.0f, 1.0f }, 1.0f,
         AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.005f ? String ("Steps") : String (roundToInt (v * 100.0f)) + " %"; }));

    add (ids::voice, "Voice", { 0.0f, 1.0f }, 0.0f, percent);
    toggle (ids::voiceInvert, "Voice Invert", false);
    add (ids::possess, "Possess", { 0.0f, 1.0f }, 0.0f, percent);
    toggle (ids::possessInvert, "Possess Invert", false);

    toggle (ids::useEcho, "Echo", false);
    add (ids::dynamics, "Dynamics", { 0.0f, 1.0f }, 0.0f, percent);

    return layout;
}

bool MimicProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto prey = layouts.getChannelSet (true, 1);

        if (! prey.isDisabled() && prey != juce::AudioChannelSet::stereo() && prey != juce::AudioChannelSet::mono())
            return false;
    }

    return true;
}

void MimicProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    now = 0;

    history.prepare (sr, 24.0);
    mainOnsets.reset();
    mainDetector.prepare (sr);
    preyDetector.prepare (sr);
    classifier.prepare (sr);
    player.prepare (sr, &history, &mainOnsets);
    shifter.prepare (sr);
    vocoder.prepare (sr);
    lastMaterial = -1;
    echoDelay.prepare (sr);
    echoSend = 0.0f;
    echoAlive = 0;
    echoRamp = 1.0f / (float) (0.005 * sr);
    restDuck = 1.0f;
    restDuckCoeff = coeff (0.003, sr);
    chopAt = possessAt = -1.0;
    voiceMix = 0.0f;
    voiceStep = coeff (0.01, sr);
    possessEnd = -1;
    possessFlash = 0.0f;
    toothFilter.reset();
    riser.reset();
    throat.reset();
    throatStep = std::numeric_limits<int64_t>::min();
    throatJump = throatOctaves = throatFrom = throatRamp = 0.0f;
    throatRampStep = 1.0f;
    loopOffset = 0;
    lastRawBar = std::numeric_limits<int64_t>::min();
    throatGlide = coeff (0.008, sr);

    preyAtt = coeff (0.001, sr);
    preyRel = coeff (0.08, sr);
    preyPeakRel = coeff (2.0, sr);
    brightSmooth = coeff (0.02, sr);
    hpCoeff = std::exp (-juce::MathConstants<float>::twoPi * 1500.0f / (float) sr);
    riserCoeff = coeff (0.01, sr);
    glideCoeff = coeff (0.02, sr);
    engageStep = 1.0f / (float) (0.003 * sr);
    rampStep = 1.0f / (float) (0.01 * sr);
    powerCoeff = coeff (0.3, sr);
    compSmooth = coeff (0.05, sr);
    peakAttack = coeff (0.0002, sr);
    peakRelease = coeff (0.15, sr);
    dryPeak = wetPeak = 0.0f;

    juce::ignoreUnused (samplesPerBlock);
    sideLow.reset();
    bassFrequency = -1.0f;

    preyEnv = preyPeak = preyHp = preyHpX = preyBrightEnv = preyBright = 0.0f;
    preyHigh = false;
    midiNote = -1;
    midiHeld = 0;
    engage = 0.0f;
    replaceMix = flag (ids::replace) ? 1.0f : 0.0f;
    mixSmoothed = 1.0f;
    dryPower = wetPower = 0.0f;
    compGain = 1.0f;
    riserLevel = 0.0f;
    pitchRatio = 1.0f;
    manualActive = false;
    wasPlaying = false;
    expectedPpq = -1.0;
    freePpq = 0.0;
    discontinuity();

    throwVerb.prepare (sr);
    throwDetector.prepare (sr);
    sendAttack = 1.0f / (float) (0.001 * sr);
    sendRelease = 1.0f / (float) (0.015 * sr);
    sendEnv = 0.0f;
    sendHold = verbAlive = afterFill = 0;
    sendCount = sendStart = 0;
    lastThrowDecay = -1.0f;

    setLatencySamples (0);
}

void MimicProcessor::discontinuity()
{
    player.beginPass (now);
    player.setValidFrom (now);
    segmentIndex = -1;
    possessEnd = -1;
    chopAt = possessAt = -1.0;
    spanKey = std::numeric_limits<int64_t>::min();
    fillsInRow = 0;
    wasInFill = false;
}

uint64_t MimicProcessor::spanSeed (int64_t key) const
{
    return hash64 ((uint64_t) key, (uint64_t) seedValue + 0x51edull);
}

void MimicProcessor::startSpan (int64_t key, double start, const GenSettings& g, bool keep, int64_t seedKey)
{
    spanKey = key;
    spanStart = start;
    segmentIndex = -1;

    const auto seed = spanSeed (keep ? keptKey.load() : seedKey);
    generate (script, g, seed);

    publishScript (g.span);

    // Never the same fill twice in a row: compare with the fill before.
    // (Wild patterns repeat on purpose.)
    if (! keep && g.fill)
    {
        generate (previousScript, g, spanSeed (seedKey - 4));

        if (previousScript.signature() == script.signature())
        {
            generate (script, g, hash64 (seed, 77));
            publishScript (g.span);
        }
    }

    slicerRng = Rng (hash64 (seed, 5));
    possessRng = Rng (hash64 (seed, 7));
    throwRng = Rng (hash64 (seed, 6));
}

void MimicProcessor::publishScript (double span)
{
    const int count = juce::jmin (script.count, maxShown);

    for (int i = 0; i < count; ++i)
    {
        const auto& seg = script.segments[(size_t) i];
        const auto start = (uint32_t) juce::jlimit (0, 4095, (int) std::lround (seg.start * 96.0));
        const auto length = (uint32_t) juce::jlimit (0, 4095, (int) std::lround (seg.length * 96.0));
        shownSegments[(size_t) i].store (start | (length << 12) | ((uint32_t) seg.kind << 24), std::memory_order_relaxed);
    }

    shownSpan.store ((float) span, std::memory_order_relaxed);
    shownCount.store (count, std::memory_order_release);
}

void MimicProcessor::reroll()
{
    if (auto* p = parameters.getParameter (ids::seed))
    {
        const int current = (int) value (ids::seed);
        const int next = (current + 1 + juce::Random::getSystemRandom().nextInt (9998)) % 10000;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) next));
        p->endChangeGesture();
    }
}

void MimicProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, true, 0);
    const int numSamples = buffer.getNumSamples();
    const int numCh = main.getNumChannels();

    if (numCh == 0 || numSamples == 0)
        return;

    // The prey's own input, when the host gives one.
    const float* scL = nullptr;
    const float* scR = nullptr;

    if (getBusCount (true) > 1)
    {
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);

            if (sc.getNumChannels() > 0)
            {
                scL = sc.getReadPointer (0);
                scR = sc.getReadPointer (juce::jmin (1, sc.getNumChannels() - 1));
            }
        }
    }

    // Parameters, once per block.
    const bool wild = choice (ids::mode) == ids::modeWild;
    const float bite = value (ids::bite);
    const float speed = value (ids::speed);
    const float mix = value (ids::mix);
    // Rounded so 0 dB is exactly unity: Fill mode must pass the track bit for bit.
    const float outDb = std::round (value (ids::output) * 100.0f) / 100.0f;
    const float outGain = outDb == 0.0f ? 1.0f : juce::Decibels::decibelsToGain (outDb);
    const int every = ids::fillEveryBars[juce::jlimit (0, 5, choice (ids::fillEvery))];
    const int lengthChoice = choice (ids::fillLength);
    const float probability = value (ids::probability);
    const bool replace = flag (ids::replace);
    const float anticipation = value (ids::anticipation);
    const bool escalate = flag (ids::escalate);
    const float lure = value (ids::lure);
    const bool keep = flag (ids::keep);
    const int triggerNote = (int) value (ids::triggerNote);
    const int preySource = choice (ids::prey);
    const int materialChoice = choice (ids::material);
    const float slicerAmount = value (ids::slicer), shaperAmount = value (ids::shaper);
    const float filterAmount = value (ids::filter), pitcherAmount = value (ids::pitcher);
    const bool slicerInvert = flag (ids::slicerInvert), shaperInvert = flag (ids::shaperInvert);
    const bool filterInvert = flag (ids::filterInvert), pitcherInvert = flag (ids::pitcherInvert);
    const float voiceAmount = value (ids::voice), possessAmount = value (ids::possess);
    const bool voiceInvert = flag (ids::voiceInvert), possessInvert = flag (ids::possessInvert);
    const float monoBass = value (ids::monoBass);
    const float swing = value (ids::swing);
    const float breaks = value (ids::breaks);
    const float throwAmount = value (ids::throwAmount);
    const int variationChoice = choice (ids::variation);
    const int throatType = choice (ids::filterType);
    const float cutoffHz = value (ids::cutoff);
    const float resonance = value (ids::resonance);
    const float filterMove = value (ids::filterMove);
    const float filterGlide = value (ids::filterGlide);
    const bool evolve = choice (ids::loop) == ids::loopEvolve;
    const int phraseStart = choice (ids::phraseStart);
    player.setDynamics (value (ids::dynamics));

    if (throwAmount > 0.0f && std::abs (throwAmount - lastThrowDecay) > 0.01f)
    {
        lastThrowDecay = throwAmount;
        gargoyle::Reverb::Settings rs;
        rs.space = 0;
        rs.size = 0.75f;
        rs.decay = 2.0f + 4.0f * throwAmount;
        rs.early = 0.4f;
        rs.predelayMs = 25.0f;
        rs.lows = 0.8f;
        rs.highs = 0.75f;
        rs.diffuse = 0.85f;
        rs.drift = 0.5f;
        rs.brightness = 6500.0f;
        throwVerb.setSettings (rs);
    }

    const float throwReturn = 0.4f + 0.4f * throwAmount;
    sendStart = 0;
    sendCount = 0;
    seedValue = (int) value (ids::seed);

    unsigned enabled = 0;

    for (int i = 0; i < numContent; ++i)
        if (flag (ids::contentIds[i]))
            enabled |= 1u << i;

    if (keep && ! keepWasOn)
        keptKey.store (lastFillKey);

    keepWasOn = keep;

    const bool bassActive = monoBass >= 1.0f && numCh > 1;

    if (bassActive && std::abs (monoBass - bassFrequency) > 0.01f)
    {
        bassFrequency = monoBass;
        sideLow.set (monoBass, 0.7071f, sr);
    }

    sidechainMissing.store (preySource == ids::preySidechain && scL == nullptr, std::memory_order_relaxed);

    // Transport. With no tempo, 120 BPM in 4/4.
    double bpm = 120.0, hostPpq = 0.0;
    bool playing = false;
    int numerator = 4, denominator = 4;
    juce::Optional<double> lastBarStart;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto b = position->getBpm())
                bpm = *b > 1.0 ? *b : 120.0;

            if (auto p = position->getPpqPosition())
                hostPpq = *p;

            if (auto sig = position->getTimeSignature())
            {
                numerator = juce::jmax (1, sig->numerator);
                denominator = juce::jmax (1, sig->denominator);
            }

            lastBarStart = position->getPpqPositionOfLastBarStart();
            playing = position->getIsPlaying();
        }
    }

    transportPlaying.store (playing, std::memory_order_relaxed);

    const double ppqPerSample = bpm / 60.0 / sr;
    const double spq = 1.0 / ppqPerSample;
    const double barLen = numerator * 4.0 / denominator;
    const double beatLen = 4.0 / denominator;
    // The grid: 1/4, 1/8, 1/16 or 1/32.
    const double step = std::exp2 (-(double) juce::jlimit (0, 3, (int) (speed * 3.999f)));
    const double fillLen = juce::jmin ((double) every * barLen,
                                       lengthChoice == ids::lastBeat ? beatLen
                                     : lengthChoice == ids::lastHalfBar ? barLen * 0.5 : barLen);

    double base;

    bool jumped = false;

    if (playing)
    {
        if (! wasPlaying)
        {
            loopOffset = 0;
            lastRawBar = std::numeric_limits<int64_t>::min();
            discontinuity();
        }
        else if (std::abs (hostPpq - expectedPpq) > juce::jmax (1.0e-3, ppqPerSample * 4.0))
        {
            jumped = true;
            discontinuity();
        }

        base = hostPpq;
    }
    else
    {
        if (wasPlaying)
        {
            loopOffset = 0;
            lastRawBar = std::numeric_limits<int64_t>::min();
            discontinuity();
        }

        base = freePpq;
    }

    wasPlaying = playing;

    double barStart = std::floor (base / barLen) * barLen;

    if (playing && lastBarStart.hasValue())
    {
        barStart = *lastBarStart;

        while (base - barStart >= barLen) barStart += barLen;
        while (base < barStart) barStart -= barLen;
    }

    int64_t barIndex = (int64_t) std::llround (barStart / barLen);

    // A cycle loop jumping back counts its passes, so a looped bar can get a
    // new pattern every pass (New every pass). Where fills and variations
    // fall is always the song's own bar: a fill set for bar 2 stays in bar 2
    // however the loop is set.
    if (jumped && lastRawBar != std::numeric_limits<int64_t>::min() && barIndex <= lastRawBar)
        loopOffset += lastRawBar + 1 - barIndex;

    // Phrases start at the Phrase Start bar: a song that begins on bar 3
    // gets its fills at the end of bars 6, 10, 14...
    const int64_t barShift = -phraseStart;
    // Salt for the seeds of each loop pass (0 without loops, or with Same
    // every pass).
    const int64_t passSalt = evolve ? loopOffset * 1000003 : 0;
    int barsToFill = -1;
    float progressShown = 0.0f;
    bool fillShown = false;

    // The granular delay: dotted eighths, longer repeats as Bite rises;
    // melodies and pads shimmer an octave up, voices and pads go backwards
    // now and then.
    {
        const int m = materialChoice == ids::materialAuto ? classifier.material() : materialChoice - 1;
        const float shimmer = m == melodic ? 0.3f : (m == harmonic ? 0.45f : (m == vocal ? 0.25f : 0.0f));
        const float backwards = m == vocal ? 0.3f : (m == harmonic ? 0.2f : 0.05f);
        echoDelay.setup (0.75 * spq, 0.4f + 0.3f * bite, shimmer, backwards);
    }

    GenSettings g;
    g.beat = beatLen;
    g.bar = barLen;
    g.step = step;
    g.enabled = enabled;
    g.swing = 0.5f * swing;
    g.breaks = breaks;
    g.material = materialChoice == ids::materialAuto ? classifier.material() : materialChoice - 1;

    // A new material (picked, or heard by Auto) takes effect at once: the
    // pattern playing now is written again for it, from the same place.
    if (g.material != lastMaterial)
    {
        if (lastMaterial >= 0)
            rewrite = true;

        lastMaterial = g.material;
    }

    auto* left = main.getWritePointer (0);
    auto* right = numCh > 1 ? main.getWritePointer (1) : nullptr;
    auto midiIt = midi.cbegin();
    bool midiStrike = false;

    for (int i = 0; i < numSamples; ++i, ++now)
    {
        const double tl = base + i * ppqPerSample;

        // MIDI: the trigger note fires a fill, every other note is prey.
        while (midiIt != midi.cend() && (*midiIt).samplePosition <= i)
        {
            const auto msg = (*midiIt).getMessage();

            if (msg.isNoteOn())
            {
                if (msg.getNoteNumber() == triggerNote)
                {
                    manualActive = true;
                    manualStart = tl;
                    manualOffset = std::fmod (juce::jmax (0.0, tl - barStart), barLen);
                    ++manualCount;
                }
                else
                {
                    midiNote = msg.getNoteNumber();
                    midiVelocity = msg.getFloatVelocity();
                    ++midiHeld;
                    midiStrike = true;
                }
            }
            else if (msg.isNoteOff() && msg.getNoteNumber() != triggerNote)
            {
                midiHeld = juce::jmax (0, midiHeld - 1);
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                midiHeld = 0;
            }

            ++midiIt;
        }

        while (tl - barStart >= barLen)
        {
            barStart += barLen;
            ++barIndex;
        }

        lastRawBar = barIndex;
        const int64_t phraseBar = barIndex + barShift;

        const float inL = left[i];
        const float inR = right != nullptr ? right[i] : inL;
        history.write (now, inL, inR);

        const float mono = 0.5f * (inL + inR);
        const bool mainOnset = mainDetector.process (mono);

        if (mainOnset)
            mainOnsets.add (now - (int64_t) (0.001 * sr));

        classifier.push (mono, mainOnset);

        // The prey: envelope, brightness, strikes and gaps.
        bool preyStrike = false;
        float envN = 0.0f, preyMono = 0.0f;

        if (preySource == ids::preyMidi)
        {
            const float target = midiHeld > 0 ? midiVelocity : 0.0f;
            preyEnv += (target > preyEnv ? preyAtt : preyRel) * (target - preyEnv);
            envN = preyEnv;
            preyBright += brightSmooth * (midiVelocity - preyBright);
            preyStrike = midiStrike;
            midiStrike = false;
        }
        else
        {
            float pm = mono;

            if (preySource == ids::preySidechain && scL != nullptr)
                pm = 0.5f * (scL[i] + scR[i]);

            preyMono = pm;

            const bool onset = preySource == ids::preySelf ? mainOnset : preyDetector.process (pm);
            preyStrike = onset;

            const float a = std::abs (pm);
            preyEnv += (a > preyEnv ? preyAtt : preyRel) * (a - preyEnv);
            preyPeak = preyEnv > preyPeak ? preyEnv : preyPeak + preyPeakRel * (preyEnv - preyPeak);
            envN = juce::jlimit (0.0f, 1.0f, preyEnv / juce::jmax (preyPeak, 1.0e-4f));

            preyHp = hpCoeff * (preyHp + pm - preyHpX);
            preyHpX = pm;
            const float h = std::abs (preyHp);
            preyBrightEnv += (h > preyBrightEnv ? preyAtt : preyRel) * (h - preyBrightEnv);
            const float ratio = juce::jlimit (0.0f, 1.0f, 2.5f * preyBrightEnv / (preyEnv + 1.0e-6f));
            preyBright += brightSmooth * (ratio - preyBright);
        }

        bool preyGap = false;

        if (envN > 0.6f)
            preyHigh = true;
        else if (preyHigh && envN < 0.25f)
        {
            preyHigh = false;
            preyGap = true;
        }

        // Scheduling: where are we, is a fill playing?
        bool inFill = false, haveSpan = false;
        float antProgress = 0.0f, fillProgress = 0.0f;
        int64_t key = 0, seedKey = 0;
        double sStart = 0.0, sLength = 0.0, offsetInBar = 0.0;
        bool variationBar = false;

        if (manualActive && (tl - manualStart >= fillLen || tl < manualStart))
            manualActive = false;

        if (manualActive)
        {
            key = seedKey = spanKeyFor (manualCount, tagManual);
            sStart = manualStart;
            sLength = fillLen;
            offsetInBar = manualOffset;
            haveSpan = inFill = true;
        }
        else if (wild)
        {
            // A pattern holds for its group of bars; the last bar of the
            // group is a wilder variation, like a breakbeat edit.
            const int groupBars = ids::variationBars[juce::jlimit (0, 3, variationChoice)];
            const int64_t group = floorDiv (phraseBar, groupBars);
            variationBar = groupBars > 1 && phraseBar - group * groupBars == groupBars - 1;
            key = spanKeyFor (phraseBar + passSalt, tagWild);
            seedKey = spanKeyFor (group * 2 + (variationBar ? 1 : 0) + passSalt, tagWild);
            sStart = barStart;
            sLength = barLen;
            haveSpan = true;
        }
        else if (playing)
        {
            const int64_t group = floorDiv (phraseBar, every);
            const int64_t barInGroup = phraseBar - group * every;
            const double groupEnd = barStart + (double) (every - barInGroup) * barLen;
            const double fillStart = groupEnd - fillLen;
            // No fills before the phrases start.
            const bool allowed = phraseBar >= 0 && (keep || probability >= 0.999f
                              || unit (hash64 ((uint64_t) (group + passSalt), (uint64_t) seedValue ^ 0xf111ull)) < probability);

            if (allowed)
            {
                barsToFill = juce::jmax (0, (int) std::floor ((fillStart - tl) / barLen));

                if (tl >= fillStart)
                {
                    key = seedKey = spanKeyFor (group + passSalt, tagFill);
                    sStart = fillStart;
                    sLength = fillLen;
                    offsetInBar = std::fmod (barLen - std::fmod (fillLen, barLen), barLen);
                    haveSpan = inFill = true;
                }
                else if (anticipation > 0.0f)
                {
                    const double antLength = anticipation * barLen;

                    if (tl >= fillStart - antLength)
                        antProgress = (float) ((tl - (fillStart - antLength)) / antLength);
                }
            }
        }

        if (inFill)
            fillProgress = (float) juce::jlimit (0.0, 1.0, (tl - sStart) / sLength);

        if (wasInFill && ! inFill)
        {
            ++fillsInRow;
            afterFill = (int) (0.25 * spq);   // the downbeat after a fill can be thrown
        }

        wasInFill = inFill;

        float intensity = variationBar ? juce::jmin (1.0f, bite * 1.25f + 0.1f) : bite;

        if (inFill && escalate)
            intensity *= 0.55f + 0.45f * (float) juce::jmin (fillsInRow, 3) / 3.0f;

        // A possessed event plays out before the script takes over again.
        if (possessEnd >= 0 && now >= possessEnd)
        {
            possessEnd = -1;
            segmentIndex = -2;   // pick up the script wherever it is now
        }

        const bool possessed = possessEnd >= 0;

        if (haveSpan)
        {
            lastFillKey = key;

            if (key != spanKey || rewrite)
            {
                g.span = sLength;
                g.intensity = intensity;
                g.fill = inFill;
                g.offsetInBar = offsetInBar;
                const bool sameSpan = key == spanKey;
                const auto before = script.signature();
                const int playingIndex = segmentIndex;
                startSpan (key, sStart, g, keep, seedKey);

                // Written again for a new material: if it came out the
                // same, the segment playing now just carries on.
                if (sameSpan && script.signature() == before)
                    segmentIndex = playingIndex;

                rewrite = false;
            }

            const double pos = tl - spanStart;
            const auto* seg = segmentIndex >= 0 && segmentIndex < script.count ? &script.segments[(size_t) segmentIndex] : nullptr;

            if (! possessed && (seg == nullptr || pos < seg->start - 1.0e-9 || pos >= seg->start + seg->length - 1.0e-9))
            {
                const int index = script.find (pos);

                if (index != segmentIndex)
                {
                    segmentIndex = index;

                    if (index >= 0)
                    {
                        const auto& s = script.segments[(size_t) index];
                        player.begin (s, now, spq, (int64_t) ((s.start + s.length - pos) * spq));
                    }
                    else
                    {
                        player.beginPass (now);
                    }
                }
            }
        }
        else if (! possessed && (segmentIndex != -1 || player.active()))
        {
            player.beginPass (now);
            segmentIndex = -1;
            spanKey = std::numeric_limits<int64_t>::min();
        }

        // How much the teeth bite right now: fully in Wild and in a fill
        // (their own knobs set how hard), and only with Lure or the riser
        // between fills.
        const float teeth = wild || inFill ? 1.0f : juce::jmax (lure, antProgress);

        // The prey's hits keep time: a hit on a grid line (or just after it,
        // as detection lags a little) acts at once; one between the lines
        // waits for the next one.
        const double gridSteps = (tl - barStart) / step;
        const double intoStep = gridSteps - std::floor (gridSteps);
        const bool onGrid = intoStep < 0.2;
        const double nextGrid = barStart + (std::floor (gridSteps) + 1.0) * step;

        // Chop: the prey's strikes (or its gaps) restart the slices.
        bool chop = false;

        if (slicerAmount > 0.0f && preySource != ids::preySelf && (slicerInvert ? preyGap : preyStrike)
            && slicerRng.chance (slicerAmount * teeth))
        {
            if (onGrid)
                chop = true;
            else if (chopAt < 0.0)
                chopAt = nextGrid;
        }

        if (chopAt >= 0.0 && tl >= chopAt - 1.0e-9)
        {
            chop = true;
            chopAt = -1.0;
        }

        if (chop)
        {
            player.strike (now, spq, step, slicerRng.next());
            strikeFlash = 1.0f;
        }

        // Possess: a prey hit (or, flipped, a silence) fires one of the
        // Mimic's events, two or four steps long, ending on the grid. The
        // next hit may cut it short, but not before a step has played.
        bool possessNow = false;

        if (possessAmount > 0.0f && (possessInvert ? preyGap : preyStrike) && now >= possessNext
            && possessRng.chance (juce::jmin (1.0f, 1.3f * possessAmount) * teeth))
        {
            if (onGrid)
                possessNow = true;
            else if (possessAt < 0.0)
                possessAt = nextGrid;
        }

        if (possessAt >= 0.0 && tl >= possessAt - 1.0e-9)
        {
            possessNow = true;
            possessAt = -1.0;
        }

        if (possessNow)
        {
            GenSettings e = g;
            e.intensity = juce::jmax (0.35f, intensity);
            e.offsetInBar = tl - barStart;
            e.fill = false;
            const double steps = possessRng.chance (0.3f + 0.5f * possessAmount) ? 4.0 : 2.0;
            const double late = (gridSteps - std::floor (gridSteps + 1.0e-6)) * step;
            const double length = juce::jmin (barLen, steps * step - juce::jmax (0.0, late));
            auto event = makeEvent (e, length, possessRng.next());

            if (event.kind != Kind::pass)
            {
                const auto samples = (int64_t) (length * spq);
                player.begin (event, now, spq, samples);
                possessEnd = now + samples;
                possessNext = now + (int64_t) (step * spq * 0.9);
                segmentIndex = -2;
                possessFlash = 1.0f;
                lastPossessPpq.store (tl, std::memory_order_relaxed);
                possessCount.fetch_add (1, std::memory_order_relaxed);
            }
        }

        float wl, wr;
        Player::Weights weights;
        player.render (now, inL, inR, wl, wr, weights);
        const float passWeight = weights.pass;

        // Replace swaps the track for the fill; Add lays the fill on top.
        replaceMix = replace ? juce::jmin (1.0f, replaceMix + rampStep) : juce::jmax (0.0f, replaceMix - rampStep);
        const float fxL = wl - inL * passWeight, fxR = wr - inR * passWeight;
        // A Break silences the track in Add mode too.
        const float keepDry = 1.0f - juce::jmin (1.0f, weights.rest);
        float sl = replaceMix * wl + (1.0f - replaceMix) * (inL * keepDry + 0.55f * fxL);
        float sr2 = replaceMix * wr + (1.0f - replaceMix) * (inR * keepDry + 0.55f * fxR);

        // The echoes (granular delay and reverb throw) duck in a Break, so
        // the hole is heard.
        restDuck += restDuckCoeff * ((1.0f - 0.95f * juce::jmin (1.0f, weights.rest) * engage) - restDuck);

        // Echo: while an echo segment plays, the track is sent into the
        // granular delay; its repeats ring on after it.
        float echoL = 0.0f, echoR = 0.0f;
        echoSend = player.kind() == Kind::echo ? juce::jmin (1.0f, echoSend + echoRamp) : juce::jmax (0.0f, echoSend - echoRamp);

        if (echoSend > 0.0f)
            echoAlive = (int) (sr * 10.0);

        if (echoAlive > 0)
        {
            --echoAlive;
            echoDelay.process (inL * echoSend, inR * echoSend, echoL, echoR);
            echoL *= 0.7f * restDuck;
            echoR *= 0.7f * restDuck;

            if (echoAlive == 0)
                echoDelay.reset();
        }

        // Voice: the track speaks with the prey's voice, or (Carve) makes
        // room for it. Needs an audio prey on the side input.
        const bool voiceWorks = voiceAmount > 0.0f && preySource == ids::preySidechain && scL != nullptr;
        voiceMix += voiceStep * ((voiceWorks ? voiceAmount * teeth : 0.0f) - voiceMix);

        if (voiceWorks || voiceMix > 1.0e-4f)
        {
            float vl = sl, vr = sr2;
            vocoder.process (vl, vr, preyMono, voiceInvert);
            sl += voiceMix * (vl - sl);
            sr2 += voiceMix * (vr - sr2);
        }

        // Shaper: the prey's envelope shapes the level.
        if (shaperAmount > 0.0f)
        {
            const float a = shaperAmount * teeth;
            const float e = shaperInvert ? 1.0f - envN : envN;
            const float gain = 1.0f - a + a * std::pow (juce::jlimit (0.0f, 1.0f, e), 0.7f);
            sl *= gain;
            sr2 *= gain;
        }

        // Filter: the prey's brightness opens the low-pass.
        if (filterAmount > 0.0f)
        {
            if (--filterCountdown <= 0)
            {
                filterCountdown = 16;
                const float b = filterInvert ? 1.0f - preyBright : preyBright;
                toothFilter.set (20000.0f * std::exp2 (-filterAmount * teeth * 7.5f * (1.0f - b)), 0.7071f, sr);
            }

            // Fades in as the tooth starts to bite, so a resting filter is
            // fully transparent.
            const float amount = juce::jlimit (0.0f, 1.0f, filterAmount * teeth * 8.0f);
            const float fl = toothFilter.process (0, sl, 0), fr = toothFilter.process (1, sr2, 0);
            sl += amount * (fl - sl);
            sr2 += amount * (fr - sr2);
        }

        // Pitcher: the track follows the MIDI prey's notes (C3 = unchanged).
        if (pitcherAmount > 0.0f && preySource == ids::preyMidi)
        {
            const float semis = midiNote >= 0 ? (float) (midiNote - 60) * (pitcherInvert ? -1.0f : 1.0f) : 0.0f;
            pitchRatio += glideCoeff * (std::exp2 (juce::jlimit (-24.0f, 24.0f, semis) / 12.0f) - pitchRatio);
            float pl = sl, pr = sr2;
            shifter.process (pl, pr, pitchRatio);
            const float a = pitcherAmount * juce::jmin (1.0f, 2.0f * teeth);
            sl += a * (pl - sl);
            sr2 += a * (pr - sr2);
        }

        // The Throat: a resonant filter. Move takes the cutoff to a new
        // place on every grid step; Glide sets whether it jumps there or
        // slides all the way. Through a fill it sweeps shut (or open,
        // high-pass).
        if (throatType != ids::filterOff)
        {
            const auto stepIndex = (phraseBar + passSalt) * 4096 + (int64_t) std::floor ((tl - barStart) / step + 1.0e-6);

            if (stepIndex != throatStep)
            {
                throatStep = stepIndex;
                const auto h = hash64 ((uint64_t) stepIndex, (uint64_t) seedValue + 0x7407ull);
                throatFrom = throatFrom + throatRamp * (throatJump - throatFrom);
                throatRamp = 0.0f;
                throatRampStep = filterGlide < 0.005f ? 1.0f : 1.0f / (float) juce::jmax (1.0, filterGlide * step * spq);

                // Hold the last place now and then, so it moves in phrases.
                if (unit (h) < 0.65f)
                    throatJump = 2.0f * unit (hash64 (h)) - 1.0f;
            }

            throatRamp = juce::jmin (1.0f, throatRamp + throatRampStep);
            const float place = throatFrom + throatRamp * (throatJump - throatFrom);
            float octaves = filterMove * 3.0f * place;

            if (inFill)
                octaves += (throatType == ids::filterHigh ? 1.0f : -1.0f) * filterMove * 4.0f * fillProgress;

            throatOctaves += throatGlide * (octaves - throatOctaves);

            if (--throatCountdown <= 0)
            {
                throatCountdown = 8;
                const float hzNow = juce::jlimit (30.0f, 18000.0f, cutoffHz * std::exp2 (throatOctaves));
                throat.set (hzNow, 0.6f + resonance * resonance * 14.0f, sr);
            }

            const int type = throatType == ids::filterLow ? 0 : (throatType == ids::filterHigh ? 1 : 2);
            sl = throat.process (0, sl, type);
            sr2 = throat.process (1, sr2, type);

            // A screaming resonance is caught softly instead of clipping.
            if (resonance > 0.5f)
            {
                sl = std::tanh (sl);
                sr2 = std::tanh (sr2);
            }
        }

        // Anticipation: a high-pass that rises into the fill, then lets go.
        if (anticipation > 0.0f || riserLevel > 1.0e-4f)
        {
            riserLevel += riserCoeff * (antProgress - riserLevel);
            riser.set (20.0f * std::exp2 (riserLevel * 5.5f), 0.9f, sr);
            sl = riser.process (0, sl, 1);
            sr2 = riser.process (1, sr2, 1);
        }

        // Bass-safe: the side signal loses everything below the crossover,
        // so the lows are mono. The mid is not touched at all (no phase
        // shift), and mono material passes unchanged.
        if (bassActive)
        {
            const float m = 0.5f * (sl + sr2);
            float side = 0.5f * (sl - sr2);
            side -= sideLow.process (0, side, 0);
            sl = m + side;
            sr2 = m - side;
        }

        // Level: wild settings never come out louder than the track.
        dryPower += powerCoeff * (mono * mono - dryPower);
        const float wetMono = 0.5f * (sl + sr2);
        wetPower += powerCoeff * (wetMono * wetMono - wetPower);
        const float compTarget = juce::jlimit (0.3f, wild ? 1.1f : 1.0f, std::sqrt ((dryPower + 1.0e-9f) / (wetPower + 1.0e-9f)));
        compGain += compSmooth * (compTarget - compGain);
        sl *= compGain;
        sr2 *= compGain;

        // Peaks too: repeats of a hit must not poke out above the track.
        const float dryAbs = juce::jmax (std::abs (inL), std::abs (inR));
        dryPeak = dryAbs > dryPeak ? dryAbs : dryPeak + peakRelease * (dryAbs - dryPeak);
        const float wetAbs = juce::jmax (std::abs (sl), std::abs (sr2));
        wetPeak = wetAbs > wetPeak ? wetPeak + peakAttack * (wetAbs - wetPeak) : wetPeak + peakRelease * (wetAbs - wetPeak);
        const float ceiling = juce::jmax (dryPeak * 1.12f, 1.0e-4f);

        if (wetPeak > ceiling)
        {
            const float limit = ceiling / wetPeak;
            sl *= limit;
            sr2 *= limit;
        }

        // How much of the output is the Mimic. In Fill mode, between fills,
        // this is exactly 0 and the track passes untouched.
        const bool engaged = wild || inFill || antProgress > 0.0f || lure > 0.001f;
        engage = engaged ? juce::jmin (1.0f, engage + engageStep) : juce::jmax (0.0f, engage - engageStep);
        mixSmoothed += 0.002f * ((wild ? mix : 1.0f) - mixSmoothed);

        const float dl = inL, dr = inR;

        const float k = engage * mixSmoothed;
        const float yl = dl * (1.0f - k) + sl * k + echoL, yr = dr * (1.0f - k) + sr2 * k + echoR;
        left[i] = yl * outGain;

        if (right != nullptr)
            right[i] = yr * outGain;

        // Throw: pick a hit now and then and send it into the reverb.
        if (throwAmount > 0.0f || verbAlive > 0)
        {
            const bool hit = throwDetector.process (0.5f * (yl + yr));

            if (afterFill > 0)
                --afterFill;

            if (hit && throwAmount > 0.0f && sendHold <= 0)
            {
                const float chance = afterFill > 0 ? throwAmount
                                   : engage > 0.5f ? throwAmount * (wild ? 0.2f : 0.35f) : 0.0f;

                if (throwRng.chance (chance))
                {
                    sendHold = (int) (0.5 * spq);   // an eighth note of the hit
                    afterFill = 0;
                    verbAlive = (int) ((2.0f * (2.0f + 4.0f * throwAmount) + 1.0f) * (float) sr);
                    throwFlash = 1.0f;
                }
            }

            sendEnv = sendHold > 0 ? juce::jmin (1.0f, sendEnv + sendAttack) : juce::jmax (0.0f, sendEnv - sendRelease);

            if (sendHold > 0)
                --sendHold;

            if (sendCount == 0)
                sendStart = i;

            sendL[(size_t) sendCount] = yl * sendEnv;
            sendR[(size_t) sendCount] = yr * sendEnv;
            duckBuf[(size_t) sendCount] = restDuck;

            if (++sendCount == throwChunk)
                flushThrow (left, right, sendStart, sendCount, throwReturn * outGain);
        }
        else if (sendCount > 0)
        {
            flushThrow (left, right, sendStart, sendCount, throwReturn * outGain);
        }

        if (inFill || antProgress > 0.0f)
        {
            fillShown = inFill;
            progressShown = inFill ? 0.5f + 0.5f * fillProgress : 0.5f * antProgress;
        }

        strikeFlash *= 0.9995f;
        possessFlash *= 0.9995f;
    }

    if (sendCount > 0)
        flushThrow (left, right, sendStart, sendCount, throwReturn * outGain);

    throwFlash *= 0.8f;
    throwOut.store (throwFlash, std::memory_order_relaxed);

    expectedPpq = base + numSamples * ppqPerSample;
    shownBar.store ((int) (lastRawBar + 1), std::memory_order_relaxed);
    shownPhraseBar.store ((int) (lastRawBar + barShift), std::memory_order_relaxed);
    freePpq = playing ? expectedPpq : freePpq + numSamples * ppqPerSample;

    // For the editor.
    materialOut.store (classifier.material(), std::memory_order_relaxed);
    clarityOut.store (classifier.clarity, std::memory_order_relaxed);
    sustainOut.store (classifier.sustain, std::memory_order_relaxed);
    onsetRateOut.store (classifier.onsetRate, std::memory_order_relaxed);
    preyOut.store (preyPeak > 1.0e-4f ? juce::jlimit (0.0f, 1.0f, preyEnv / preyPeak) : 0.0f, std::memory_order_relaxed);
    fillProgressOut.store (progressShown, std::memory_order_relaxed);
    inFillOut.store (fillShown, std::memory_order_relaxed);
    engageOut.store (engage, std::memory_order_relaxed);
    kindOut.store ((int) player.kind(), std::memory_order_relaxed);
    barsToFillOut.store (wild ? -1 : barsToFill, std::memory_order_relaxed);
    shownPosition.store (spanKey != std::numeric_limits<int64_t>::min() ? (float) (expectedPpq - spanStart) : -1.0f, std::memory_order_relaxed);
    pitchDriftOut.store (classifier.pitchDrift, std::memory_order_relaxed);

    const float teethShown = wild || fillShown ? 1.0f : lure;
    toothOut[0].store (strikeFlash, std::memory_order_relaxed);
    toothOut[1].store (shaperAmount * teethShown, std::memory_order_relaxed);
    toothOut[2].store (filterAmount * teethShown, std::memory_order_relaxed);
    toothOut[3].store (preySource == ids::preyMidi ? pitcherAmount * teethShown : 0.0f, std::memory_order_relaxed);
    toothOut[4].store (preySource == ids::preySidechain ? voiceMix : 0.0f, std::memory_order_relaxed);
    toothOut[5].store (possessFlash, std::memory_order_relaxed);
}

void MimicProcessor::flushThrow (float* left, float* right, int start, int count, float returnGain)
{
    if (verbAlive <= 0)
    {
        sendCount = 0;
        return;
    }

    throwVerb.process (sendL.data(), sendR.data(), verbL.data(), verbR.data(), count);

    for (int n = 0; n < count; ++n)
    {
        left[start + n] += verbL[(size_t) n] * returnGain * duckBuf[(size_t) n];

        if (right != nullptr)
            right[start + n] += verbR[(size_t) n] * returnGain * duckBuf[(size_t) n];
    }

    sendCount = 0;
    verbAlive = juce::jmax (0, verbAlive - count);

    // The tail has died away: stop the reverb until the next throw.
    if (verbAlive == 0)
        throwVerb.reset();
}

void MimicProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("keptKey", (juce::int64) keptKey.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MimicProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        keptKey.store ((int64_t) (juce::int64) state.getProperty ("keptKey", 0));
        keepWasOn = (float) state.getChildWithProperty ("id", juce::String (ids::keep)).getProperty ("value", 0.0) > 0.5f;
        parameters.replaceState (state);
    }
}

bool MimicProcessor::hasEditor() const
{
   #if MIMIC_HEADLESS
    return false;
   #else
    return true;
   #endif
}

juce::AudioProcessorEditor* MimicProcessor::createEditor()
{
   #if MIMIC_HEADLESS
    return nullptr;
   #else
    return new MimicEditor (*this);
   #endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MimicProcessor();
}
