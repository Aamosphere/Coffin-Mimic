#include "Engine.h"

namespace mimic
{
    namespace
    {
        float coeff (double seconds, double sampleRate)
        {
            return 1.0f - std::exp (-1.0f / (float) (seconds * sampleRate));
        }

        // How likely each content kind is for each material, in the order of
        // contentKinds: stutter, roll, reverse, scatter, tape, gate, grains, slice, echo.
        constexpr float contentWeights[numMaterials][numContent] {
            { 3.0f, 3.0f, 1.0f, 2.0f, 0.8f, 2.0f, 0.8f, 3.0f, 0.8f },   // percussive
            { 2.5f, 1.5f, 2.0f, 1.5f, 2.0f, 1.0f, 1.5f, 2.0f, 1.8f },   // melodic
            { 1.5f, 0.4f, 3.0f, 1.0f, 2.0f, 2.5f, 3.0f, 1.0f, 2.0f },   // harmonic
            { 3.5f, 1.0f, 2.5f, 1.5f, 2.0f, 1.5f, 2.5f, 1.5f, 2.2f }    // voice: chops, swells, choirs
        };

        bool isEnabled (const GenSettings& g, Kind kind)
        {
            for (int i = 0; i < numContent; ++i)
                if (contentKinds[i] == kind)
                    return ((g.enabled >> i) & 1u) != 0;

            return false;
        }

        Kind pickKind (Rng& r, const GenSettings& g, bool climax, Kind avoid, bool allowSlice)
        {
            float w[numContent];
            float total = 0.0f;

            for (int i = 0; i < numContent; ++i)
            {
                const auto kind = contentKinds[i];
                float weight = (g.enabled >> i) & 1u ? contentWeights[juce::jlimit (0, numMaterials - 1, g.material)][i] : 0.0f;

                if (kind == Kind::slice && ! allowSlice)
                    weight = 0.0f;

                if (climax && (kind == Kind::roll || (g.material != percussive && kind == Kind::tape)))
                    weight *= 3.0f;

                if (kind == avoid)
                    weight *= 0.35f;

                w[i] = weight;
                total += weight;
            }

            if (total <= 0.0f)
                return Kind::pass;

            float x = r.uni() * total;

            for (int i = 0; i < numContent; ++i)
            {
                if (x < w[i])
                    return contentKinds[i];

                x -= w[i];
            }

            return Kind::pass;
        }

        // Note values in quarter notes, long to short: 1/4, dotted 1/8, 1/8,
        // 1/8T, 1/16, 1/16T, 1/32.
        constexpr double noteValues[] { 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };

        // A repeat length that is a real note value: the wilder, the shorter.
        // Melodies and pads lean to longer, echo-like values.
        double pickRepeat (Rng& r, float I, int material)
        {
            const float lean = material == percussive ? 0.6f : (material == melodic ? -0.4f : (material == vocal ? 0.0f : -0.8f));
            // Bite moves the values only a little: mostly eighths and
            // sixteenths, whatever the Bite.
            int index = juce::jlimit (0, 6, juce::roundToInt (1.8f + lean + I * 1.6f + r.range (-0.8f, 0.8f)));

            if (index == 6 && I < 0.85f)
                index = 4;                                  // 1/32 only when it bites hard

            if ((index == 3 || index == 5) && ! r.chance (0.25f + 0.2f * I))
                index = index == 3 ? 2 : 4;                 // triplets now and then

            return noteValues[index];
        }

        // Arpeggio shapes for melodic stutters, in semitones per repeat.
        constexpr int8_t arps[][4] {
            { 0, 12, 7, 19 }, { 0, 7, 12, 7 }, { 0, -12, 0, 12 }, { 0, 3, 7, 12 }, { 0, 5, 7, 12 }, { 12, 7, 0, -5 }
        };

        // Vocal chops: the syllable repeats, now and then an octave or a
        // fifth away, like a chopped-up acapella.
        constexpr int8_t chops[][4] {
            { 0, 0, 12, 12 }, { 0, 7, 0, 12 }, { 0, 0, 0, -12 }, { 12, 7, 0, 0 }, { 0, 12, 0, 12 }
        };

        // Where a grid piece of the track comes from: a step of this bar
        // that has already played, or else the same step of the bar before.
        double sliceBack (double destInBar, double sourceInBar, double sourceLength, double bar)
        {
            double back = destInBar - sourceInBar;

            while (back < sourceLength - 1.0e-9)
                back += bar;

            return back;
        }

