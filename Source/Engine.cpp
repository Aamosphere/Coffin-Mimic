#include "Engine.h"

namespace mimic
{
    namespace
    {
        float coeff (double seconds, double sampleRate)
        {
            return 1.0f - std::exp (-1.0f / (float) (seconds * sampleRate));
        }

        // How likely each content kind is for each material:
        // stutter, roll, reverse, scatter, tape, gate, grains.
        constexpr float contentWeights[3][numContent] {
            { 3.0f, 3.0f, 1.0f, 3.0f, 1.0f, 2.0f, 0.4f },   // percussive
            { 2.0f, 1.0f, 2.5f, 2.0f, 2.0f, 1.0f, 2.0f },   // melodic
            { 1.0f, 0.4f, 3.0f, 1.0f, 2.0f, 3.0f, 3.0f }    // harmonic
        };

        Kind pickKind (Rng& r, const GenSettings& g, bool climax, Kind avoid)
        {
            float w[numContent];
            float total = 0.0f;

            for (int i = 0; i < numContent; ++i)
            {
                const auto kind = (Kind) (i + 1);
                float weight = (g.enabled >> i) & 1u ? contentWeights[juce::jlimit (0, 2, g.material)][i] : 0.0f;

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
                    return (Kind) (i + 1);

                x -= w[i];
            }

            return Kind::pass;
        }

        // Repeat length in steps: the wilder, the shorter. Melodic and
        // harmonic material leans to longer, echo-like repeats.
        double pickRepeat (Rng& r, double step, float intensity, int material)
        {
            constexpr double options[] { 3.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 0.125 };
            const float lean = material == percussive ? 1.6f : (material == melodic ? 0.4f : 0.0f);
            const float centre = 1.2f + lean + intensity * 4.6f + r.range (-1.3f, 1.3f);
            return step * options[juce::jlimit (0, 8, juce::roundToInt (centre))];
        }

        // Arpeggio shapes for melodic stutters, in semitones per repeat.
        constexpr int8_t arps[][4] {
            { 0, 12, 7, 19 }, { 0, 7, 12, 7 }, { 0, -12, 0, 12 }, { 0, 3, 7, 12 }, { 0, 5, 7, 12 }, { 12, 7, 0, -5 }
        };