        Segment makeSegment (Kind kind, double start, double length, float intensity, const GenSettings& g, Rng& r)
        {
            const int material = g.material;

            // Each material has its own versions of some switches. (Drums
            // keep theirs, and their scripts stay exactly as they were.)
            if (kind == Kind::stutter && material == melodic && r.chance (0.55f))
                kind = Kind::shards;
            else if (kind == Kind::stutter && material == harmonic && r.chance (0.5f))
                kind = Kind::fog;
            else if (kind == Kind::grains && material == harmonic && r.chance (0.6f))
                kind = Kind::fog;
            else if (kind == Kind::roll && (material == melodic || material == vocal) && r.chance (0.6f))
                kind = Kind::bend;

            Segment s;
            s.start = start;
            s.length = length;
            s.kind = kind;
            s.step = g.step;
            s.swing = g.swing;
            s.seed = r.next();
            s.reach = g.bar * (double) (1 + r.below (intensity > 0.5f ? 2 : 1));
            const float I = intensity;
            const int m = g.material;

            switch (kind)
            {
                case Kind::stutter:
                {
                    s.repeat = juce::jmin (length, pickRepeat (r, I, m));

                    if (m == vocal)
                    {
                        // A syllable chopped on eighths or sixteenths.
                        s.repeat = juce::jmin (length, r.chance (0.35f + 0.4f * I) ? 0.25 : 0.5);

                        if (r.chance (0.25f + 0.45f * I))
                        {
                            const auto& shape = chops[r.below ((int) std::size (chops))];
                            std::copy (std::begin (shape), std::end (shape), s.arp.begin());
                            s.arpLength = 4;
                        }
                        else if (r.chance (0.3f))
                        {
                            s.repeat = juce::jmin (length, r.chance (0.5f) ? 0.75 : 0.5);
                            s.decay = r.range (0.65f, 0.85f);   // a vocal echo
                        }
                    }
                    else if (m == melodic && I > 0.4f && r.chance (0.3f + 0.3f * I))
                    {
                        // Each repeat jumps to the next note of an arpeggio.
                        const auto& shape = arps[r.below ((int) std::size (arps))];
                        std::copy (std::begin (shape), std::end (shape), s.arp.begin());
                        s.arpLength = 2 + r.below (3);
                        s.repeat = juce::jmin (length, I > 0.7f ? 0.25 : 0.5);
                    }
                    else if (m != percussive && r.chance (0.4f))
                    {
                        // Echo: dotted or straight eighths fading out.
                        s.repeat = juce::jmin (length, r.chance (0.5f) ? 0.75 : 0.5);
                        s.decay = r.range (0.6f, 0.82f);
                    }
                    else if (I > 0.7f && r.chance (0.25f))
                    {
                        s.rateStart = s.rateEnd = r.chance (0.7f) ? 0.5f : 2.0f;   // an octave
                    }

                    if (s.decay >= 1.0f && r.chance (0.35f))
                        s.decay = r.range (0.8f, 0.95f);

                    s.punch = s.repeat <= 0.25 + 1.0e-9;
                    break;
                }

                case Kind::roll:
                    // A drummer's roll: one hit per step, then two, then four.
                    s.repeat = juce::jmin (length, I > 0.8f ? 0.25 : 0.5);
                    s.minRepeat = I > 0.95f ? 0.0625 : 0.125;
                    s.gainStart = r.range (0.5f, 0.75f);
                    s.punch = true;

                    if (m == percussive && I > 0.5f && r.chance (0.4f))
                        s.rateEnd = std::exp2 ((float) (2 + r.below (6)) / 12.0f);
                    break;

                case Kind::reverse:
                    // A voice is reversed whole: a swell into the next word.
                    s.repeat = (m != vocal && length > 0.5 && r.chance (0.3f + 0.3f * I)) ? (r.chance (0.5f) ? 0.5 : 1.0) : length;
                    s.repeat = juce::jmin (length, s.repeat);

                    if (r.chance (0.2f * I))
                        s.rateStart = s.rateEnd = 0.5f;
                    break;

                case Kind::scatter:
                    s.repeat = juce::jmin (length, m == percussive ? g.step : g.step * 2.0);
                    s.reverseChance = (m == harmonic ? 0.3f : 0.15f) * I;
                    s.octaveChance = 0.15f * I;   // half speed
                    break;

                case Kind::tape:
                    s.repeat = length;

                    if (m != percussive && r.chance (m == melodic ? 0.6f : 0.4f))
                        s.rateEnd = r.chance (0.5f) ? 2.0f : 0.5f;   // glide
                    else
                    {
                        s.rateEnd = 0.0f;                           // stop
                        s.gainEnd = 0.35f;
                    }
                    break;

                case Kind::gate:
                    s.repeat = r.chance (0.15f) ? 1.0 / 6.0 : (I > 0.5f && r.chance (0.5f) ? g.step * 0.5 : g.step);
                    s.repeat = juce::jmax (0.125, s.repeat);
                    s.density = m == harmonic ? r.range (0.55f, 0.85f) : r.range (0.4f, 0.8f);
                    break;

                case Kind::grains:
                    // Grains on the grid: each one a note value long.
                    if (m == percussive)
                    {
                        s.grainNote = r.chance (0.3f + 0.4f * I) ? 0.125 : 0.25;
                        s.grainOverlap = 1.0f;
                        s.taper = 0.15f;
                        s.repeat = 1.0;
                        s.octaveChance = 0.1f * I;
                    }
                    else if (m == melodic)
                    {
                        s.grainNote = r.chance (0.5f) ? 0.25 : 0.5;
                        s.grainOverlap = 1.5f;
                        s.taper = 0.5f;
                        s.repeat = 1.0;
                        s.octaveChance = 0.3f + 0.2f * I;
                    }
                    else if (m == vocal)
                    {
                        // A choir: the voice frozen, with octaves around it.
                        s.grainNote = r.chance (0.5f) ? 0.25 : 0.5;
                        s.grainOverlap = 2.0f;
                        s.taper = 1.0f;
                        s.repeat = 0.25;
                        s.octaveChance = 0.45f + 0.3f * I;
                    }
                    else
                    {
                        s.grainNote = r.chance (0.5f) ? 0.5 : 1.0;
                        s.grainOverlap = 2.0f;
                        s.taper = 1.0f;
                        s.repeat = r.chance (0.5f) ? 0.25 : 2.0;   // nearly frozen, or drifting
                        s.octaveChance = 0.4f + 0.3f * I;
                    }

                    s.density = s.grainOverlap;
                    s.reverseChance = (m == harmonic || m == vocal ? 0.3f : 0.15f) * I;
                    break;

                case Kind::slice:
                {
                    // A grid piece from elsewhere in the last bar.
                    const int barSteps = juce::jmax (1, juce::roundToInt (g.bar / g.step));
                    const int lengthSteps = juce::jmax (1, juce::roundToInt (length / g.step));
                    const int slots = juce::jmax (1, barSteps / lengthSteps);
                    const double source = (double) (r.below (slots) * lengthSteps) * g.step;
                    const bool half = r.chance (0.15f * I);

                    if (half)
                        s.rateStart = s.rateEnd = 0.5f;

                    s.reverseChance = r.chance (0.15f * I) ? 1.0f : 0.0f;
                    s.back = sliceBack (g.offsetInBar + start, source, length * s.rateStart, g.bar);
                    break;
                }

                case Kind::shards:
                {
                    // The loop with itself an octave up (twice as fast) and
                    // an octave down or a fifth up, the layers coming in
                    // one loop after another.
                    s.repeat = juce::jmin (length, r.chance (0.5f) ? 0.5 : 0.25);
                    s.arp = { 0, 12, (int8_t) (r.chance (0.5f) ? -12 : 7), 0 };
                    s.arpLength = 0;   // the layers, not an arpeggio
                    break;
                }

                case Kind::bend:
                {
                    // A loop that glides to a new pitch over the segment.
                    constexpr int8_t targets[] { 12, 7, -12, -5, 5 };
                    s.repeat = juce::jmin (length, r.chance (0.5f) ? 0.25 : 0.5);
                    s.rateEnd = std::exp2 ((float) targets[r.below ((int) std::size (targets))] / 12.0f);
                    s.decay = r.range (0.9f, 1.0f);
                    break;
                }

                case Kind::fog:
                    // A haze: short, heavily overlapped, slightly detuned
                    // grains from the last beat, some an octave away, some
                    // backwards.
                    s.grainNote = 0.125;
                    s.grainOverlap = 3.0f;
                    s.taper = 1.0f;
                    s.repeat = 1.0;
                    s.octaveChance = 0.25f + 0.25f * I;
                    s.reverseChance = 0.35f;
                    s.density = s.grainOverlap;
                    break;

                case Kind::echo:
                case Kind::pass:
                case Kind::rest:
                    break;
            }

            // Filter sweeps: a closing low-pass, or a high-pass opening up
            // like a riser.
            if (kind != Kind::slice && kind != Kind::echo && r.chance ((m == harmonic ? 0.2f : 0.1f) + 0.2f * I))
            {
                if (r.chance (0.5f))
                    s.cutEnd = r.range (0.3f, 0.6f);
                else
                {
                    s.highPass = true;
                    s.cutEnd = r.range (0.25f, 0.5f);
                }
            }

            return s;
        }

        Segment passSegment (double start, double length)
        {
            Segment s;
            s.start = start;
            s.length = length;
            return s;
        }

        Segment restSegment (double start, double length)
        {
            Segment s = passSegment (start, length);
            s.kind = Kind::rest;
            return s;
        }

        // Grid position of step i, with the odd steps of each pair late.
        double gridAt (int i, double step, float swing)
        {
            return (double) (i / 2) * 2.0 * step + ((i & 1) ? step * (1.0 + swing) : 0.0);
        }

        void addOrMerge (Script& script, const Segment& s)
        {
            if (script.count > 0)
            {
                auto& last = script.segments[(size_t) script.count - 1];

                const bool same = last.kind == s.kind
                               && (s.kind == Kind::pass || s.kind == Kind::rest
                                   || (s.kind == Kind::slice && std::abs (last.back - s.back) < 1.0e-6
                                       && last.rateStart == 1.0f && s.rateStart == 1.0f
                                       && last.reverseChance < 0.5f && s.reverseChance < 0.5f));

                if (same)
                {
                    last.length += s.length;
                    return;
                }
            }

            script.add (s);
        }
    }

    //==========================================================================
    int Script::find (double position) const
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& s = segments[(size_t) i];