        Segment makeSegment (Kind kind, double start, double length, float intensity, const GenSettings& g, Rng& r)
        {
            Segment s;
            s.start = start;
            s.length = length;
            s.kind = kind;
            s.step = g.step;
            s.swing = g.swing;
            s.seed = r.next();
            s.reach = g.bar * (double) (1 + r.below (intensity > 0.5f ? 4 : 2));
            const float I = intensity;
            const int m = g.material;

            switch (kind)
            {
                case Kind::stutter:
                {
                    s.repeat = juce::jmin (length, pickRepeat (r, g.step, I, m));

                    if (m == melodic && r.chance (0.35f + 0.4f * I))
                    {
                        // Each repeat jumps to the next note of an arpeggio.
                        const auto& shape = arps[r.below ((int) std::size (arps))];
                        std::copy (std::begin (shape), std::end (shape), s.arp.begin());
                        s.arpLength = 2 + r.below (3);
                        s.repeat = juce::jmin (length, g.step * (I > 0.6f ? 0.5 : 1.0));
                    }
                    else if (m != percussive && r.chance (0.5f))
                    {
                        // Echo: dotted or straight eighths fading out.
                        s.repeat = juce::jmin (length, r.chance (0.5f) ? 0.75 : 0.5);
                        s.decay = r.range (0.6f, 0.82f);
                    }
                    else if (r.chance (0.3f * I))
                    {
                        constexpr float semis[] { 12.0f, -12.0f, 7.0f, -5.0f, 5.0f };
                        s.rateStart = s.rateEnd = std::exp2 (semis[r.below (5)] / 12.0f);
                    }

                    if (r.chance (0.25f * I))
                        s.rateEnd = s.rateStart * (r.chance (0.5f) ? 2.0f : 0.5f);

                    if (s.decay >= 1.0f && r.chance (0.4f))
                        s.decay = r.range (0.78f, 0.97f);
                    break;
                }

                case Kind::roll:
                    s.repeat = juce::jmin (length, g.step * (I > 0.6f || m == percussive ? 0.5 : 1.0));
                    s.shrink = m == percussive ? r.range (0.55f, 0.75f) : r.range (0.7f, 0.88f);
                    s.gainStart = r.range (0.45f, 0.7f);

                    if (r.chance ((m == percussive ? 0.5f : 0.3f) * I))
                        s.rateEnd = std::exp2 (r.range (2.0f, 12.0f) / 12.0f);
                    break;

                case Kind::reverse:
                    s.repeat = (length > g.step && r.chance ((m == harmonic ? 0.1f : 0.3f) + 0.4f * I))
                                 ? juce::jmin (length, juce::jmax (g.step, pickRepeat (r, g.step, I, m)))
                                 : length;

                    if (r.chance ((m == melodic ? 0.35f : 0.2f) * I))
                        s.rateStart = s.rateEnd = r.chance (0.5f) ? 2.0f : 0.5f;
                    break;

                case Kind::scatter:
                    s.repeat = m == percussive ? juce::jmin (length, r.chance (I) ? g.step * 0.5 : g.step)
                                               : juce::jmin (length, r.chance (0.5f) ? g.step * 2.0 : g.step);
                    s.reverseChance = (m == harmonic ? 0.45f : 0.3f) * I;
                    s.octaveChance = (m == melodic ? 0.45f : 0.25f) * I;
                    break;

                case Kind::tape:
                    s.repeat = length;

                    if (m != percussive && r.chance (m == melodic ? 0.6f : 0.4f))
                    {
                        s.rateEnd = r.chance (0.5f) ? 2.0f : 0.5f;   // glide
                    }
                    else
                    {
                        s.rateEnd = 0.0f;                           // stop
                        s.gainEnd = 0.35f;
                    }
                    break;

                case Kind::gate:
                    s.repeat = g.step * (r.chance (0.2f) ? 2.0 / 3.0 : (r.chance (I) || m == harmonic ? 0.5 : 1.0));
                    s.density = m == harmonic ? r.range (0.55f, 0.85f) : r.range (0.35f, 0.8f);
                    break;

                case Kind::grains:
                    if (m == percussive)
                    {
                        // Micro-grains: a fizzing, bitty texture of the hits.
                        s.grainMin = 0.010f;
                        s.grainMax = 0.035f;
                        s.repeat = g.step * (double) r.range (1.0f, 4.0f);
                        s.density = juce::jlimit (0.3f, 1.0f, 0.5f + 0.5f * I);
                        s.octaveChance = 0.15f * I;
                    }
                    else if (m == melodic)
                    {
                        s.grainMin = 0.04f;
                        s.grainMax = 0.12f;
                        s.repeat = r.chance (0.3f) ? 0.06 : g.beat * (double) r.range (0.5f, 1.5f);
                        s.density = juce::jlimit (0.2f, 1.0f, 0.3f + 0.6f * I);
                        s.octaveChance = 0.35f + 0.3f * I;
                    }
                    else
                    {
                        // Long, overlapping grains, often frozen, with an
                        // octave-up shimmer.
                        s.grainMin = 0.08f;
                        s.grainMax = 0.25f;
                        s.repeat = r.chance (0.6f) ? 0.08 : g.beat * (double) r.range (1.0f, 2.0f);
                        s.density = juce::jlimit (0.4f, 1.0f, 0.55f + 0.45f * I);
                        s.octaveChance = 0.45f + 0.3f * I;
                    }

                    s.reverseChance = (m == harmonic ? 0.35f : 0.25f) * I;
                    break;

                case Kind::pass:
                case Kind::rest:
                    break;
            }

            // Filter sweeps: a closing low-pass, or a high-pass opening up
            // like a riser. Pads and chords get more of them.
            if (r.chance ((m == harmonic ? 0.3f : 0.15f) + 0.35f * I))
            {
                if (r.chance (0.5f))
                {
                    s.cutEnd = r.range (0.25f, 0.6f);
                }
                else
                {
                    s.highPass = true;
                    s.cutEnd = r.range (0.2f, 0.5f);
                }
            }

            return s;
        }

        Segment restSegment (double start, double length)
        {
            Segment s;
            s.start = start;
            s.length = length;
            s.kind = Kind::rest;
            return s;
        }

        // Grid position of step i, with the odd steps of each pair late.
        double gridAt (int i, double step, float swing)
        {
            return (double) (i / 2) * 2.0 * step + ((i & 1) ? step * (1.0 + swing) : 0.0);
        }

        Segment passSegment (double start, double length)
        {
            Segment s;
            s.start = start;
            s.length = length;
            return s;
        }

        void addOrMerge (Script& script, const Segment& s)
        {
            if ((s.kind == Kind::pass || s.kind == Kind::rest) && script.count > 0)
            {
                auto& last = script.segments[(size_t) script.count - 1];

                if (last.kind == s.kind)
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

    void generate (Script& script, const GenSettings& g, uint64_t seed)
    {
        script.clear();
        Rng r (seed);
        const float I = juce::jlimit (0.0f, 1.0f, g.intensity);
        const float swing = juce::jlimit (0.0f, 0.5f, g.swing);
        const double step = juce::jmax (1.0 / 64.0, g.step);
        int spanSteps = juce::jmax (1, (int) std::floor (g.span / step + 1.0e-6));
        Kind previous = Kind::pass;
        int at = 0;

        // Start and end of a run of steps on the swung grid; the last step
        // always reaches the end of the span.
        const int totalSteps = spanSteps;
        auto startOf = [&] (int i) { return gridAt (i, step, swing); };
        auto endOf = [&] (int i) { return i >= totalSteps ? g.span : gridAt (i, step, swing); };

        auto addRest = [&] (int from, int steps)
        {
            addOrMerge (script, restSegment (startOf (from), endOf (from + steps) - startOf (from)));
        };

        if (g.fill)
        {
            // A Break right before the downbeat: the fill ends in silence.
            const bool endRest = spanSteps > 2 && r.chance (g.breaks * 0.7f);

            if (endRest)
                --spanSteps;

            // Phrases that get shorter and wilder towards the end of the fill.
            while (at < spanSteps)
            {
                const int remaining = spanSteps - at;
                const int longest = juce::jmax (1, juce::roundToInt (juce::jmap (I, (float) spanSteps, 2.0f)));
                int steps = juce::jlimit (1, remaining, 1 + r.below (longest));

                if (steps >= 3 && (steps & 1) && r.chance (0.6f))
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
                    const auto kind = pickKind (r, g, last && I > 0.45f, previous);
                    addOrMerge (script, kind == Kind::pass ? passSegment (start, end - start)
                                                           : makeSegment (kind, start, end - start, local, g, r));
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
        }
        else
        {
            // Wild: mostly the bar as it is, with events that grow denser and
            // longer with intensity, a little more often towards the bar end.
            const float density = 0.03f + 0.85f * std::pow (I, 1.5f);

            while (at < spanSteps)
            {
                const float position = (float) at / (float) spanSteps;

                if (r.chance (density * (0.6f + 0.8f * position)))
                {
                    const int steps = juce::jmin (spanSteps - at, 1 + r.below (I < 0.35f ? 2 : 4));
                    const double start = startOf (at), end = endOf (at + steps);
                    const auto kind = pickKind (r, g, false, previous);
                    addOrMerge (script, kind == Kind::pass ? passSegment (start, end - start)
                                                           : makeSegment (kind, start, end - start, I * r.range (0.7f, 1.1f), g, r));
                    previous = kind;
                    at += steps;

                    if (kind != Kind::pass && at < spanSteps && r.chance (g.breaks * 0.7f))
                    {
                        const int rest = juce::jmin (spanSteps - at, 1 + r.below (2));
                        addRest (at, rest);
                        at += rest;
                    }
                }
                else
                {
                    addOrMerge (script, passSegment (startOf (at), endOf (at + 1) - startOf (at)));
                    previous = Kind::pass;
                    ++at;
                }
            }
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
        current = candidate = harmonic;
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

            const float frameSustain = (float) (envSum / juce::jmax (1.0e-9, peakSum));
            const float frameRate = (float) (onsets / juce::jmax (1.0e-3, seconds));
            const float a = primed ? 0.25f : 1.0f;
            clarity += a * (best - clarity);
            sustain += a * (frameSustain - sustain);
            onsetRate += a * (frameRate - onsetRate);

            const int guess = (sustain < 0.42f && onsetRate > 0.8f && clarity < 0.85f) ? percussive
                            : (clarity > 0.72f ? melodic : harmonic);

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

    void Player::pushTail()
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
        slot->fadeStep = 1.0f / (float) (sr * 0.0015);
        slot->endK = -1;
    }

    void Player::beginPass (int64_t now)
    {
        pushTail();
        current = Voice {};
        current.alive = true;
        current.startNow = now;
        current.fade = 0.0f;
        current.fadeStep = 1.0f / fadeSamples (false);
    }

    void Player::begin (const Segment& seg, int64_t now, double spq, int64_t lengthSamples)
    {
        if (seg.kind == Kind::pass)
        {
            if (current.kind != Kind::pass)
                beginPass (now);
            return;
        }

        pushTail();
        Voice v;
        v.alive = true;
        v.kind = seg.kind;
        v.seg = seg;
        v.startNow = now;
        v.lengthS = juce::jmax ((int64_t) 1, lengthSamples);
        v.spq = spq;
        v.minRepeatS = juce::jmax (0.004 * sr, spq * 0.03125);
        v.baseRepeatS = juce::jmax (v.minRepeatS, seg.repeat * spq);
        v.repeatS = swungRepeat (v, 0);

        const double maxRate = juce::jmax (std::abs (seg.rateStart), std::abs (seg.rateEnd)) * seg.maxArpRatio();
        const double preroll = 0.0012 * sr;
        bool aligned = false;

        switch (seg.kind)
        {
            case Kind::stutter:
            case Kind::roll:
            {
                const double back = v.baseRepeatS * 1.5 * juce::jmax (0.0, maxRate - 1.0);
                v.capture = onsets->snap ((double) now - back, v.repeatS * 0.35, preroll, &aligned);
                break;
            }

            case Kind::reverse:
            {
                const double span = v.repeatS * maxRate;
                v.capture = onsets->snap ((double) now - span, span * 0.5, preroll, &aligned);
                v.reversed = true;
                v.slotRate = 1.0f;
                break;
            }

            case Kind::scatter:
                pickScatterSlot (v, now);
                aligned = v.reversed ? false : true;
                break;

            case Kind::tape:
                v.capture = (double) now - juce::jmax (0.0, maxRate - 1.0) * (double) v.lengthS;
                break;

            case Kind::grains:
                v.capture = (double) now - v.repeatS - (2.0 * seg.grainMax * 1.3 + 0.02) * sr;
                break;

            case Kind::gate:
            case Kind::pass:
            case Kind::rest:
                break;
        }

        v.filterOn = seg.highPass || seg.cutStart < 0.999f || seg.cutEnd < 0.999f;
        v.fade = 0.0f;
        v.fadeStep = 1.0f / fadeSamples (aligned);
        current = v;
    }

    void Player::pickScatterSlot (Voice& v, int64_t now)
    {
        Rng r (hash64 (v.seg.seed, (uint64_t) v.repeatIndex));
        const double stepS = v.seg.step * v.spq;
        const int choices = juce::jmax (1, (int) std::floor (v.seg.reach / v.seg.step));

        v.slotRate = r.chance (v.seg.octaveChance) ? (r.chance (0.5f) ? 2.0f : 0.5f) : 1.0f;
        v.reversed = r.chance (v.seg.reverseChance);

        double back = (double) (1 + r.below (choices)) * stepS;
        back = juce::jmax (back, v.repeatS * v.slotRate + 0.003 * sr);
        back = juce::jmin (back, (double) history->size() * 0.5);

        v.capture = onsets->snap ((double) now - back, v.repeatS * 0.3, 0.0012 * sr);
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
            v.repeatS = juce::jmax (v.minRepeatS, v.repeatS * v.seg.shrink);
        else
            v.repeatS = swungRepeat (v, v.repeatIndex);

        if (v.kind == Kind::scatter)
            pickScatterSlot (v, now);

        v.fade = 0.0f;
        v.fadeStep = 1.0f / fadeSamples (v.kind != Kind::scatter || ! v.reversed);
    }

    void Player::strike (int64_t now, double spq, double stepPpq, uint32_t seed)
    {
        switch (current.kind)
        {
            case Kind::stutter:
            case Kind::roll:
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
        Grain* g = nullptr;

        for (auto& candidate : v.grains)
            if (! candidate.on) { g = &candidate; break; }

        Rng r (hash64 (v.seg.seed, v.grainCount++));
        const float d = v.seg.density;
        const int length = juce::jmax (64, (int) (sr * r.range (v.seg.grainMin, v.seg.grainMax)));
        v.spawnCountdown = juce::jmax (16, (int) ((float) length / (1.0f + 6.0f * d) * r.range (0.6f, 1.4f)));

        if (g == nullptr)
            return;

        float rate = rateAt (v, p);

        if (r.chance (v.seg.octaveChance))
        {
            constexpr float intervals[] { 2.0f, 0.5f, 1.5f };
            rate *= intervals[r.below (3)];
        }

        const bool reversed = r.chance (v.seg.reverseChance);
        const double start = v.capture + r.uni() * v.repeatS;
        const float pan = juce::MathConstants<float>::pi * (0.25f + r.range (-0.22f, 0.22f));

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
                l = inL;
                r = inR;
                return;

            case Kind::rest:
                l = r = 0.0f;
                return;

            case Kind::stutter:
            case Kind::roll:
            case Kind::reverse:
            case Kind::scatter:
            {
                const double rate = std::abs (rateAt (v, p) * v.slotRate * s.arpRatio (v.repeatIndex));
                const double pos = v.reversed ? v.capture + v.repeatS * rate - v.phase : v.capture + v.phase;
                v.phase += rate;
                l = history->read (0, pos, now);
                r = history->read (1, pos, now);
                gain *= v.repGain;
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
            {
                if (--v.spawnCountdown <= 0)
                    spawnGrain (v, now, p);

                l = r = 0.0f;

                for (auto& g : v.grains)
                {
                    if (! g.on)
                        continue;

                    const float x = (float) g.age / (float) g.length;
                    const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * x);
                    l += history->read (0, g.pos, now) * w * g.gainL;
                    r += history->read (1, g.pos, now) * w * g.gainR;
                    g.pos += g.rate;

                    if (++g.age >= g.length)
                        g.on = false;
                }

                gain *= 1.4f / std::sqrt (1.0f + 6.0f * s.density);
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

        l *= gain;
        r *= gain;
    }

    void Player::render (int64_t now, float inL, float inR, float& outL, float& outR, Weights& w)
    {
        outL = outR = 0.0f;
        w = {};

        if (current.endK >= 0 && now - current.startNow >= current.endK)
            beginPass (now);

        const bool repeats = current.kind == Kind::stutter || current.kind == Kind::roll
                          || current.kind == Kind::reverse || current.kind == Kind::scatter;

        if (repeats && (double) (now - current.startNow - current.repStart) >= current.repeatS)
            startRepeat (current, now);

        float l = 0.0f, r = 0.0f;
        renderVoice (current, now, inL, inR, l, r);
        outL += l * current.fade;
        outR += r * current.fade;

        if (current.kind == Kind::pass)
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

            if (t.kind == Kind::pass)
                w.pass += t.fade;
            else if (t.kind == Kind::rest)
                w.rest += t.fade;

            t.fade -= t.fadeStep;

            if (t.fade <= 0.0f)
                t.alive = false;
        }
    }

    //==========================================================================
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