            if (position >= s.start - 1.0e-9 && position < s.start + s.length - 1.0e-9)
                return i;
        }

        return -1;
    }

    uint64_t Script::signature() const
    {
        uint64_t h = hash64 ((uint64_t) count);

        for (int i = 0; i < count; ++i)
        {
            const auto& s = segments[(size_t) i];
            h = hash64 (h, (uint64_t) s.kind);
            h = hash64 (h, (uint64_t) std::llround (s.start * 64.0));
            h = hash64 (h, (uint64_t) std::llround (s.repeat * 64.0));
        }

        return h;
    }

    Segment makeEvent (const GenSettings& g, double length, uint64_t seed)
    {
        Rng r (seed);
        const float I = std::pow (juce::jlimit (0.0f, 1.0f, g.intensity), 1.35f);
        const auto kind = pickKind (r, g, false, Kind::pass, true);

        if (kind == Kind::pass)
            return passSegment (0.0, length);

        return makeSegment (kind, 0.0, length, I, g, r);
    }

    void generate (Script& script, const GenSettings& g, uint64_t seed)
    {
        script.clear();
        Rng r (seed);
        // A curve: the low half of Bite stays gentle.
        const float I = std::pow (juce::jlimit (0.0f, 1.0f, g.intensity), 1.35f);
        const float swing = juce::jlimit (0.0f, 0.5f, g.swing);
        double step = juce::jmax (1.0 / 64.0, g.step);

        while (g.span / step > 64.0 + 1.0e-6)
            step *= 2.0;

        GenSettings gs = g;
        gs.step = step;
        int spanSteps = juce::jmax (1, (int) std::floor (g.span / step + 1.0e-6));
        const int totalSteps = spanSteps;
        Kind previous = Kind::pass;
        int at = 0;

        // Start and end of steps on the swung grid; the last step always
        // reaches the end of the span.
        auto startOf = [&] (int i) { return gridAt (i, step, swing); };
        auto endOf = [&] (int i) { return i >= totalSteps ? g.span : gridAt (i, step, swing); };

        auto addRest = [&] (int from, int steps)
        {
            addOrMerge (script, restSegment (startOf (from), endOf (from + steps) - startOf (from)));
        };

        // Breaks: holes in the beat, an eighth (or a step, if longer) at a
        // time, mostly on the offbeats. They do not depend on Bite, and
        // their own seed leaves the rest of the script as it was.
        std::array<uint8_t, 64> restAt {};

        if (g.breaks > 0.0f)
        {
            Rng br (hash64 (seed, 0xb7eaull));
            const int unit = juce::jmax (1, juce::roundToInt (0.5 / step));
            const int offsetUnits = juce::roundToInt (g.offsetInBar / step);

            for (int i = 0; i < spanSteps; ++i)
            {
                const int inBar = offsetUnits + i;

                if (inBar % unit != 0)
                    continue;

                const bool downbeat = inBar % juce::jmax (1, juce::roundToInt (g.bar / step)) == 0;
                const bool offbeat = (inBar / unit) % 2 == 1;
                const float chance = g.breaks * (downbeat ? 0.08f : (offbeat ? 0.38f : 0.22f));

                if (br.chance (chance))
                {
                    const int length = (g.breaks > 0.6f && br.chance (0.3f)) ? 2 * unit : unit;
                    restAt[(size_t) i] = (uint8_t) juce::jmin (length, spanSteps - i);
                }
            }
        }

        if (g.fill)
        {
            // A Break right before the downbeat: the fill ends in silence.
            const bool endRest = spanSteps > 2 && r.chance (g.breaks * 0.7f);

            if (endRest)
                --spanSteps;

            // Phrases that get shorter and wilder towards the end of the fill.
            while (at < spanSteps)
            {
                if (restAt[(size_t) at] > 0 && at > 0)
                {
                    const int rest = juce::jmin ((int) restAt[(size_t) at], spanSteps - at);
                    addRest (at, rest);
                    at += rest;
                    previous = Kind::rest;
                    continue;
                }

                const int remaining = spanSteps - at;
                const int longest = juce::jmax (1, juce::roundToInt (juce::jmap (I, (float) spanSteps, 2.0f)));
                int steps = juce::jlimit (1, remaining, 1 + r.below (longest));

                // A phrase stops where a Break is planned.
                for (int j = 1; j < steps; ++j)
                    if (restAt[(size_t) (at + j)] > 0)
                    {
                        steps = j;
                        break;
                    }

                if (steps >= 3 && (steps & 1) && r.chance (0.7f))
                    --steps;

                const bool last = steps == remaining;
                const double start = startOf (at);
                const double end = (last && ! endRest) ? g.span : endOf (at + steps);
                const float progress = (float) (end / g.span);
                const float local = I * (0.55f + 0.45f * progress);

                if (! last && r.chance (0.5f * (1.0f - I) * (1.0f - I)))
                {
                    addOrMerge (script, passSegment (start, end - start));
                    previous = Kind::pass;
                }
                else
                {
                    const auto kind = pickKind (r, gs, last && I > 0.45f, previous, true);
                    addOrMerge (script, kind == Kind::pass ? passSegment (start, end - start)
                                                           : makeSegment (kind, start, end - start, local, gs, r));
                    previous = kind;
                }

                at += steps;

                // Breaks: a step of silence after the chewing.
                if (previous != Kind::pass && at < spanSteps - 1 && r.chance (g.breaks * 0.6f))
                {
                    addRest (at, 1);
                    ++at;
                }
            }

            if (endRest)
                addRest (spanSteps, 1);

            return;
        }

        // Wild. First the masher: where each step of the bar comes from.
        // Pieces of an eighth (or a beat when calm, a step when wild) are
        // shifted early or late, swapped, repeated, reversed or slowed down,
        // so the snare does not always land in the same place - the one
        // included.
        const int barSteps = juce::jmax (1, juce::roundToInt (g.bar / step));
        const int offsetSteps = juce::roundToInt (g.offsetInBar / step);
        const int unitsPerBeat = juce::jmax (1, juce::roundToInt (g.beat / step));
        std::array<int, 64> map {}, chunk {};
        std::array<uint8_t, 64> flag {};    // 0 straight, 1 reversed, 2 half speed

        for (int i = 0; i < spanSteps; ++i)
            map[(size_t) i] = offsetSteps + i;

        auto wrap = [barSteps] (int v) { return ((v % barSteps) + barSteps) % barSteps; };

        if (isEnabled (g, Kind::slice))
        {
            const int ops = juce::roundToInt (I * (float) spanSteps * 0.22f + r.uni() * 1.5f * I);
            bool downbeatDone = false;

            for (int o = 0; o < ops; ++o)
            {
                const float x = r.uni();
                // Mostly eighths; single steps only when it bites hard.
                int length = x < 0.3f * (1.0f - I) ? unitsPerBeat
                           : (x < (I > 0.5f ? 0.8f : 1.0f) ? juce::jmax (1, unitsPerBeat / 2) : 1);
                length = juce::jmin (length, spanSteps);
                const int slots = spanSteps / length;

                if (slots < 1)
                    continue;

                int d = r.below (slots) * length;

                if (! downbeatDone && r.chance (0.08f + 0.3f * I))
                {
                    d = 0;
                    downbeatDone = true;
                }

                const float t = r.uni();

                // Any flagged piece the op lands on goes back to straight.
                for (int j = 0; j < length; ++j)
                {
                    flag[(size_t) (d + j)] = 0;
                    chunk[(size_t) (d + j)] = 0;
                }

                if (t < 0.35f)
                {
                    // The hit comes a sixteenth or an eighth early or late.
                    const int shift = (r.chance (0.6f) ? 1 : 2) * (r.chance (0.5f) ? 1 : -1) * juce::jmax (1, unitsPerBeat / 4);

                    for (int j = 0; j < length; ++j)
                        map[(size_t) (d + j)] = wrap (map[(size_t) (d + j)] + shift);
                }
                else if (t < 0.6f)
                {
                    const int e = r.below (slots) * length;

                    for (int j = 0; j < length; ++j)
                    {
                        std::swap (map[(size_t) (d + j)], map[(size_t) (e + j)]);
                        flag[(size_t) (e + j)] = 0;
                        chunk[(size_t) (e + j)] = 0;
                    }
                }
                else if (t < 0.85f || I < 0.5f)
                {
                    const int e = r.below (slots) * length;
                    std::array<int, 64> copy = map;

                    for (int j = 0; j < length; ++j)
                        map[(size_t) (d + j)] = copy[(size_t) (e + j)];
                }
                else
                {
                    // Reversed or half-speed piece, played as one segment.
                    flag[(size_t) d] = t < 0.93f ? 1 : 2;
                    chunk[(size_t) d] = length;
                }
            }
        }

        // Then events on top, sparser when the masher is doing the work.
        const float density = isEnabled (g, Kind::slice) ? 0.01f + 0.22f * std::pow (I, 1.6f)
                                                         : 0.02f + 0.55f * std::pow (I, 1.5f);

        while (at < spanSteps)
        {
            const float position = (float) at / (float) spanSteps;

            if (restAt[(size_t) at] > 0)
            {
                const int rest = juce::jmin ((int) restAt[(size_t) at], spanSteps - at);
                addRest (at, rest);
                at += rest;
                previous = Kind::rest;
                continue;
            }

            // Every beat of the bar gets its share, a little more towards
            // the end.
            if (r.chance (density * (0.8f + 0.4f * position)))
            {
                int steps = juce::jmin (spanSteps - at, 1 + r.below (I < 0.35f ? 2 : 3));

                for (int j = 1; j < steps; ++j)
                    if (restAt[(size_t) (at + j)] > 0)
                    {
                        steps = j;
                        break;
                    }
                const double start = startOf (at), end = endOf (at + steps);
                const auto kind = pickKind (r, gs, false, previous, false);

                if (kind != Kind::pass)
                {
                    addOrMerge (script, makeSegment (kind, start, end - start, I * r.range (0.75f, 1.1f), gs, r));
                    previous = kind;
                    at += steps;

                    if (at < spanSteps && r.chance (g.breaks * 0.7f))
                    {
                        const int rest = juce::jmin (spanSteps - at, 1 + r.below (2));
                        addRest (at, rest);
                        at += rest;
                    }

                    continue;
                }
            }

            // One step (or one flagged piece) from the map.
            int length = flag[(size_t) at] != 0 ? juce::jmin (chunk[(size_t) at], spanSteps - at) : 1;

            for (int j = 1; j < length; ++j)
                if (restAt[(size_t) (at + j)] > 0)
                {
                    length = j;
                    break;
                }
            const double start = startOf (at), end = endOf (at + length);
            const double destInBar = g.offsetInBar + start;

            if (flag[(size_t) at] == 0 && map[(size_t) at] == offsetSteps + at)
            {
                addOrMerge (script, passSegment (start, end - start));
                previous = Kind::pass;
            }
            else
            {
                Segment s = passSegment (start, end - start);
                s.kind = Kind::slice;
                s.step = step;
                s.seed = r.next();

                if (flag[(size_t) at] == 2)
                    s.rateStart = s.rateEnd = 0.5f;

                if (flag[(size_t) at] == 1)
                    s.reverseChance = 1.0f;

                s.back = sliceBack (destInBar, (double) map[(size_t) at] * step, (end - start) * s.rateStart, g.bar);
                addOrMerge (script, s);
                previous = Kind::slice;

                if (length > 1 && at + length < spanSteps && r.chance (g.breaks * 0.4f))
                {
                    at += length;
                    addRest (at, 1);
                    ++at;
                    continue;
                }
            }

            at += length;
        }
    }

    //==========================================================================
    void History::prepare (double sampleRate, double seconds)
    {
        const auto wanted = (int64_t) (sampleRate * seconds);
        int64_t size = 1;

        while (size < wanted)
            size <<= 1;

        for (auto& d : data)
            d.assign ((size_t) size, 0.0f);

        mask = size - 1;
    }

    void History::clear()
    {
        for (auto& d : data)
            std::fill (d.begin(), d.end(), 0.0f);
    }

    float History::read (int ch, double pos, int64_t now) const
    {
        const auto& d = data[ch];
        pos = juce::jlimit ((double) (now - size() + 8), (double) now, pos);

        const auto i = (int64_t) std::floor (pos);
        const auto f = (float) (pos - (double) i);
        auto at = [&] (int64_t j) { return d[(size_t) (j & mask)]; };

        if (f < 1.0e-5f)
            return at (i);

        if (i + 2 > now)
            return at (i) + f * (at (juce::jmin (i + 1, now)) - at (i));

        // Cubic Hermite.
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    //==========================================================================
    void OnsetDetector::prepare (double sampleRate)
    {
        hpCoeff = std::exp (-juce::MathConstants<float>::twoPi * 120.0f / (float) sampleRate);
        fastAtt = coeff (0.00015, sampleRate);
        fastRel = coeff (0.010, sampleRate);
        slowAtt = coeff (0.012, sampleRate);
        slowRel = coeff (0.150, sampleRate);
        holdoffSamples = (int) (0.035 * sampleRate);
        reset();
    }

    void OnsetDetector::reset()
    {
        hpX = hpY = fast = slow = 0.0f;
        holdoff = 0;
        armed = true;
    }

    bool OnsetDetector::process (float x)
    {
        hpY = hpCoeff * (hpY + x - hpX);
        hpX = x;
        const float a = std::abs (hpY);

        fast += (a > fast ? fastAtt : fastRel) * (a - fast);
        slow += (a > slow ? slowAtt : slowRel) * (a - slow);

        if (holdoff > 0)
            --holdoff;

        if (! armed && fast < slow * 1.25f)
            armed = true;

        if (armed && holdoff == 0 && fast > 0.003f && fast > slow * 1.9f)
        {
            armed = false;
            holdoff = holdoffSamples;
            return true;
        }

        return false;
    }

    void OnsetList::add (int64_t position)
    {
        times[(size_t) write] = position;
        write = (write + 1) % (int) times.size();
        count = juce::jmin (count + 1, (int) times.size());
    }

    double OnsetList::snap (double target, double window, double preroll, bool* found) const
    {
        for (int n = 0; n < count; ++n)
        {
            const int i = (write - 1 - n + (int) times.size()) % (int) times.size();
            const auto t = (double) times[(size_t) i];

            if (t > target)
                continue;

            if (t < target - window)
                break;

            if (found != nullptr)
                *found = true;

            return t - preroll;
        }

        if (found != nullptr)
            *found = false;

        return target;
    }

    bool OnsetList::near (double position, double tolerance, double limit, double& onset) const
    {
        for (int n = 0; n < count; ++n)
        {
            const int i = (write - 1 - n + (int) times.size()) % (int) times.size();
            const auto t = (double) times[(size_t) i];

            if (t > limit || t > position + tolerance)
                continue;

            if (t < position - tolerance)
                return false;

            onset = t;
            return true;
        }

        return false;
    }

    //==========================================================================
    void Classifier::prepare (double rate)
    {
        sampleRate = rate;
        decimation = juce::jmax (1, juce::roundToInt (rate / 11025.0));
        lpCoeff = coeff (1.0 / (juce::MathConstants<double>::twoPi * 3500.0), rate);
        envAtt = coeff (0.005, rate);
        envRel = coeff (0.060, rate);
        peakRel = coeff (1.5, rate);
        frame.assign (2048, 0.0f);
        reset();
    }

    void Classifier::reset()
    {
        decimCount = 0;
        decimAcc = lp1 = lp2 = 0.0f;
        frameFill = 0;
        env = peak = 0.0f;
        envSum = peakSum = 0.0;
        onsets = 0;
        frameSamples = 0;
        clarity = sustain = onsetRate = 0.0f;
        pitchDrift = pitchWobble = 0.0f;
        current = candidate = percussive;
        candidateSeconds = 0.0f;
        primed = false;
    }

    void Classifier::push (float mono, bool onset)
    {
        const float a = std::abs (mono);
        env += (a > env ? envAtt : envRel) * (a - env);
        peak = a > peak ? a : peak + peakRel * (a - peak);
        envSum += env;
        peakSum += peak;
        ++frameSamples;

        if (onset)
            ++onsets;

        lp1 += lpCoeff * (mono - lp1);
        lp2 += lpCoeff * (lp1 - lp2);

        if (++decimCount >= decimation)
        {
            decimCount = 0;
            frame[(size_t) frameFill++] = lp2;

            if (frameFill == (int) frame.size())
            {
                analyse();
                frameFill = 0;
            }
        }
    }

    float Classifier::nsdfPitch (int from, int length, double& hz) const
    {
        // McLeod: the first key maximum near the highest one is the period.
        const double fsd = sampleRate / decimation;
        const int minLag = juce::jmax (2, (int) (fsd / 1000.0));
        const int maxLag = juce::jmin (length / 2, (int) (fsd / 70.0));
        const float* x = frame.data() + from;
        float values[512] {};
        double m = 0.0;

        for (int i = 0; i < length; ++i)
            m += 2.0 * (double) x[i] * x[i];

        for (int tau = 1; tau <= maxLag && tau < 512; ++tau)
        {
            m -= (double) x[tau - 1] * x[tau - 1] + (double) x[length - tau] * x[length - tau];
            double r = 0.0;

            for (int i = 0; i < length - tau; ++i)
                r += (double) x[i] * x[i + tau];

            values[tau] = m > 1.0e-12 ? (float) (2.0 * r / m) : 0.0f;
        }

        float best = 0.0f;
        bool passedZero = false;

        for (int tau = 1; tau <= maxLag; ++tau)
        {
            passedZero = passedZero || values[tau] < 0.0f;

            if (passedZero && tau >= minLag)
                best = juce::jmax (best, values[tau]);
        }

        if (best < 0.5f)
            return best;

        passedZero = false;

        for (int tau = 1; tau < maxLag; ++tau)
        {
            passedZero = passedZero || values[tau] < 0.0f;

            if (passedZero && tau >= minLag && values[tau] >= 0.9f * best
                && values[tau] >= values[tau - 1] && values[tau] >= values[tau + 1])
            {
                const double a = values[tau - 1], b = values[tau], c = values[tau + 1];
                const double d = a - 2.0 * b + c;
                const double lag = tau + (std::abs (d) > 1.0e-12 ? 0.5 * (a - c) / d : 0.0);
                hz = fsd / lag;
                return best;
            }
        }

        return best;
    }

    void Classifier::analyse()
    {
        const int n = (int) frame.size();
        const double seconds = (double) frameSamples / sampleRate;
        double energy = 0.0;

        for (auto x : frame)
            energy += (double) x * x;

        const bool silent = std::sqrt (energy / n) < 3.0e-4;

        if (! silent)
        {
            // Normalised square difference function: 1 = perfectly periodic.
            const double fsd = sampleRate / decimation;
            const int minLag = juce::jmax (2, (int) (fsd / 1000.0));
            const int maxLag = juce::jmin (n / 2, (int) (fsd / 55.0));
            double m = 2.0 * energy;
            bool passedZero = false;
            float best = 0.0f;

            for (int tau = 1; tau <= maxLag; ++tau)
            {
                m -= (double) frame[(size_t) (tau - 1)] * frame[(size_t) (tau - 1)]
                   + (double) frame[(size_t) (n - tau)] * frame[(size_t) (n - tau)];

                double r = 0.0;

                for (int i = 0; i < n - tau; ++i)
                    r += (double) frame[(size_t) i] * frame[(size_t) (i + tau)];

                const float nsdf = m > 1.0e-12 ? (float) (2.0 * r / m) : 0.0f;

                if (nsdf < 0.0f)
                    passedZero = true;

                if (passedZero && tau >= minLag)
                    best = juce::jmax (best, nsdf);
            }

            // The pitch of short stretches: how far off the scale it sits,
            // and how much it moves while a note is held.
            constexpr int part = 384;
            double previousCents = 0.0;
            bool havePrevious = false;
            float driftSum = 0.0f, wobbleSum = 0.0f;
            int driftCount = 0, wobbleCount = 0;

            for (int from = 0; from + part <= n; from += part / 2)
            {
                double hz = 0.0;
                const float c = nsdfPitch (from, part, hz);

                if (c < 0.8f || hz <= 0.0)
                {
                    havePrevious = false;
                    continue;
                }

                const double cents = 1200.0 * std::log2 (hz / 440.0);
                driftSum += (float) std::abs (cents - 100.0 * std::round (cents / 100.0));
                ++driftCount;

                if (havePrevious && std::abs (cents - previousCents) < 80.0)
                {
                    wobbleSum += (float) std::abs (cents - previousCents);
                    ++wobbleCount;
                }

                previousCents = cents;
                havePrevious = true;
            }

            const float frameSustain = (float) (envSum / juce::jmax (1.0e-9, peakSum));
            const float frameRate = (float) (onsets / juce::jmax (1.0e-3, seconds));
            const float a = primed ? 0.25f : 1.0f;
            clarity += a * (best - clarity);
            sustain += a * (frameSustain - sustain);
            onsetRate += a * (frameRate - onsetRate);

            // Unvoiced frames (consonants, gaps) leave the pitch features as
            // they were.
            if (driftCount >= 2)
            {
                pitchDrift += a * (driftSum / (float) driftCount - pitchDrift);
                pitchWobble += a * ((wobbleCount > 0 ? wobbleSum / (float) wobbleCount : 0.0f) - pitchWobble);
            }

            const bool drums = sustain < 0.42f && onsetRate > 0.8f && clarity < 0.85f;
            const bool voice = clarity > 0.45f && pitchDrift > 11.0f && pitchWobble > 6.0f;
            const int guess = drums ? percussive : (voice ? vocal : (clarity > 0.72f ? melodic : harmonic));

            if (! primed)
            {
                current = candidate = guess;
                primed = true;
            }
            else if (guess == current)
            {
                candidateSeconds = 0.0f;
            }
            else if (guess == candidate)
            {
                candidateSeconds += (float) seconds;

                if (candidateSeconds > 1.0f)
                    current = guess;
            }
            else
            {
                candidate = guess;
                candidateSeconds = (float) seconds;
            }
        }

        envSum = peakSum = 0.0;
        onsets = 0;
        frameSamples = 0;
    }

    //==========================================================================
    void Player::prepare (double sampleRate, const History* h, const OnsetList* o)
    {
        sr = sampleRate;
        history = h;
        onsets = o;
        reset();
    }

    void Player::reset()
    {
        current = Voice {};
        current.alive = true;

        for (auto& t : tails)
            t.alive = false;
    }

    float Player::fadeSamples (bool onsetAligned) const
    {
        return (float) (sr * (onsetAligned ? 0.0004 : 0.002));
    }

    void Player::pushTail (double fadeSeconds)
    {
        if (! current.alive)
            return;

        // A free slot, or the one closest to silence.
        Voice* slot = &tails[0];

        for (auto& t : tails)
        {
            if (! t.alive) { slot = &t; break; }
            if (t.fade < slot->fade) slot = &t;
        }

        *slot = current;
        slot->fadeStep = 1.0f / (float) (sr * fadeSeconds);
        slot->endK = -1;
    }

    void Player::beginPass (int64_t now)
    {
        // Out of a Break the track comes back a little softer, so a sound
        // that was ringing on does not snap in.
        const bool fromRest = current.alive && current.kind == Kind::rest;
        pushTail();
        current = Voice {};
        current.alive = true;
        current.startNow = now;
        current.fade = 0.0f;
        current.fadeStep = fromRest ? 1.0f / (float) (sr * 0.004) : 1.0f / fadeSamples (false);
    }

    float Player::accentFor (uint32_t seed, int index, bool onBeat) const
    {
        if (dynamics <= 0.0f)
            return 1.0f;

        Rng r (hash64 (seed, (uint64_t) index + 0xd1ull));
        const float depth = onBeat ? 0.25f : 0.75f;
        return 1.0f - dynamics * depth * r.range (0.3f, 1.0f);
    }

    void Player::begin (const Segment& seg, int64_t now, double spq, int64_t lengthSamples)
    {
        if (seg.kind == Kind::pass)
        {
            if (current.kind != Kind::pass)
                beginPass (now);
            return;
        }

        // A Break fades the sound out over 7 ms instead of cutting it, so a
        // ringing kick does not click; out of one, a little softer too.
        const bool isRest = seg.kind == Kind::rest;
        const bool fromRest = current.alive && current.kind == Kind::rest;
        pushTail (isRest ? 0.007 : 0.0015);
        Voice v;
        v.alive = true;
        v.kind = seg.kind;
        v.accent = accentFor (seg.seed, -1, false);
        v.seg = seg;
        v.startNow = now;
        v.lengthS = juce::jmax ((int64_t) 1, lengthSamples);
        v.spq = spq;
        v.minRepeatS = juce::jmax (0.004 * sr, spq * (seg.kind == Kind::roll ? seg.minRepeat : 0.03125));
        v.baseRepeatS = juce::jmax (v.minRepeatS, seg.repeat * spq);
        v.repeatS = swungRepeat (v, 0);

        const double maxRate = juce::jmax (std::abs (seg.rateStart), std::abs (seg.rateEnd)) * seg.maxArpRatio();
        const double preroll = 0.0012 * sr;
        bool aligned = false;

        switch (seg.kind)
        {
            case Kind::stutter:
            case Kind::roll:
            case Kind::bend:
            {
                // Repeat a hit that sits on the grid: the one landing now,
                // or one of the last few grid hits (the snare two beats ago).
                const double back = v.baseRepeatS * 1.5 * juce::jmax (0.0, maxRate - 1.0);
                const double stepS = juce::jmax (1.0, seg.step * spq);
                const double tolerance = 0.012 * sr + seg.swing * stepS;
                const double limit = (double) now - back;
                Rng pick (hash64 (seg.seed, 99));
                double found[4] {};
                int count = 0;

                if (back <= 0.0 && pick.chance (0.5f))
                {
                    double onset = 0.0;

                    if (onsets->near ((double) now, tolerance, (double) now, onset))
                        found[count++] = onset;
                }

                for (int j = 1; j <= 16 && count < 4; ++j)
                {
                    double onset = 0.0;

                    if (onsets->near ((double) now - j * stepS, tolerance, limit, onset))
                        if (count == 0 || std::abs (found[count - 1] - onset) > 1.0)
                            found[count++] = onset;
                }

                if (count > 0)
                {
                    v.capture = found[pick.below (juce::jmin (count, 3))] - preroll;
                    aligned = true;
                }
                else
                {
                    v.capture = (double) now - back;
                }
                break;
            }

            case Kind::slice:
                v.capture = (double) now - seg.back * spq;
                v.reversed = seg.reverseChance > 0.5f;
                v.slotRate = 1.0f;
                v.repeatS = (double) v.lengthS;
                aligned = true;
                break;

            case Kind::reverse:
            {
                const double span = v.repeatS * maxRate;
                v.capture = onsets->snap ((double) now - span, span * 0.5, preroll, &aligned);
                v.reversed = true;
                v.slotRate = 1.0f;
                break;
            }

            case Kind::shards:
                // The loop: the last repeat-length of the track, from a hit.
                v.capture = onsets->snap ((double) now - v.baseRepeatS, v.baseRepeatS * 0.5, preroll, &aligned);
                break;

            case Kind::scatter:
                pickScatterSlot (v, now);
                aligned = v.reversed ? false : true;
                break;

            case Kind::tape:
                v.capture = (double) now - juce::jmax (0.0, maxRate - 1.0) * (double) v.lengthS;
                break;

            case Kind::grains:
            case Kind::fog:
            {
                const double grainLength = seg.grainNote * spq * seg.grainOverlap;
                v.capture = (double) now - v.repeatS - grainLength * 2.2 - 0.01 * sr;
                break;
            }

            case Kind::gate:
            case Kind::pass:
            case Kind::rest:
            case Kind::echo:
                break;
        }

        // Sources from before the last jump would be another part of the
        // song: play the track instead. (Grains and tape start from
        // the recent past as well.)
        const bool needsPast = seg.kind == Kind::slice || seg.kind == Kind::scatter || seg.kind == Kind::reverse
                            || seg.kind == Kind::grains || seg.kind == Kind::fog || seg.kind == Kind::shards
                            || ((seg.kind == Kind::stutter || seg.kind == Kind::roll || seg.kind == Kind::bend) && v.capture < (double) now);

        if (needsPast && v.capture < (double) validFrom)
        {
            if (seg.kind == Kind::stutter || seg.kind == Kind::roll || seg.kind == Kind::bend)
            {
                v.capture = (double) now;   // repeat what is playing now instead
            }
            else
            {
                current = v;
                current.kind = Kind::pass;
                current.fade = 0.0f;
                current.fadeStep = 1.0f / fadeSamples (false);
                return;
            }
        }

        v.filterOn = seg.highPass || seg.cutStart < 0.999f || seg.cutEnd < 0.999f;
        v.fade = 0.0f;
        v.fadeStep = isRest ? 1.0f / (float) (sr * 0.007)
                   : (fromRest && ! aligned) ? 1.0f / (float) (sr * 0.004)
                                             : 1.0f / fadeSamples (aligned);
        current = v;
    }

    void Player::pickScatterSlot (Voice& v, int64_t now)
    {
        Rng r (hash64 (v.seg.seed, (uint64_t) v.repeatIndex));
        const double stepS = v.seg.step * v.spq;
        const int choices = juce::jmax (1, (int) std::floor (v.seg.reach / v.seg.step));

        v.slotRate = r.chance (v.seg.octaveChance) ? 0.5f : 1.0f;
        v.reversed = r.chance (v.seg.reverseChance);

        // Whole grid steps back, so the piece keeps its place in the groove.
        double back = (double) (1 + r.below (choices)) * stepS;

        while (back < v.repeatS * v.slotRate + 0.003 * sr)
            back += stepS;

        back = juce::jmin (back, (double) history->size() * 0.5);

        // Never further back than the last jump.
        const double shortest = v.repeatS * v.slotRate + 0.003 * sr;

        while (back - stepS >= shortest && (double) now - back < (double) validFrom)
            back -= stepS;

        v.capture = (double) now - back;
    }

    double Player::swungRepeat (const Voice& v, int index) const
    {
        const bool swings = v.seg.swing > 0.0f && (v.kind == Kind::stutter || v.kind == Kind::scatter)
                         && v.seg.repeat <= v.seg.step * 1.01 && v.seg.arpLength == 0;

        if (! swings)
            return v.baseRepeatS;

        return v.baseRepeatS * ((index & 1) ? 1.0 - v.seg.swing : 1.0 + v.seg.swing);
    }

    void Player::startRepeat (Voice& v, int64_t now)
    {
        pushTail();
        v.repStart += (int64_t) std::llround (v.repeatS);
        v.phase = 0.0;
        ++v.repeatIndex;
        v.repGain *= v.seg.decay;

        if (v.kind == Kind::roll)
        {
            // One hit per step, then two, then four: the repeat halves at
            // every grid step of the roll.
            const double stepS = juce::jmax (1.0, v.seg.step * v.spq);
            const int level = juce::jlimit (0, 6, (int) ((double) v.repStart / stepS + 1.0e-6));
            v.repeatS = juce::jmax (v.minRepeatS, v.baseRepeatS / (double) (1 << level));
        }
        else
        {
            v.repeatS = swungRepeat (v, v.repeatIndex);
        }

        if (v.kind == Kind::scatter)
            pickScatterSlot (v, now);

        // Dynamics: repeats on the beat (and on the eighths) stay strong,
        // the ones between them get quieter.
        {
            const double beatS = v.spq;
            const double into = (double) (v.startNow + v.repStart);
            const double phase = std::fmod (into, beatS * 0.5) / (beatS * 0.5);
            v.repAccent = accentFor (v.seg.seed, v.repeatIndex, phase < 0.08 || phase > 0.92);
        }

        v.fade = 0.0f;
        v.fadeStep = 1.0f / fadeSamples (v.kind != Kind::scatter || ! v.reversed);
    }

    void Player::strike (int64_t now, double spq, double stepPpq, uint32_t seed)
    {
        switch (current.kind)
        {
            case Kind::stutter:
            case Kind::roll:
            case Kind::bend:
            case Kind::scatter:
            case Kind::reverse:
                pushTail();
                current.repStart = now - current.startNow;
                current.phase = 0.0;
                current.fade = 0.0f;
                current.fadeStep = 1.0f / fadeSamples (true);
                break;

            case Kind::pass:
            {
                // Replay the last hit of the main track for one step.
                Segment s;
                s.kind = Kind::stutter;
                s.repeat = stepPpq;
                s.step = stepPpq;
                s.seed = seed;
                const auto length = (int64_t) (stepPpq * spq);
                begin (s, now, spq, length);
                bool aligned = false;
                current.capture = onsets->snap ((double) now, spq, 0.0012 * sr, &aligned);

                if (! aligned)
                {
                    // Nothing to replay: stay with the track.
                    current.kind = Kind::pass;
                }

                current.endK = length;
                break;
            }

            case Kind::tape:
            case Kind::gate:
            case Kind::grains:
            case Kind::rest:
            case Kind::slice:
            case Kind::shards:
            case Kind::fog:
            case Kind::echo:
                break;
        }
    }

    float Player::rateAt (const Voice& v, float p) const
    {
        const float a = v.seg.rateStart, b = v.seg.rateEnd;

        if (b < 0.01f)
            return a * std::pow (1.0f - p, 1.6f);

        if (std::abs (a - b) < 1.0e-4f)
            return a;

        return a * std::pow (b / a, p);
    }

    void Player::spawnGrain (Voice& v, int64_t, float p)
    {
        // A grain on every grain note: the texture keeps time.
        const double note = juce::jmax (0.004 * sr, v.seg.grainNote * v.spq);
        v.spawnCountdown = juce::jmax (16, (int) std::llround (note));

        Grain* g = nullptr;

        for (auto& candidate : v.grains)
            if (! candidate.on) { g = &candidate; break; }

        if (g == nullptr)
            return;

        Rng r (hash64 (v.seg.seed, v.grainCount++));
        const int length = juce::jmax (64, (int) (note * v.seg.grainOverlap));
        float rate = rateAt (v, p);

        if (r.chance (v.seg.octaveChance))
        {
            constexpr float intervals[] { 2.0f, 0.5f, 1.5f };
            rate *= intervals[r.below (v.seg.taper < 0.3f ? 2 : 3)];
        }

        if (v.kind == Kind::fog)
            rate *= std::exp2 (r.range (-0.12f, 0.12f) / 12.0f);   // a few cents either way

        const bool reversed = r.chance (v.seg.reverseChance);
        g->accent = 1.0f - dynamics * 0.7f * r.uni();

        // Drums take grains from grid positions; textures drift around them
        // (fog drifts the whole beat).
        const int slots = juce::jmax (1, (int) (v.repeatS / note));
        double start = v.capture + (double) r.below (slots) * note;

        if (v.seg.taper > 0.3f)
            start += r.range (-0.25f, 0.25f) * note * (v.kind == Kind::fog ? 3.0f : 1.0f);

        start = juce::jmin (start, v.capture + v.repeatS);

        const float pan = juce::MathConstants<float>::pi * (0.25f + r.range (-0.2f, 0.2f) * v.seg.taper);

        g->on = true;
        g->length = length;
        g->age = 0;
        g->rate = reversed ? -rate : rate;
        g->pos = reversed ? start + length * rate : start;
        g->gainL = std::cos (pan) * 1.41421356f;
        g->gainR = std::sin (pan) * 1.41421356f;
    }

    void Player::renderVoice (Voice& v, int64_t now, float inL, float inR, float& l, float& r)
    {
        const int64_t k = now - v.startNow;
        const float p = juce::jlimit (0.0f, 1.0f, (float) k / (float) v.lengthS);
        const auto& s = v.seg;
        float gain = s.gainStart + (s.gainEnd - s.gainStart) * p;

        switch (v.kind)
        {
            case Kind::pass:
            case Kind::echo:
                l = inL;
                r = inR;
                return;

            case Kind::rest:
                l = r = 0.0f;
                return;

            case Kind::shards:
            {
                // Three layers loop the same piece at their own speeds: the
                // first at once, the next a loop later, the last after two.
                const double loopS = juce::jmax (64.0, v.baseRepeatS);
                const float fadeS = (float) (0.004 * sr);
                constexpr float gains[] { 0.8f, 0.5f, 0.42f };
                constexpr float panL[] { 1.0f, 1.0f, 0.55f }, panR[] { 1.0f, 0.55f, 1.0f };
                l = r = 0.0f;

                for (int j = 0; j < 3; ++j)
                {
                    const double t = (double) k - (double) j * loopS;

                    if (t < 0.0)
                        continue;

                    const double rate = std::exp2 ((double) s.arp[(size_t) j] / 12.0);
                    const double cycle = loopS / rate;
                    const double inCycle = std::fmod (t, cycle);
                    const double pos = v.capture + inCycle * rate;
                    const float w = juce::jmin (1.0f, (float) inCycle / fadeS) * juce::jmin (1.0f, (float) (cycle - inCycle) / fadeS);
                    l += history->read (0, pos, now) * w * gains[j] * panL[j];
                    r += history->read (1, pos, now) * w * gains[j] * panR[j];
                }

                gain *= v.repAccent;
                break;
            }

            case Kind::stutter:
            case Kind::roll:
            case Kind::bend:
            case Kind::reverse:
            case Kind::scatter:
            case Kind::slice:
            {
                const double rate = std::abs (rateAt (v, p) * v.slotRate * s.arpRatio (v.repeatIndex));
                const double pos = v.reversed ? v.capture + v.repeatS * rate - v.phase : v.capture + v.phase;
                v.phase += rate;
                l = history->read (0, pos, now);
                r = history->read (1, pos, now);
                gain *= v.repGain * v.repAccent;

                // Short repeats are articulated: each one stops just before
                // the next, so they sound like hits, not a buzz.
                if (s.punch)
                {
                    const double t = (double) (k - v.repStart);
                    gain *= (float) juce::jlimit (0.0, 1.0, (v.repeatS * 0.88 - t) / (0.004 * sr));
                }
                break;
            }

            case Kind::tape:
            {
                const double pos = v.capture + v.phase;
                v.phase += rateAt (v, p);
                l = history->read (0, pos, now);
                r = history->read (1, pos, now);
                break;
            }

            case Kind::gate:
            {
                // Steps in swung pairs: the first of each pair is longer.
                const double stepS = juce::jmax (1.0, v.repeatS);
                const double pair = 2.0 * stepS, first = stepS * (1.0 + s.swing);
                const auto pairIndex = (int64_t) ((double) k / pair);
                const double inPair = (double) k - (double) pairIndex * pair;
                const bool second = inPair >= first;
                const auto index = pairIndex * 2 + (second ? 1 : 0);
                const float t = (float) (second ? inPair - first : inPair);
                const float length = (float) (second ? pair - first : first);
                const bool on = index == 0 || (float) (hash64 (s.seed, (uint64_t) index) >> 40) * (1.0f / 16777216.0f) < s.density;
                const float attack = (float) (0.0005 * sr), release = (float) (0.004 * sr);
                const float g = on ? juce::jmin (1.0f, t / attack) * juce::jlimit (0.0f, 1.0f, (length * 0.9f - t) / release) : 0.0f;
                l = inL * g;
                r = inR * g;
                break;
            }

            case Kind::grains:
            case Kind::fog:
            {
                if (--v.spawnCountdown <= 0)
                    spawnGrain (v, now, p);

                l = r = 0.0f;

                for (auto& g : v.grains)
                {
                    if (! g.on)
                        continue;

                    // Tukey window: a short taper for punchy drum grains, a
                    // full Hann for smooth textures.
                    const float x = (float) g.age / (float) g.length;
                    const float edge = 0.5f * s.taper;
                    float w = 1.0f;

                    if (x < edge)
                        w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x / edge);
                    else if (x > 1.0f - edge)
                        w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (1.0f - x) / edge);
                    l += history->read (0, g.pos, now) * w * g.gainL * g.accent;
                    r += history->read (1, g.pos, now) * w * g.gainR * g.accent;
                    g.pos += g.rate;

                    if (++g.age >= g.length)
                        g.on = false;
                }

                gain *= 1.0f / std::sqrt (juce::jmax (1.0f, s.grainOverlap));
                break;
            }
        }

        if (v.filterOn)
        {
            if (--v.filterCountdown <= 0)
            {
                v.filterCountdown = 32;
                const float c = s.cutStart + (s.cutEnd - s.cutStart) * p;
                const float hz = s.highPass ? 20.0f * std::exp2 ((1.0f - c) * 7.2f)
                                            : 60.0f * std::exp2 (c * 8.38f);
                v.filter.set (hz, 1.2f, sr);
            }

            const int type = s.highPass ? 1 : 0;
            l = v.filter.process (0, l, type);
            r = v.filter.process (1, r, type);
        }

        gain *= v.accent;
        l *= gain;
        r *= gain;
    }

    void Player::render (int64_t now, float inL, float inR, float& outL, float& outR, Weights& w)
    {
        outL = outR = 0.0f;
        w = {};

        if (current.endK >= 0 && now - current.startNow >= current.endK)
            beginPass (now);

        const bool repeats = current.kind == Kind::stutter || current.kind == Kind::roll || current.kind == Kind::bend
                          || current.kind == Kind::reverse || current.kind == Kind::scatter;

        if (repeats && (double) (now - current.startNow - current.repStart) >= current.repeatS)
            startRepeat (current, now);

        float l = 0.0f, r = 0.0f;
        renderVoice (current, now, inL, inR, l, r);
        outL += l * current.fade;
        outR += r * current.fade;

        if (current.kind == Kind::pass || current.kind == Kind::echo)
            w.pass += current.fade;
        else if (current.kind == Kind::rest)
            w.rest += current.fade;

        current.fade = juce::jmin (1.0f, current.fade + current.fadeStep);

        for (auto& t : tails)
        {
            if (! t.alive)
                continue;

            renderVoice (t, now, inL, inR, l, r);
            outL += l * t.fade;
            outR += r * t.fade;

            if (t.kind == Kind::pass || t.kind == Kind::echo)
                w.pass += t.fade;
            else if (t.kind == Kind::rest)
                w.rest += t.fade;

            t.fade -= t.fadeStep;

            if (t.fade <= 0.0f)
                t.alive = false;
        }
    }

    //==========================================================================
    //==========================================================================
    void GrainDelay::prepare (double sampleRate)
    {
        sr = sampleRate;
        int size = 1;

        while (size < (int) (sr * 4.0))
            size <<= 1;

        buffer[0].assign ((size_t) size, 0.0f);
        buffer[1].assign ((size_t) size, 0.0f);
        mask = size - 1;
        grainLength = (int) (0.07 * sr);
        tone.set (5500.0f, 0.6f, sr);
        reset();
    }

    void GrainDelay::reset()
    {
        std::fill (buffer[0].begin(), buffer[0].end(), 0.0f);
        std::fill (buffer[1].begin(), buffer[1].end(), 0.0f);
        write = 0;
        countdown = 0;
        grainCount = 0;
        left = true;
        fbL = fbR = 0.0f;
        tone.reset();

        for (auto& g : grains)
            g.on = false;
    }

    void GrainDelay::setup (double delaySamples, float fb, float shimmerAmount, float reverseAmount)
    {
        delay = juce::jlimit ((double) grainLength * 1.5, (double) mask * 0.45, delaySamples);
        feedback = fb;
        shimmer = shimmerAmount;
        reverse = reverseAmount;
    }

    void GrainDelay::spawn()
    {
        Grain* g = nullptr;

        for (auto& candidate : grains)
            if (! candidate.on) { g = &candidate; break; }

        if (g == nullptr)
            return;

        Rng r (hash64 (0xec40ull, grainCount++));
        const double rate = r.chance (shimmer) ? 2.0 : 1.0;
        const bool backwards = r.chance (reverse);
        // A little smear in time: up to 12 ms either way of the echo.
        const double jitter = r.range (-0.012f, 0.012f) * sr;
        const double start = (double) write - delay + jitter - (double) grainLength * (rate - 1.0);

        g->on = true;
        g->age = 0;
        g->length = grainLength;
        g->rate = backwards ? -rate : rate;
        g->pos = backwards ? start + (double) grainLength * rate : start;

        // Echoes lean left and right in turn.
        left = ! left;
        const float pan = juce::MathConstants<float>::halfPi * (left ? 0.3f : 0.7f) + r.range (-0.1f, 0.1f);
        g->gainL = std::cos (pan) * 1.2f;
        g->gainR = std::sin (pan) * 1.2f;
    }

    void GrainDelay::process (float inL, float inR, float& outL, float& outR)
    {
        if (--countdown <= 0)
        {
            countdown = grainLength / 2;   // two grains overlap
            spawn();
        }

        outL = outR = 0.0f;

        for (auto& g : grains)
        {
            if (! g.on)
                continue;

            const float x = (float) g.age / (float) g.length;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * x);
            const auto i = (int64_t) std::floor (g.pos);
            const float f = (float) (g.pos - (double) i);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto& b = buffer[ch];
                const float a = b[(size_t) (i & mask)], c = b[(size_t) ((i + 1) & mask)];
                const float y = (a + f * (c - a)) * w;

                if (ch == 0) outL += y * g.gainL;
                else         outR += y * g.gainR;
            }

            g.pos += g.rate;

            if (++g.age >= g.length)
                g.on = false;
        }

        // The repeats go round again, a little darker each time.
        fbL = tone.process (0, outL, 0);
        fbR = tone.process (1, outR, 0);
        buffer[0][(size_t) (write & mask)] = inL + feedback * fbL;
        buffer[1][(size_t) (write & mask)] = inR + feedback * fbR;
        ++write;
    }

    //==========================================================================
    void Vocoder::prepare (double sampleRate)
    {
        // Bands spread evenly in octaves from 90 Hz to 10 kHz, about a
        // third of an octave apart.
        for (int k = 0; k < numBands; ++k)
        {
            const float hz = 90.0f * std::pow (10000.0f / 90.0f, (float) k / (float) (numBands - 1));
            mod[k].set (hz, 4.0f, sampleRate);
            car[k].set (hz, 4.0f, sampleRate);
        }

        modAtt = coeff (0.002, sampleRate);
        modRel = coeff (0.025, sampleRate);
        carAtt = coeff (0.008, sampleRate);
        carRel = coeff (0.06, sampleRate);
        peakRel = coeff (3.0, sampleRate);
        duckAtt = coeff (0.004, sampleRate);
        duckRel = coeff (0.15, sampleRate);
        reset();
    }

    void Vocoder::reset()
    {
        for (int k = 0; k < numBands; ++k)
        {
            mod[k].reset();
            car[k].reset();
            modEnv[k] = carEnv[k] = duckEnv[k] = 0.0f;
        }

        totalPeak = 0.0f;
    }

    void Vocoder::process (float& l, float& r, float prey, bool carve)
    {
        float bandL[numBands], bandR[numBands];
        float modSquares = 0.0f, carSquares = 0.0f;

        for (int k = 0; k < numBands; ++k)
        {
            const float m = std::abs (mod[k].process (0, prey, 2));
            modEnv[k] += (m > modEnv[k] ? modAtt : modRel) * (m - modEnv[k]);

            bandL[k] = car[k].process (0, l, 2);
            bandR[k] = car[k].process (1, r, 2);
            const float c = 0.5f * (std::abs (bandL[k]) + std::abs (bandR[k]));
            carEnv[k] += (c > carEnv[k] ? carAtt : carRel) * (c - carEnv[k]);

            modSquares += modEnv[k] * modEnv[k];
            carSquares += carEnv[k] * carEnv[k];
        }

        // How loud the prey is now against its recent peak.
        const float modNorm = std::sqrt (modSquares);
        totalPeak = modNorm > totalPeak ? modNorm : totalPeak + peakRel * (modNorm - totalPeak);
        const float reference = totalPeak + 1.0e-7f;

        float outL = 0.0f, outR = 0.0f;

        if (carve)
        {
            // The track clears the range the prey sings in: every band
            // within about 18 dB of the prey's loudest band now ducks all
            // the way (42 dB down not at all), as deep as the prey is loud
            // against its recent peak.
            float loudest = 1.0e-9f;

            for (int k = 0; k < numBands; ++k)
                loudest = juce::jmax (loudest, modEnv[k]);

            const float level = juce::jlimit (0.0f, 1.0f, (std::log2 (modNorm / reference + 1.0e-9f) + 5.0f) * 0.25f);

            float shape[numBands];

            for (int k = 0; k < numBands; ++k)
                shape[k] = juce::jlimit (0.0f, 1.0f, (std::log2 (modEnv[k] / loudest + 1.0e-9f) + 7.0f) * 0.25f);

            for (int k = 0; k < numBands; ++k)
            {
                // The hole spreads a band either side, so it is heard.
                const float spread = 0.7f * juce::jmax (k > 0 ? shape[k - 1] : 0.0f, k < numBands - 1 ? shape[k + 1] : 0.0f);
                // Opens fast, closes slowly: the hole stays through a phrase.
                const float target = juce::jmax (shape[k], spread) * level;
                duckEnv[k] += (target > duckEnv[k] ? duckAtt : duckRel) * (target - duckEnv[k]);
                const float duck = duckEnv[k];
                outL += bandL[k] * duck;
                outR += bandR[k] * duck;
            }

            // The bands overlap and sum to about 1.5: where the prey fills
            // the bands, the track drops by 12 dB or more.
            l -= 0.64f * outL;
            r -= 0.64f * outR;
            return;
        }

        // The track, flattened band by band, takes the prey's spectral
        // shape and its level.
        const float carRms = std::sqrt (carSquares);
        const float floorLevel = 0.15f * carRms + 1.0e-7f;
        const float level = juce::jmin (1.0f, 1.5f * modNorm / reference);
        const float shape = level / (modNorm + 1.0e-7f);

        for (int k = 0; k < numBands; ++k)
        {
            const float gain = modEnv[k] * shape * carRms / juce::jmax (carEnv[k], floorLevel);
            outL += bandL[k] * gain;
            outR += bandR[k] * gain;
        }

        l = outL;
        r = outR;
    }

    void PitchShifter::prepare (double sampleRate)
    {
        int size = 1;

        while (size < (int) (sampleRate * 0.2))
            size <<= 1;

        for (auto& b : buffer)
            b.assign ((size_t) size, 0.0f);

        mask = size - 1;
        windowS = (float) (sampleRate * 0.05);
        reset();
    }

    void PitchShifter::reset()
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);

        write = 0;
        phase = 0.0f;
    }

    void PitchShifter::process (float& l, float& r, float ratio)
    {
        buffer[0][(size_t) write] = l;
        buffer[1][(size_t) write] = r;

        phase += (1.0f - ratio) / windowS;
        phase -= std::floor (phase);

        auto tap = [this] (int ch, float delay)
        {
            const float pos = (float) write - 2.0f - delay;
            const int i = (int) std::floor (pos);
            const float f = pos - (float) i;
            const float a = buffer[ch][(size_t) (i & mask)], b = buffer[ch][(size_t) ((i + 1) & mask)];
            return a + f * (b - a);
        };

        const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
        const float w1 = std::sin (juce::MathConstants<float>::pi * phase);
        const float w2 = std::sin (juce::MathConstants<float>::pi * p2);
        const float g1 = w1 * w1, g2 = w2 * w2;

        l = tap (0, phase * windowS) * g1 + tap (0, p2 * windowS) * g2;
        r = tap (1, phase * windowS) * g1 + tap (1, p2 * windowS) * g2;
        write = (write + 1) & mask;
    }
}
