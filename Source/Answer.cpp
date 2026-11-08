#include "Answer.h"

namespace mimic
{
    namespace
    {
        float coeff (double seconds, double sampleRate)
        {
            return 1.0f - std::exp (-1.0f / (float) (seconds * sampleRate));
        }

        float hermite (float y0, float y1, float y2, float y3, float f)
        {
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }

        void equalPower (float pan, float& l, float& r)
        {
            const float theta = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            l = std::cos (theta) * juce::MathConstants<float>::sqrt2;
            r = std::sin (theta) * juce::MathConstants<float>::sqrt2;
        }

        constexpr float fifthUp = 1.4983071f;
    }

    //==========================================================================
    void Saturator::prepare (double sampleRate)
    {
        sr = sampleRate;
        dcCoeff = 1.0f - juce::MathConstants<float>::twoPi * 12.0f / (float) sr;
        reset();
    }

    void Saturator::reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            x1[c] = f1[c] = 0.0;
            dcX[c] = dcY[c] = toneZ[c] = 0.0f;
        }
    }

    void Saturator::set (float amount)
    {
        const float a = juce::jlimit (0.0f, 1.0f, amount);
        mix = juce::jmin (1.0f, a * 3.0f);
        drive = 1.0f + 2.0f * a + 14.0f * a * a;
        bias = 0.3f * a;
        biasOut = std::tanh (bias);
        // The same level as the clean signal at about -10 dBFS.
        makeup = 0.3f / juce::jmax (1.0e-4f, std::tanh (0.3f * drive + bias) - biasOut);
        // Hot tape loses its top.
        toneA = std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (17000.0f - 8000.0f * a, 0.45f * (float) sr) / (float) sr);
    }

    double Saturator::shaped (int ch, double x)
    {
        // F (x) = log cosh (d x + b) / d, the antiderivative of tanh (d x + b).
        auto logCosh = [] (double u)
        {
            const double au = std::abs (u);
            return au + std::log1p (std::exp (-2.0 * au)) - 0.69314718055994531;
        };

        const double d = drive;
        const double f = logCosh (d * x + bias) / d;
        const double dx = x - x1[ch];
        const double y = std::abs (dx) > 1.0e-6 ? (f - f1[ch]) / dx
                                                : std::tanh (d * 0.5 * (x + x1[ch]) + bias);
        x1[ch] = x;
        f1[ch] = f;
        return y - biasOut;
    }

    float Saturator::process (int ch, float x)
    {
        if (mix <= 0.0f)
            return x;

        float y = (float) shaped (ch, x) * makeup;

        // The bias leaves a little DC behind.
        const float dc = y - dcX[ch] + dcCoeff * dcY[ch];
        dcX[ch] = y;
        dcY[ch] = dc;
        y = dc;

        toneZ[ch] = y + toneA * (toneZ[ch] - y);
        return x + mix * (toneZ[ch] - x);
    }

    //==========================================================================
    void TapeDelay::prepare (double sampleRate, double maxSeconds)
    {
        sr = sampleRate;
        int size = 1;

        while (size < (int) (maxSeconds * sr) + 16)
            size <<= 1;

        for (auto& b : buffer)
            b.assign ((size_t) size, 0.0f);

        mask = size - 1;
        glide = coeff (0.15, sr);
        smooth = coeff (0.4, sr);
        wowInc = (float) (0.55 / sr);
        flutterInc = (float) (6.3 / sr);
        hpA = std::exp (-juce::MathConstants<float>::twoPi * 140.0f / (float) sr);
        reset();
    }

    void TapeDelay::reset()
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);

        write = 0;
        delay = target.delaySamples;
        feedback = target.feedback;

        for (int c = 0; c < 2; ++c)
            lp[c] = hp[c] = hpX[c] = 0.0f;

        drift = driftTarget = 0.0f;
        driftCountdown = 0;
    }

    float TapeDelay::read (int ch, float d) const
    {
        const float pos = (float) write - juce::jlimit (2.0f, (float) mask - 4.0f, d);
        const int i = (int) std::floor (pos);
        const float f = pos - (float) i;
        const auto& b = buffer[ch];
        return hermite (b[(size_t) ((i - 1) & mask)], b[(size_t) (i & mask)], b[(size_t) ((i + 1) & mask)], b[(size_t) ((i + 2) & mask)], f);
    }

    void TapeDelay::process (float inL, float inR, float& outL, float& outR)
    {
        delay += glide * (target.delaySamples - delay);
        feedback += smooth * (juce::jlimit (0.0f, 0.97f, target.feedback) - feedback);

        if (std::abs (target.toneHz - lastToneHz) > 1.0f)
        {
            lastToneHz = target.toneHz;
            lpA = std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (target.toneHz, 0.45f * (float) sr) / (float) sr);
        }

        // The head: slow wow, quick flutter, and a little wandering.
        if (--driftCountdown <= 0)
        {
            driftCountdown = (int) (0.3 * sr);
            driftTarget = rng.range (-1.0f, 1.0f);
        }

        drift += 0.00005f * (driftTarget - drift);
        wowPhase += wowInc;
        flutterPhase += flutterInc;
        wowPhase -= std::floor (wowPhase);
        flutterPhase -= std::floor (flutterPhase);

        const float wobble = target.wow * (float) sr
                           * (0.0024f * (0.7f * std::sin (juce::MathConstants<float>::twoPi * wowPhase) + 0.3f * drift)
                              + 0.00012f * std::sin (juce::MathConstants<float>::twoPi * flutterPhase));

        const float yl = read (0, delay + wobble);
        const float yr = read (1, delay + wobble * 0.85f + 0.0004f * (float) sr * target.wow);

        // Every pass: darker, thinner, a little driven.
        const float g = 1.0f + 3.0f * target.grit;
        float fb[2] { yl, yr };

        for (int c = 0; c < 2; ++c)
        {
            lp[c] = fb[c] + lpA * (lp[c] - fb[c]);
            const float h = hpA * (hp[c] + lp[c] - hpX[c]);
            hpX[c] = lp[c];
            hp[c] = h;
            fb[c] = std::tanh (g * h) / g;
        }

        const float pp = target.pingPong;
        const float mono = 0.5f * (inL + inR);
        const float wl = (1.0f - pp) * inL + pp * mono + feedback * ((1.0f - pp) * fb[0] + pp * fb[1]);
        const float wr = (1.0f - pp) * inR + feedback * ((1.0f - pp) * fb[1] + pp * fb[0]);

        buffer[0][(size_t) write] = wl;
        buffer[1][(size_t) write] = wr;
        write = (write + 1) & mask;

        outL = yl;
        outR = yr;
    }

    //==========================================================================
    void Capture::prepare (double rate, double seconds)
    {
        sampleRate = rate;
        capacity = (int) (seconds * rate);

        for (int c = 0; c < 2; ++c)
        {
            data[c].assign ((size_t) capacity + 4, 0.0f);
            smooth[c].assign ((size_t) capacity + 4, 0.0f);
        }

        length = numOnsets = 0;
    }

    float Capture::read (int ch, double pos, bool reversed, bool fast) const
    {
        if (reversed)
            pos = (double) (length - 1) - pos;

        if (pos < 0.0 || pos > (double) (length - 1))
            return 0.0f;

        const auto& b = fast ? smooth[ch] : data[ch];
        const int i = (int) pos;
        const float f = (float) (pos - i);
        const float y0 = b[(size_t) juce::jmax (0, i - 1)];
        const float y1 = b[(size_t) i];
        const float y2 = b[(size_t) juce::jmin (length - 1, i + 1)];
        const float y3 = b[(size_t) juce::jmin (length - 1, i + 2)];
        return hermite (y0, y1, y2, y3, f);
    }

    float Capture::mono (int index, bool reversed) const
    {
        if (reversed)
            index = length - 1 - index;

        if (index < 0 || index >= length)
            return 0.0f;

        return 0.5f * (data[0][(size_t) index] + data[1][(size_t) index]);
    }

    int Capture::startFor (int end, int want, int minimum) const
    {
        const int target = juce::jmax (0, end - want);
        int best = target, bestDistance = std::numeric_limits<int>::max();

        for (int i = 0; i < numOnsets; ++i)
        {
            const int o = onsets[(size_t) i];

            if (o > end - minimum)
                continue;

            const int distance = std::abs (o - target);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = o;
            }
        }

        // No onset near enough: a cut at the plain place.
        return bestDistance < want ? best : juce::jmax (0, juce::jmin (target, end - minimum));
    }

    float Capture::rmsOver (int from, int to) const
    {
        from = juce::jlimit (0, length, from);
        to = juce::jlimit (from, length, to);
        double sum = 0.0;
        int count = 0;

        for (int i = from; i < to; i += 4)
        {
            const float m = 0.5f * (data[0][(size_t) i] + data[1][(size_t) i]);
            sum += (double) m * m;
            ++count;
        }

        return count > 0 ? (float) std::sqrt (sum / count) : 0.0f;
    }

    //==========================================================================
    void TapeRead::start (const Capture* c, double from, double to, double r, bool rev)
    {
        capture = c;
        reversed = rev;
        rate = juce::jmax (0.05, r);

        if (rev)
        {
            begin = (double) (c->length - 1) - to;
            end = (double) (c->length - 1) - from;
        }
        else
        {
            begin = from;
            end = to;
        }

        begin = juce::jmax (0.0, begin);
        end = juce::jmin ((double) (c->length - 1), end);
        pos = begin;
        on = end > begin + 8.0;
        fadeLength = juce::jmin (0.003 * c->sampleRate * rate, 0.25 * (end - begin));
        wowDepth = 0.0f;
    }

    void TapeRead::setWow (float depth, float hz, double sampleRate)
    {
        wowDepth = depth;
        wowInc = (float) (hz / sampleRate);
    }

    double TapeRead::progress() const
    {
        return end > begin ? (pos - begin) / (end - begin) : 1.0;
    }

    bool TapeRead::render (float& l, float& r)
    {
        l = r = 0.0f;

        if (! active())
            return false;

        double rateNow = rate;

        if (wowDepth > 0.0f)
        {
            wowPhase += wowInc;
            wowPhase -= std::floor (wowPhase);
            rateNow *= 1.0 + wowDepth * std::sin (juce::MathConstants<float>::twoPi * wowPhase);
        }

        const bool fast = rateNow > 1.15;
        const float edge = (float) juce::jmin (1.0, juce::jmin (pos - begin, end - pos) / juce::jmax (1.0, fadeLength));
        // pos runs in the played direction; read() flips it when reversed.
        l = capture->read (0, pos, reversed, fast) * edge;
        r = capture->read (1, pos, reversed, fast) * edge;
        pos += rateNow;

        if (pos >= end)
            on = false;

        return true;
    }

    //==========================================================================
    void Stretch::prepare (double sampleRate)
    {
        length = 1;

        while (length < (int) (sampleRate * 0.04))
            length <<= 1;

        hop = length / 2;
        searchReach = (int) (sampleRate * 0.011);
        window.assign ((size_t) length, 0.0f);

        for (int i = 0; i < length; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) length);
    }

    void Stretch::start (const Capture* c, double f, double t, double sp, float p, bool rev, bool al)
    {
        capture = c;
        reversed = rev;
        align = al;

        // Work in the played direction: backwards is forwards through the
        // flipped capture.
        from = rev ? (double) (c->length - 1) - t : f;
        to = rev ? (double) (c->length - 1) - f : t;
        speed = juce::jmax (0.01, sp);
        pitch = juce::jlimit (0.2f, 4.0f, p);
        fast = pitch > 1.15f;
        nominal = from;
        lastStart = -1.0;
        countdown = 0;
        finishing = false;
        on = to > from + 64.0;

        for (auto& g : grains)
            g.on = false;
    }

    double Stretch::bestStart (double nominalStart, double natural) const
    {
        // The template: how the last grain goes on where the new one starts.
        constexpr int points = 256;
        const int stride = juce::jmax (1, hop / points);
        const int count = juce::jmin (points, hop / stride);
        float templ[points];
        double templEnergy = 0.0;

        for (int j = 0; j < count; ++j)
        {
            templ[j] = capture->mono ((int) (natural + (double) (j * stride) * pitch), reversed);
            templEnergy += (double) templ[j] * templ[j];
        }

        if (templEnergy < 1.0e-9)
            return nominalStart;

        auto score = [&] (double start)
        {
            double cross = 0.0, energy = 1.0e-9;

            for (int j = 0; j < count; ++j)
            {
                const float x = capture->mono ((int) (start + (double) (j * stride) * pitch), reversed);
                cross += (double) templ[j] * x;
                energy += (double) x * x;
            }

            return cross / std::sqrt (energy);
        };

        const double lowest = juce::jmax (0.0, nominalStart - searchReach);
        const double highest = juce::jmin ((double) capture->length - 1.0 - length * pitch, nominalStart + searchReach);

        if (highest <= lowest)
            return nominalStart;

        double best = nominalStart, bestScore = -1.0e30;

        for (double s = lowest; s <= highest; s += 3.0)
        {
            const double v = score (s);

            if (v > bestScore)
            {
                bestScore = v;
                best = s;
            }
        }

        const double coarse = best;

        for (double s = coarse - 2.0; s <= coarse + 2.0; s += 1.0)
        {
            if (s < lowest || s > highest)
                continue;

            const double v = score (s);

            if (v > bestScore)
            {
                bestScore = v;
                best = s;
            }
        }

        return best;
    }

    void Stretch::spawn()
    {
        if (finishing)
            return;

        // The last grain that still fits.
        if (nominal + (double) length * pitch > to)
            finishing = true;

        double start = nominal;

        if (align && lastStart >= 0.0)
            start = bestStart (nominal, lastStart + (double) hop * pitch);

        auto& g = grains[(size_t) nextGrain];
        g.on = true;
        g.pos = start;
        g.age = 0;
        nextGrain ^= 1;
        lastStart = start;
        nominal += speed * hop;
    }

    bool Stretch::render (float& l, float& r)
    {
        l = r = 0.0f;

        if (! active())
            return false;

        if (--countdown <= 0)
        {
            spawn();
            countdown = hop;
        }

        bool any = false;

        for (auto& g : grains)
        {
            if (! g.on)
                continue;

            any = true;
            const float w = window[(size_t) g.age];
            l += w * capture->read (0, g.pos, reversed, fast);
            r += w * capture->read (1, g.pos, reversed, fast);
            g.pos += pitch;

            if (++g.age >= length)
                g.on = false;
        }

        if (! any && finishing)
            on = false;

        return any;
    }

    //==========================================================================
    void Cloud::prepare (double sampleRate)
    {
        sr = sampleRate;

        for (size_t i = 0; i < hann.size(); ++i)
            hann[i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (hann.size() - 1));
    }

    void Cloud::start (const Capture* c, const Settings& settings, uint64_t seed)
    {
        capture = c;
        s = settings;
        s.from = juce::jlimit (0.0, (double) c->length - 1.0, s.from);
        s.to = juce::jlimit (s.from, (double) c->length - 1.0, s.to);
        scanPos = s.from;
        spawning = true;
        countdown = 0.0f;
        rng = Rng (seed);

        // Overlapping grains add up roughly like noise.
        const float overlap = juce::jmax (1.0f, s.density * s.grainMs * 0.001f);
        norm = 1.15f / std::sqrt (overlap);

        for (auto& g : grains)
            g.on = false;
    }

    bool Cloud::active() const
    {
        if (capture == nullptr)
            return false;

        if (spawning)
            return true;

        for (const auto& g : grains)
            if (g.on)
                return true;

        return false;
    }

    void Cloud::spawn()
    {
        Grain* slot = nullptr;

        for (auto& g : grains)
            if (! g.on) { slot = &g; break; }

        if (slot == nullptr)
            return;

        auto& g = *slot;
        g.length = juce::jmax (64, (int) (s.grainMs * 0.001f * (float) sr * rng.range (0.75f, 1.25f)));
        const float cents = rng.range (-s.detuneCents, s.detuneCents);
        g.rate = s.pitches[(size_t) rng.below (4)] * std::exp2 (cents / 1200.0f);
        g.reversed = rng.chance (s.reverse);

        const double centre = scanPos + rng.range (-1.0f, 1.0f) * s.jitterMs * 0.001 * sr;
        const double span = g.length * g.rate;
        double startPos = g.reversed ? centre + 0.5 * span : centre - 0.5 * span;
        startPos = juce::jlimit (0.0, (double) capture->length - 1.0, startPos);
        g.pos = startPos;
        g.age = 0;
        g.on = true;

        float pl = 1.0f, pr = 1.0f;
        equalPower (rng.range (-1.0f, 1.0f) * s.spread, pl, pr);
        g.gainL = pl * norm;
        g.gainR = pr * norm;
    }

    void Cloud::render (float& l, float& r)
    {
        l = r = 0.0f;

        if (capture == nullptr)
            return;

        if (spawning)
        {
            countdown -= 1.0f;

            if (countdown <= 0.0f)
            {
                spawn();
                countdown += (float) sr / juce::jmax (1.0f, s.density) * rng.range (0.6f, 1.4f);
            }

            scanPos += s.scan;

            if (scanPos > s.to)
            {
                if (s.scan > 0.0)
                    spawning = false;
                scanPos = s.to;
            }
        }

        const float tableScale = (float) (hann.size() - 1);

        for (auto& g : grains)
        {
            if (! g.on)
                continue;

            const float phase = (float) g.age / (float) g.length * tableScale;
            const int k = (int) phase;
            const float w = hann[(size_t) k] + (phase - (float) k) * (hann[(size_t) juce::jmin (k + 1, (int) tableScale)] - hann[(size_t) k]);
            const bool fast = g.rate > 1.15;
            l += w * g.gainL * capture->read (0, g.pos, false, fast);
            r += w * g.gainR * capture->read (1, g.pos, false, fast);
            g.pos += g.reversed ? -g.rate : g.rate;

            if (++g.age >= g.length || g.pos < 0.0 || g.pos > (double) capture->length - 1.0)
                g.on = false;
        }
    }

    //==========================================================================
    void Cave::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        reverb.prepare (sr);
        octave.prepare (sr);
        shimmerTone.set (5000.0f, 0.6f, sr);
        const auto size = (size_t) juce::jmax (256, maxBlock);
        feedL.assign (size, 0.0f);
        feedR.assign (size, 0.0f);
        prevL.assign (size, 0.0f);
        prevR.assign (size, 0.0f);
        lastSpace = lastDecay = -1.0f;
        reset();
    }

    void Cave::reset()
    {
        reverb.reset();
        octave.reset();
        shimmerTone.reset();
        std::fill (prevL.begin(), prevL.end(), 0.0f);
        std::fill (prevR.begin(), prevR.end(), 0.0f);
        prevCount = 0;
        shimmer = 0.0f;
    }

    void Cave::set (float space, float decayScale, float shimmerAmount)
    {
        shimmerTarget = juce::jlimit (0.0f, 0.3f, shimmerAmount);

        if (std::abs (space - lastSpace) < 0.01f && std::abs (decayScale - lastDecay) < 0.01f)
            return;

        lastSpace = space;
        lastDecay = decayScale;
        gargoyle::Reverb::Settings rs;
        rs.space = 0;
        rs.size = 0.55f + 0.4f * space;
        rs.decay = (1.2f + 5.5f * space) * decayScale;
        rs.early = 0.3f;
        rs.predelayMs = 18.0f + 40.0f * space;
        rs.lows = 0.85f;
        rs.highs = 0.6f;
        rs.diffuse = 0.85f;
        rs.drift = 0.6f;
        rs.brightness = 7500.0f;
        reverb.setSettings (rs);
    }

    void Cave::process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        // Shimmer: the cave's own last output, an octave up, fed back in.
        for (int i = 0; i < n; ++i)
        {
            shimmer += 0.0005f * (shimmerTarget - shimmer);
            float l = i < prevCount ? prevL[(size_t) i] : 0.0f;
            float r = i < prevCount ? prevR[(size_t) i] : 0.0f;

            if (shimmer > 1.0e-4f)
            {
                octave.process (l, r, 2.0f);
                l = std::tanh (shimmerTone.process (0, l, 0));
                r = std::tanh (shimmerTone.process (1, r, 0));
            }
            else
            {
                l = r = 0.0f;
            }

            feedL[(size_t) i] = inL[i] + shimmer * l;
            feedR[(size_t) i] = inR[i] + shimmer * r;
        }

        reverb.process (feedL.data(), feedR.data(), outL, outR, n);

        std::copy (outL, outL + n, prevL.begin());
        std::copy (outR, outR + n, prevR.begin());
        prevCount = n;
    }

    //==========================================================================
    void Answerer::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        int size = 1;

        // Twenty seconds and more: Memory reaches back into them.
        while (size < (int) (sr * 20.0))
            size <<= 1;

        for (auto& b : ring)
            b.assign ((size_t) size, 0.0f);

        ringMask = size - 1;
        blockLevel.assign ((size_t) (size / levelBlock), 0.0f);
        detector.prepare (sr);
        envAtt = coeff (0.001, sr);
        envRel = coeff (0.06, sr);
        duckAtt = coeff (0.008, sr);
        duckRel = coeff (0.35, sr);

        for (auto& s : slots)
        {
            s.capture.prepare (sr, 4.5);
            s.stretch.prepare (sr);
            s.stretch2.prepare (sr);
            s.cloud.prepare (sr);
            s.sat.prepare (sr);
        }

        delay.prepare (sr, 2.6);
        cave.prepare (sr, chunk);
        juce::ignoreUnused (maxBlock);
        reset();
    }

    void Answerer::reset()
    {
        for (auto& b : ring)
            std::fill (b.begin(), b.end(), 0.0f);

        now = 0;
        detector.reset();
        env = phrasePeak = phraseSum = 0.0f;
        phraseStart = lastOnsetAt = -1;
        phraseSamples = 0;
        numPhraseOnsets = 0;
        inPhrase = false;
        pendingAt = -1;
        phraseCount = answerCount = 0;
        lastTrick = -1;
        lastPeak = endEnv = 0.0f;
        answeredLast = true;
        delayHold = 0;
        asleep = true;

        for (auto& s : slots)
        {
            s.on = false;
            s.sat.reset();
        }

        delay.reset();
        cave.reset();
        std::fill (blockLevel.begin(), blockLevel.end(), 0.0f);
        blockSum = 0.0f;
        blockCount = 0;
        onsetTimes.fill (-1);
        onsetWrite = 0;
        swing[0] = swing[1] = 0.5f;
        swingHeard[0] = swingHeard[1] = 0;
        masterLow.reset();
        masterHigh.reset();
        filterSmooth = 0.0f;
        filterCountdown = 0;
        duck = tailDuck = 1.0f;
        levelSmooth = 0.0f;
        quietFor = 1 << 30;
        shownState = idle;
        shownWait = shownProgress = 0.0f;
        shownTrick = -1;
    }

    bool Answerer::busy() const
    {
        for (const auto& s : slots)
            if (s.on)
                return true;

        return pendingAt >= 0 || ! asleep;
    }

    //==========================================================================
    void Answerer::listen (float mono, double ppqNow, const AnswerSettings& s)
    {
        const float a = std::abs (mono);
        env += (a > env ? envAtt : envRel) * (a - env);
        const bool onset = detector.process (mono);

        // Patience: how long it waits after the last hit, and how far the
        // sound must have died away, before it takes the gap.
        const float p = s.patience;
        const auto patience = (int64_t) ((0.12 + 0.6 * p) * sr);
        const float drop = juce::Decibels::decibelsToGain (-(7.0f + 15.0f * p));

        if (onset)
        {
            onsetTimes[(size_t) (onsetWrite++ & 255)] = now - (int64_t) (0.002 * sr);

            // The swing: where the off-beat eighths and sixteenths land.
            if (s.playing)
            {
                for (int g = 0; g < 2; ++g)
                {
                    const double pair = g == 0 ? 1.0 : 0.5;
                    const double phase = std::fmod (ppqNow, pair) / pair;

                    if (phase > 0.42 && phase < 0.7)
                    {
                        // A plain average at first, then a moving one.
                        ++swingHeard[g];
                        swing[g] += juce::jmax (0.06f, 1.0f / (float) swingHeard[g]) * ((float) phase - swing[g]);
                    }
                }
            }
        }

        // A phrase starts on a hit, or when a sound swells in without one
        // (well up from the quietest it has been since the last phrase).
        if (! inPhrase)
            endEnv = juce::jmin (endEnv, env);

        if (! inPhrase && env > 0.0015f
            && (onset || env > juce::jmax (0.003f, endEnv * 2.5f)))
        {
            inPhrase = true;
            phraseStart = now - (int64_t) (0.006 * sr);
            phrasePeak = env;
            phraseSum = 0.0f;
            phraseSamples = 0;
            numPhraseOnsets = 0;
            lastOnsetAt = now;

            // The player is back: whatever the Mimic was saying, it steps
            // back (a hum lingers a while).
            pendingAt = -1;
            auto& slot = slots[(size_t) active];

            if (slot.on && ! slot.retreating && ! slot.inBreak)
            {
                slot.retreating = true;
                slot.fadeStep = 1.0f / (float) (sr * (slot.trick == Trick::hum ? 1.6 : 0.16));
            }
        }

        if (onset && inPhrase)
        {
            lastOnsetAt = now;

            if (numPhraseOnsets < (int) phraseOnsets.size())
                phraseOnsets[(size_t) numPhraseOnsets++] = now - (int64_t) (0.002 * sr);
        }

        if (! inPhrase)
            return;

        phrasePeak = juce::jmax (phrasePeak, env);
        phraseSum += mono * mono;
        ++phraseSamples;

        const int64_t since = now - lastOnsetAt;
        shownWait = juce::jlimit (0.0f, 1.0f, (float) since / (float) juce::jmax ((int64_t) 1, patience));

        if (since > patience && (env < phrasePeak * drop || env < 2.0e-4f))
        {
            inPhrase = false;
            endEnv = env;
            phraseEnded (ppqNow, s);
        }
    }

    void Answerer::phraseEnded (double ppqNow, const AnswerSettings& s)
    {
        ++phraseCount;
        lastPeak = phrasePeak;

        if (now - phraseStart < (int64_t) (0.1 * sr) || phrasePeak < 0.002f || s.level <= 0.0f)
            return;

        Rng rng (hash64 ((uint64_t) phraseCount, (uint64_t) s.seed + 0xa115ull));

        // A hungrier Mimic answers more often. It always answers the first
        // phrase, and never lets two go by in a row.
        if (answeredLast && phraseCount > 1 && ! rng.chance (0.2f + 0.8f * s.hunger))
        {
            answeredLast = false;
            return;
        }

        answeredLast = true;

        bool any = false;

        for (int t = 0; t < numTricks; ++t)
            any = any || (s.enabled & (1u << t)) != 0;

        if (! any)
            return;

        pendingTrick = pickTrick (s, rng);
        pendingSeed = rng.next();
        pendingFrom = juce::jmax (phraseStart, now - (int64_t) slots[0].capture.capacity + (int64_t) (0.02 * sr));
        pendingTo = now;

        // In time: the answer starts on the next sixteenth (eighth when it
        // is less hungry). Without a running transport, at once.
        if (s.playing && s.ppqPerSample > 0.0)
        {
            const double grid = s.patience < 0.5f ? 0.25 : 0.5;
            const double next = nextGridLine (ppqNow, grid);
            pendingAt = now + (int64_t) std::llround ((next - ppqNow) / s.ppqPerSample);
        }
        else
        {
            pendingAt = now + (int64_t) (0.03 * sr);
        }
    }

    float Answerer::heardSwing (int g) const
    {
        // Until it has heard a few off-beats, straight. A hair off straight
        // is straight.
        if (swingHeard[g] < 8 || swing[g] < 0.52f)
            return 0.5f;

        return juce::jlimit (0.5f, 0.75f, swing[g]);
    }

    double Answerer::nextGridLine (double ppqNow, double grid) const
    {
        // The grid with the track's swing: every other line comes late.
        const double pair = 2.0 * grid;
        const double r = heardSwing (grid > 0.3 ? 0 : 1);
        const double base = std::floor (ppqNow / pair) * pair;

        for (int k = 0; k < 4; ++k)
        {
            const double line = base + (k / 2) * pair + ((k & 1) ? pair * r : 0.0);

            if (line > ppqNow + 1.0e-6)
                return line;
        }

        return base + 2.0 * pair;
    }

    Trick Answerer::pickTrick (const AnswerSettings& s, Rng& rng)
    {
        //                               parrot ghost cloud back  dub  mock  hum  radio
        static const float weights[4][numTricks] {
            /* percussive */             { 1.0f, 1.5f, 1.0f, 2.0f, 3.0f, 3.0f, 0.3f, 2.0f },
            /* melodic */                { 2.0f, 3.0f, 2.0f, 2.0f, 2.0f, 1.5f, 1.5f, 1.0f },
            /* harmonic */               { 1.0f, 2.0f, 3.0f, 2.0f, 1.5f, 0.5f, 3.0f, 1.0f },
            /* vocal */                  { 1.0f, 3.0f, 2.0f, 2.5f, 2.0f, 2.5f, 1.0f, 2.0f } };

        // More mockery: fewer plain repeats, more mocking and broken radios.
        const float m = s.mockery;
        const float bend[numTricks] { 1.6f - 1.4f * m, 0.6f + 0.8f * m, 1.0f, 0.7f + 0.6f * m,
                                      1.0f, 0.4f + 1.2f * m, 1.2f - 0.4f * m, 0.3f + 1.4f * m };

        float w[numTricks] {}, total = 0.0f;
        int enabledCount = 0;

        for (int t = 0; t < numTricks; ++t)
            enabledCount += (s.enabled >> t) & 1u;

        for (int t = 0; t < numTricks; ++t)
        {
            if ((s.enabled & (1u << t)) == 0)
                continue;

            w[t] = weights[juce::jlimit (0, 3, s.material)][t] * bend[t];

            // Not the same trick twice in a row, mostly.
            if (t == lastTrick && enabledCount > 1)
                w[t] *= 0.2f;

            total += w[t];
        }

        float pick = rng.uni() * total;

        for (int t = 0; t < numTricks; ++t)
        {
            if (w[t] <= 0.0f)
                continue;

            pick -= w[t];

            if (pick <= 0.0f)
                return (Trick) t;
        }

        for (int t = numTricks - 1; t >= 0; --t)
            if (w[t] > 0.0f)
                return (Trick) t;

        return Trick::parrot;
    }

    void Answerer::capture (Capture& c, int64_t from, int64_t to)
    {
        const int n = (int) juce::jlimit ((int64_t) 0, (int64_t) c.capacity, to - from);
        c.length = n;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                c.data[ch][(size_t) i] = ring[ch][(size_t) ((from + i) & ringMask)];

        // The copy read when going faster: lowpassed below a quarter of the
        // sample rate (two passes of a 2-pole, forwards: 4 poles).
        {
            Svf a, b;
            const float cutoff = (float) (0.2 * sr);
            a.set (cutoff, 0.54f, sr);
            b.set (cutoff, 1.31f, sr);

            for (int ch = 0; ch < 2; ++ch)
            {
                a.reset();
                b.reset();

                for (int i = 0; i < n; ++i)
                    c.smooth[ch][(size_t) i] = b.process (0, a.process (0, c.data[ch][(size_t) i], 0), 0);
            }
        }

        // Where the notes start.
        c.numOnsets = 0;

        for (int k = onsetWrite - 256; k < onsetWrite; ++k)
        {
            if (k < 0)
                continue;

            const auto t = onsetTimes[(size_t) (k & 255)];
            const auto o = t - from;

            if (t >= 0 && o >= 0 && o < n && c.numOnsets < (int) c.onsets.size())
                c.onsets[(size_t) c.numOnsets++] = (int) o;
        }

        // Level, and the end of the sound (the quiet after it trimmed off).
        double sum = 0.0;
        float peak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (c.data[0][(size_t) i] + c.data[1][(size_t) i]);
            sum += (double) m * m;
            peak = juce::jmax (peak, std::abs (m));
        }

        c.peak = peak;
        c.rms = n > 0 ? (float) std::sqrt (sum / n) : 0.0f;

        int end = n;
        const float quiet = peak * 0.02f;
        const int block = juce::jmax (1, (int) (0.005 * sr));

        while (end > block)
        {
            float m = 0.0f;

            for (int i = end - block; i < end; ++i)
                m = juce::jmax (m, std::abs (c.data[0][(size_t) i]) + std::abs (c.data[1][(size_t) i]));

            if (m * 0.5f > quiet)
                break;

            end -= block;
        }

        c.length = juce::jmin (n, end + (int) (0.03 * sr));
        c.lastOnset = c.numOnsets > 0 ? c.onsets[(size_t) c.numOnsets - 1] : 0;

        if (c.lastOnset > c.length - (int) (0.05 * sr))
            c.lastOnset = juce::jmax (0, c.numOnsets > 1 ? c.onsets[(size_t) c.numOnsets - 2] : 0);

        // A steady place in the last note, past its attack.
        const int noteLength = c.length - c.lastOnset;
        c.sustainAt = juce::jlimit (0, juce::jmax (0, c.length - 1),
                                    c.lastOnset + juce::jmax ((int) (0.06 * sr), (int) (0.35 * noteLength)));
        c.sustainAt = juce::jmin (c.sustainAt, juce::jmax (0, c.length - (int) (0.05 * sr)));
    }

    //==========================================================================
    void Answerer::begin (Slot& slot, Trick trick, const AnswerSettings& s, uint64_t seed)
    {
        Rng rng (seed);
        auto& c = slot.capture;
        const float m = s.mockery, sp = s.space;
        const double beat = s.samplesPerBeat;
        const bool tonal = s.material != percussive;
        const int end = c.length;

        slot.on = true;
        slot.trick = trick;
        slot.inBreak = false;
        slot.mockSwing = 0.5f;
        slot.retreating = false;
        slot.swell = false;
        slot.age = 0;
        slot.env = 0.0f;
        slot.fade = 1.0f;
        slot.fadeStep = 0.0f;
        slot.attack = (float) (0.006 * sr);
        slot.releaseSamples = (float) (0.03 * sr);
        slot.dry = 1.0f;
        slot.toDelay = 0.12f * sp;
        slot.toCave = 0.25f * sp;
        slot.useHp = slot.useLp = slot.useBand = false;
        slot.radio = slot.mock = false;
        slot.tape = {};
        slot.tape2 = {};
        slot.tapeGain = slot.tape2Gain = slot.stretchGain = slot.stretch2Gain = slot.cloudGain = 0.0f;
        slot.tapePanL = slot.tapePanR = slot.tape2PanL = slot.tape2PanR = 1.0f;
        slot.stretch.start (&c, 0, 0, 1.0, 1.0f, false, false);
        slot.stretch2.start (&c, 0, 0, 1.0, 1.0f, false, false);
        slot.cloud.stop();
        slot.hp.reset();
        slot.lp.reset();
        slot.band.reset();
        slot.filterCountdown = 0;
        slot.lpFrom = slot.lpTo = 18000.0f;
        slot.sat.reset();

        float heatScale = 1.0f, shimmer = 0.0f, decayScale = 1.0f;
        int from = 0;
        double length = (double) end;
        float panSpread = 0.3f;

        // Faithful tricks start on a note, one to two beats from the end.
        auto lastBeats = [&] (double beats, double minimumSeconds)
        {
            return c.startFor (end, (int) (beats * beat), (int) (minimumSeconds * sr));
        };

        TapeDelay::Settings echo;
        echo.delaySamples = (float) juce::jmin (2.4 * sr, 0.75 * beat);
        echo.feedback = 0.25f + 0.25f * sp;
        echo.toneHz = 4500.0f;
        echo.wow = 0.2f;
        echo.pingPong = 0.5f;
        echo.grit = 0.2f;
        bool dub = false;

        switch (trick)
        {
            case Trick::parrot:
            {
                from = lastBeats (rng.chance (0.5f) ? 1.0 : 2.0, 0.25);
                double rate = 1.0;

                // A mocking parrot sometimes squeaks or growls it back.
                if (m > 0.55f && rng.chance (m - 0.3f))
                    rate = rng.chance (0.5f) ? 2.0 : 0.5;

                slot.tape.start (&c, from, end, rate, false);
                slot.tape.setWow (0.0015f + 0.006f * m, 0.7f, sr);
                slot.tapeGain = 1.0f;
                length = (end - from) / rate;
                slot.useLp = slot.useHp = true;
                slot.lpFrom = slot.lpTo = 7500.0f - 3000.0f * m;
                slot.toDelay = 0.25f + 0.35f * sp;
                slot.toCave = 0.2f + 0.35f * sp;
                break;
            }

            case Trick::ghost:
            {
                from = lastBeats (2.0, 0.3);
                length = end - from;

                if (tonal)
                {
                    if (m < 0.35f)
                    {
                        // An octave up over the phrase itself.
                        slot.stretch.start (&c, from, end, 1.0, 2.0f, false, true);
                        slot.stretchGain = 0.8f;
                        slot.tape.start (&c, from, end, 1.0, false);
                        slot.tapeGain = 0.4f;
                        shimmer = 0.2f;
                    }
                    else if (m < 0.7f)
                    {
                        slot.stretch.start (&c, from, end, 1.0, 0.5f, false, true);
                        slot.stretchGain = 0.9f;
                        slot.stretch2.start (&c, from, end, 1.0, 2.0f, false, true);
                        slot.stretch2Gain = 0.45f;
                        shimmer = 0.15f;
                    }
                    else
                    {
                        slot.stretch.start (&c, from, end, 1.0, 0.5f, false, true);
                        slot.stretchGain = 0.85f;
                        slot.stretch2.start (&c, from, end, 1.0, rng.chance (0.5f) ? 0.25f : fifthUp, false, true);
                        slot.stretch2Gain = 0.55f;
                        shimmer = 0.1f;
                    }
                }
                else
                {
                    // Drums: tape speed. Slow and heavy, or quick and small.
                    const bool down = rng.chance (0.6f);
                    const double rate = down ? 0.5 : 2.0;
                    const int src = down ? (int) juce::jmax ((double) from, end - beat) : from;
                    slot.tape.start (&c, src, end, rate, false);
                    slot.tapeGain = 1.0f;
                    length = (end - src) / rate;

                    if (m > 0.5f)
                    {
                        slot.tape2.start (&c, src, end, down ? 1.0 : 0.5, false);
                        slot.tape2Gain = 0.35f;
                    }
                }

                slot.toCave = 0.45f + 0.35f * sp;
                slot.toDelay = 0.15f;
                panSpread = 0.15f;
                break;
            }

            case Trick::cloud:
            {
                Cloud::Settings cs;
                cs.from = 0.0;
                cs.to = end;
                cs.scan = 0.5 - 0.33 * m;
                cs.grainMs = tonal ? 110.0f + 90.0f * (1.0f - m) : 45.0f + 45.0f * (1.0f - m);
                cs.density = 26.0f + 30.0f * m;
                cs.jitterMs = 20.0f + 70.0f * m;
                cs.reverse = 0.1f + 0.45f * m;
                cs.spread = 1.0f;

                if (m < 0.3f)
                    cs.pitches = { 1.0f, 1.0f, 1.0f, 1.0f };
                else if (m < 0.6f)
                    cs.pitches = { 1.0f, 1.0f, 2.0f, 1.0f };
                else
                    cs.pitches = { 1.0f, 2.0f, 0.5f, fifthUp };

                slot.cloud.start (&c, cs, rng.next());
                slot.cloudGain = 1.1f;
                length = juce::jmin (6.0 * sr, end / cs.scan);
                slot.attack = (float) (0.08 * sr);
                slot.releaseSamples = (float) (0.4 * length);
                slot.dry = 0.85f;
                slot.toCave = 0.55f + 0.35f * sp;
                slot.toDelay = 0.1f + 0.2f * sp;
                shimmer = 0.1f + 0.15f * m;
                decayScale = 1.3f;
                break;
            }

            case Trick::backwards:
            {
                // Ends on the grid, so the swell lands like an upbeat.
                const double beats = rng.chance (0.5f) ? 1.0 : 2.0;
                from = lastBeats (beats, 0.25);
                length = end - from;

                if (tonal && m > 0.6f && rng.chance (0.6f))
                {
                    slot.stretch.start (&c, from, end, 1.0, 0.5f, true, true);
                    slot.stretchGain = 1.0f;
                }
                else
                {
                    slot.tape.start (&c, from, end, 1.0, true);
                    slot.tapeGain = 1.0f;
                }

                slot.swell = true;
                slot.toCave = 0.5f + 0.4f * sp;
                slot.toDelay = 0.15f + 0.2f * sp;
                decayScale = 1.2f;
                break;
            }

            case Trick::dub:
            {
                from = c.lastOnset;
                const int to = juce::jmin (end, from + (int) juce::jmin (beat, 1.2 * sr));
                slot.tape.start (&c, from, to, 1.0, false);
                slot.tapeGain = 1.0f;
                length = to - from;
                slot.dry = 0.8f;
                slot.toDelay = 1.0f;
                slot.toCave = 0.2f + 0.25f * sp;

                static constexpr double times[] { 0.75, 0.75, 1.0, 0.5, 1.5 };
                const double t = times[rng.below (5)] * beat;
                echo.delaySamples = (float) juce::jmin (2.4 * sr, t);
                echo.feedback = 0.55f + 0.37f * m;
                echo.pingPong = 0.75f;
                echo.wow = 0.25f + 0.6f * m;
                echo.grit = 0.3f + 0.6f * m;
                echo.toneHz = 4200.0f - 2200.0f * m;
                dub = true;
                heatScale = 1.2f;

                // The tape slows to a crawl: the echoes dive.
                slot.spiralFrom = slot.spiralTo = echo.delaySamples;

                if (m > 0.6f && rng.chance (m))
                    slot.spiralTo = (float) juce::jmin (2.4 * sr, 2.0 * t);

                delayHold = (int64_t) ((2.0 + 5.0 * m) * sr);
                break;
            }

            case Trick::mock:
            {
                slot.mock = true;
                slot.mockFrom = c.lastOnset;
                const double slice = juce::jmax (0.03 * sr, juce::jmin ((double) (end - c.lastOnset), beat * (rng.chance (0.5f) ? 0.25 : 0.125)));
                slot.mockTo = slot.mockFrom + slice;
                const bool triplets = m > 0.5f && rng.chance (0.4f);
                slot.mockStep = beat * (triplets ? 1.0 / 6.0 : 0.25);
                // Sixteenths swing with the track.
                slot.mockSwing = triplets || ! s.playing ? 0.5f : heardSwing (1);
                slot.mockRepeats = 4 + (int) (8.0f * m);
                slot.mockIndex = 0;
                slot.mockNext = 0;
                slot.mockGain = 1.0f;
                slot.mockDecay = 0.86f;
                slot.mockOctaves = m >= 0.35f && m < 0.7f;
                slot.mockPitchStep = m >= 0.7f ? (rng.chance (0.6f) ? 1.0f : -1.0f) : 0.0f;
                slot.mockRoll = ! tonal && m > 0.5f;
                slot.mockPan = rng.chance (0.5f) ? 0.6f : -0.6f;
                length = slot.mockRepeats * slot.mockStep + slice;
                slot.toDelay = 0.2f + 0.3f * sp;
                slot.toCave = 0.2f + 0.3f * sp;
                panSpread = 0.0f;
                break;
            }

            case Trick::hum:
            {
                Cloud::Settings cs;
                cs.from = c.sustainAt - 0.04 * sr;
                cs.to = c.sustainAt + 0.04 * sr;
                cs.scan = 0.0;
                cs.grainMs = 90.0f + 90.0f * rng.uni();
                cs.density = 45.0f;
                cs.jitterMs = 25.0f;
                cs.reverse = 0.15f;
                cs.spread = 0.8f;
                cs.detuneCents = 8.0f;

                if (m < 0.3f)
                    cs.pitches = { 1.0f, 1.0f, 1.0f, 1.0f };
                else if (m < 0.6f)
                    cs.pitches = { 1.0f, 1.0f, 2.0f, 0.5f };
                else
                    cs.pitches = { 1.0f, 0.5f, 2.0f, fifthUp };

                slot.cloud.start (&c, cs, rng.next());
                slot.cloudGain = 0.8f;
                length = (2.5 + 5.0 * sp) * sr;
                slot.attack = (float) (0.25 * sr);
                slot.releaseSamples = (float) (0.45 * length);
                slot.useLp = true;
                slot.lpFrom = 9000.0f;
                slot.lpTo = 1800.0f;
                slot.dry = 0.9f;
                slot.toCave = 0.5f + 0.4f * sp;
                slot.toDelay = 0.05f;
                shimmer = 0.05f + 0.2f * m;
                decayScale = 1.4f;
                break;
            }

            case Trick::radio:
            {
                from = lastBeats (2.0, 0.3);
                const double rate = m > 0.75f && rng.chance (0.4f) ? 2.0 : 1.0;
                slot.tape.start (&c, from, end, rate, false);
                slot.tape.setWow (0.004f + 0.01f * m, 4.0f, sr);
                slot.tapeGain = 1.0f;
                length = (end - from) / rate;
                slot.radio = true;
                slot.useBand = slot.useHp = true;
                slot.crushStep = std::exp2 (-(9.0f - 5.0f * m)) * juce::jmax (0.05f, c.peak);
                slot.holdLength = (1.0f + std::floor (m * 4.0f)) * (float) (sr / 48000.0);
                slot.holdCount = 0.0f;
                heatScale = 1.0f;
                slot.toDelay = 0.3f;
                slot.toCave = 0.15f + 0.2f * sp;
                panSpread = 0.6f;
                break;
            }
        }

        if (! dub && delayHold <= 0)
            delay.set ({ echo.delaySamples, echo.feedback, echo.toneHz, echo.wow, echo.pingPong, echo.grit });
        else if (dub)
            delay.set (echo);

        cave.set (sp, decayScale, shimmer);

        // Saturation: Heat for all of them; the radio and the dub always cook.
        float heatNow = s.heat * heatScale;

        if (trick == Trick::radio)
            heatNow = juce::jmax (0.7f, heatNow);
        else if (trick == Trick::dub)
            heatNow = juce::jmax (0.25f, heatNow);

        slot.sat.set (juce::jmin (1.0f, heatNow));

        equalPower (rng.range (-1.0f, 1.0f) * panSpread, slot.panL, slot.panR);

        slot.hp.set (trick == Trick::radio ? 450.0f : 110.0f, 0.7071f, sr);
        slot.band.set (1300.0f, 0.8f, sr);
        slot.lp.set (slot.lpFrom, 0.7071f, sr);
        slot.length = juce::jmax ((int64_t) (0.05 * sr), (int64_t) length);
        slot.releaseSamples = juce::jmin (slot.releaseSamples, 0.5f * (float) slot.length);
        slot.attack = juce::jmin (slot.attack, 0.3f * (float) slot.length);

        // As loud as the phrase was, a little behind it.
        int a = from, b = end;

        if (trick == Trick::hum)
        {
            a = c.sustainAt - (int) (0.05 * sr);
            b = c.sustainAt + (int) (0.05 * sr);
        }
        else if (trick == Trick::mock)
        {
            a = (int) slot.mockFrom;
            b = (int) slot.mockTo;
        }
        else if (trick == Trick::cloud)
        {
            a = 0;
        }

        const float fragment = c.rmsOver (a, b);
        const float trickGain = trick == Trick::radio ? 1.6f : (trick == Trick::hum ? 0.75f : (trick == Trick::cloud ? 0.9f : 0.85f));
        slot.gain = fragment > 1.0e-6f ? juce::jlimit (0.25f, 4.0f, c.rms / fragment) * trickGain * 0.75f : 0.0f;

        lastTrick = (int) trick;
        ++answerCount;
    }

    void Answerer::startMockRepeat (Slot& slot)
    {
        const int k = slot.mockIndex;
        double rate = 1.0;

        if (slot.mockOctaves)
            rate = (k & 1) ? 2.0 : 1.0;

        if (slot.mockPitchStep != 0.0f)
            rate = std::exp2 (slot.mockPitchStep * (float) k / 12.0f);

        // Each repeat on its own reader, so the last one rings out under it.
        auto& reader = (k & 1) ? slot.tape2 : slot.tape;
        reader.start (&slot.capture, slot.mockFrom, slot.mockTo, rate, false);
        const float gain = slot.mockGain * std::pow (slot.mockDecay, (float) k);
        float pl = 1.0f, pr = 1.0f;
        equalPower ((k & 1) ? -slot.mockPan : slot.mockPan, pl, pr);

        if (k & 1)
        {
            slot.tape2Gain = gain;
            slot.tape2PanL = pl;
            slot.tape2PanR = pr;
        }
        else
        {
            slot.tapeGain = gain;
            slot.tapePanL = pl;
            slot.tapePanR = pr;
        }

        // Rolls on drums: the second half comes twice as fast.
        double step = slot.mockStep;

        if (slot.mockSwing > 0.5f)
            step = 2.0 * slot.mockStep * ((k & 1) ? 1.0 - slot.mockSwing : slot.mockSwing);

        if (slot.mockRoll && k >= slot.mockRepeats / 2)
            step *= 0.5;

        slot.mockNext = slot.age + (int64_t) step;
        ++slot.mockIndex;
    }

    void Answerer::renderSlot (Slot& slot, float& outL, float& outR, const AnswerSettings&)
    {
        outL = outR = 0.0f;

        if (! slot.on)
            return;

        if (slot.mock && slot.mockIndex < slot.mockRepeats && slot.age >= slot.mockNext)
            startMockRepeat (slot);

        float l = 0.0f, r = 0.0f, a, b;

        if (slot.tape.render (a, b))
        {
            l += a * slot.tapeGain * slot.tapePanL;
            r += b * slot.tapeGain * slot.tapePanR;
        }

        if (slot.tape2.render (a, b))
        {
            l += a * slot.tape2Gain * slot.tape2PanL;
            r += b * slot.tape2Gain * slot.tape2PanR;
        }

        if (slot.stretch.render (a, b))
        {
            l += a * slot.stretchGain;
            r += b * slot.stretchGain;
        }

        if (slot.stretch2.render (a, b))
        {
            l += a * slot.stretch2Gain;
            r += b * slot.stretch2Gain;
        }

        if (slot.cloudGain > 0.0f)
        {
            slot.cloud.render (a, b);
            l += a * slot.cloudGain;
            r += b * slot.cloudGain;
        }

        // The broken radio: fewer bits and a held sample rate, before its
        // speaker (the band-pass after) rounds the steps off.
        if (slot.radio)
        {
            slot.holdCount += 1.0f;

            if (slot.holdCount >= slot.holdLength)
            {
                slot.holdCount -= slot.holdLength;
                slot.holdL = std::round (l / slot.crushStep) * slot.crushStep;
                slot.holdR = std::round (r / slot.crushStep) * slot.crushStep;
            }

            l = slot.holdL;
            r = slot.holdR;
        }

        // The lowpass can close over the answer (the hum darkens).
        if (slot.useLp && --slot.filterCountdown <= 0)
        {
            slot.filterCountdown = 32;
            const float p = juce::jmin (1.0f, (float) slot.age / (float) slot.length);
            slot.lp.set (slot.lpFrom * std::pow (slot.lpTo / slot.lpFrom, p), 0.7071f, sr);
        }

        if (slot.useHp)
        {
            l = slot.hp.process (0, l, 1);
            r = slot.hp.process (1, r, 1);
        }

        if (slot.useBand)
        {
            l = slot.band.process (0, l, 2);
            r = slot.band.process (1, r, 2);
        }

        if (slot.useLp)
        {
            l = slot.lp.process (0, l, 0);
            r = slot.lp.process (1, r, 0);
        }

        l = slot.sat.process (0, l * slot.gain);
        r = slot.sat.process (1, r * slot.gain);

        // The envelope.
        const float age = (float) slot.age, length = (float) slot.length;
        float e = juce::jmin (1.0f, age / juce::jmax (1.0f, slot.attack));

        if (slot.swell)
        {
            const float p = juce::jmin (1.0f, age / length);
            e = p * p;
        }

        const float left = length - age;

        if (left < slot.releaseSamples)
        {
            const float x = juce::jmax (0.0f, left / juce::jmax (1.0f, slot.releaseSamples));
            e *= slot.swell ? juce::jmin (1.0f, left / (float) (0.015 * sr)) : x * x * (3.0f - 2.0f * x);
        }

        if (slot.retreating)
        {
            slot.fade = juce::jmax (0.0f, slot.fade - slot.fadeStep);
            e *= slot.fade * slot.fade;
        }

        outL = l * e * slot.panL;
        outR = r * e * slot.panR;

        ++slot.age;

        if (slot.age >= slot.length || (slot.retreating && slot.fade <= 0.0f))
        {
            slot.on = false;
            slot.cloud.stop();
        }
    }

    Answerer::Slot& Answerer::takeSlot()
    {
        // The one playing steps aside; a free slot, or the quietest one.
        auto& old = slots[(size_t) active];

        if (old.on)
        {
            old.retreating = true;
            old.fadeStep = juce::jmax (old.fadeStep, 1.0f / (float) (sr * 0.12));
        }

        int next = -1;

        for (int k = 0; k < (int) slots.size(); ++k)
            if (k != active && ! slots[(size_t) k].on) { next = k; break; }

        if (next < 0)
            for (int k = 0; k < (int) slots.size(); ++k)
                if (k != active && (next < 0 || slots[(size_t) k].fade < slots[(size_t) next].fade))
                    next = k;

        active = next;
        return slots[(size_t) active];
    }

    bool Answerer::captureOlder (Capture& c, Rng& rng, int64_t length)
    {
        // Somewhere in the last twenty seconds (not the phrase just played)
        // where the track was really playing.
        const auto blocks = (int64_t) blockLevel.size();
        const int64_t newest = (pendingFrom - (int64_t) sr) / levelBlock;
        const int64_t oldest = juce::jmax ((int64_t) 0, now / levelBlock - blocks + 2);

        if (newest - oldest < 8)
            return false;

        float loudest = 0.0f;

        for (int64_t b = oldest; b < newest; ++b)
            loudest = juce::jmax (loudest, blockLevel[(size_t) (b % blocks)]);

        if (loudest < 1.0e-3f)
            return false;

        for (int tries = 0; tries < 24; ++tries)
        {
            const int64_t b = oldest + (int64_t) rng.below ((int) (newest - oldest));

            if (blockLevel[(size_t) (b % blocks)] > 0.4f * loudest)
            {
                const int64_t from = b * levelBlock - (int64_t) (0.1 * sr);
                const int64_t to = juce::jmin (from + length, pendingFrom);

                if (to - from < (int64_t) (0.5 * sr) || from < now - (int64_t) ring[0].size() + levelBlock)
                    continue;

                capture (c, from, to);
                return c.length > (int) (0.3 * sr);
            }
        }

        return false;
    }

    void Answerer::answerBreak (int lengthSamples, const AnswerSettings& s)
    {
        // Only breaks long enough to say something in.
        if (! s.answerBreaks || s.level <= 0.0f || lengthSamples < (int) (0.1 * sr))
            return;

        bool any = false;

        for (int t = 0; t < numTricks; ++t)
            any = any || (s.enabled & (1u << t)) != 0;

        if (! any)
            return;

        Rng rng (hash64 ((uint64_t) now, (uint64_t) s.seed + 0xb4eaull));

        if (! rng.chance (0.35f + 0.65f * s.hunger))
            return;

        const Trick trick = pickTrick (s, rng);
        auto& slot = takeSlot();

        // What the track played in the last two beats, into the hole.
        const auto span = (int64_t) juce::jmin (2.0 * s.samplesPerBeat, (double) slot.capture.capacity - 0.05 * sr);
        capture (slot.capture, now - span, now);
        begin (slot, trick, s, rng.next());

        slot.inBreak = true;
        slot.length = juce::jmin (slot.length, (int64_t) lengthSamples + (int64_t) (0.015 * sr));
        slot.releaseSamples = juce::jmin (slot.releaseSamples, juce::jmax ((float) (0.01 * sr), 0.2f * (float) slot.length));
        slot.attack = juce::jmin (slot.attack, (float) (0.005 * sr));
    }

    //==========================================================================
    void Answerer::process (const float* inL, const float* inR, float* outL, float* outR, int n, const AnswerSettings& s)
    {
        const float sp = s.space;
        const float delayReturn = 0.35f + 0.5f * sp;
        const float caveReturn = 0.3f + 0.7f * sp;

        for (int offset = 0; offset < n; offset += chunk)
        {
            const int count = juce::jmin (chunk, n - offset);

            for (int i = 0; i < count; ++i, ++now)
            {
                const float l = inL[offset + i], r = inR[offset + i];
                ring[0][(size_t) (now & ringMask)] = l;
                ring[1][(size_t) (now & ringMask)] = r;

                const double ppqNow = s.ppq + (double) (offset + i) * s.ppqPerSample;
                listen (0.5f * (l + r), ppqNow, s);

                // An answer due now. Clouds and hums now and then take their
                // grains from further back (Memory).
                if (pendingAt >= 0 && now >= pendingAt)
                {
                    pendingAt = -1;
                    auto& slot = takeSlot();
                    Rng memoryRng (hash64 (pendingSeed, 0x3e3ull));
                    const bool older = (pendingTrick == Trick::cloud || pendingTrick == Trick::hum)
                                    && memoryRng.chance (s.memory * 0.8f);

                    if (! older || ! captureOlder (slot.capture, memoryRng, (int64_t) (3.0 * sr)))
                        capture (slot.capture, pendingFrom, pendingTo);

                    begin (slot, pendingTrick, s, pendingSeed);
                }

                // A break the masher just bit: fill it.
                for (int k = 0; k < s.numBreaks; ++k)
                    if (s.breaks[(size_t) k].offset == offset + i)
                        answerBreak (s.breaks[(size_t) k].length, s);

                // How loud the track is in blocks, for Memory.
                blockSum += std::abs (l) + std::abs (r);

                if (++blockCount == levelBlock)
                {
                    blockLevel[(size_t) ((now / levelBlock) % (int64_t) blockLevel.size())] = blockSum / (2.0f * levelBlock);
                    blockSum = 0.0f;
                    blockCount = 0;
                }

                // While the player plays, the Mimic steps back: its own voice
                // a lot, its echoes less.
                const float playing = inPhrase ? juce::jlimit (0.0f, 1.0f, env / juce::jmax (1.0e-4f, lastPeak * 0.4f)) : 0.0f;
                const float duckTarget = 1.0f - 0.85f * playing;
                const float tailTarget = 1.0f - 0.55f * playing;
                duck += (duckTarget < duck ? duckAtt : duckRel) * (duckTarget - duck);
                tailDuck += (tailTarget < tailDuck ? duckAtt : duckRel) * (tailTarget - tailDuck);

                // A dub echo lets go of its feedback after a while.
                if (delayHold > 0 && --delayHold == 0)
                {
                    TapeDelay::Settings calm;
                    calm.delaySamples = (float) juce::jmin (2.4 * sr, 0.75 * s.samplesPerBeat);
                    calm.feedback = 0.25f + 0.25f * sp;
                    delay.set (calm);
                }

                float dl = 0.0f, dr = 0.0f, toDelayL = 0.0f, toDelayR = 0.0f, toCaveL = 0.0f, toCaveR = 0.0f;

                for (auto& slot : slots)
                {
                    if (! slot.on)
                        continue;

                    float vl, vr;
                    renderSlot (slot, vl, vr, s);
                    // An answer in a break never ducks: the track is silent there.
                    const float d = slot.inBreak ? 1.0f : duck;
                    dl += vl * slot.dry * d;
                    dr += vr * slot.dry * d;
                    toDelayL += vl * slot.toDelay;
                    toDelayR += vr * slot.toDelay;
                    toCaveL += vl * slot.toCave;
                    toCaveR += vr * slot.toCave;

                    // The dub's tape slows down under its echoes.
                    if (slot.trick == Trick::dub && std::abs (slot.spiralTo - slot.spiralFrom) > 0.5f && ! slot.retreating)
                    {
                        TapeDelay::Settings now2;
                        now2.delaySamples = slot.spiralTo;
                        now2.feedback = 0.55f + 0.37f * s.mockery;
                        now2.pingPong = 0.75f;
                        now2.wow = 0.25f + 0.6f * s.mockery;
                        now2.grit = 0.3f + 0.6f * s.mockery;
                        now2.toneHz = 4200.0f - 2200.0f * s.mockery;
                        delay.set (now2);
                        slot.spiralFrom = slot.spiralTo;
                    }
                }

                bool sounding = false;

                for (const auto& slot : slots)
                    sounding = sounding || slot.on;

                quietFor = sounding ? 0 : juce::jmin (quietFor + 1, 1 << 30);

                if (sounding)
                    asleep = false;

                float el = 0.0f, er = 0.0f;

                if (! asleep)
                    delay.process (toDelayL, toDelayR, el, er);

                levelSmooth += 0.0005f * (s.level - levelSmooth);
                const float level = levelSmooth * 1.4f;

                caveInL[(size_t) i] = toCaveL + 0.3f * sp * el;
                caveInR[(size_t) i] = toCaveR + 0.3f * sp * er;
                dryL[(size_t) i] = level * (dl + el * delayReturn * tailDuck);
                dryR[(size_t) i] = level * (dr + er * delayReturn * tailDuck);
                tailGain[(size_t) i] = level * caveReturn * tailDuck;
            }

            const bool caveAlive = ! asleep;

            if (caveAlive)
                cave.process (caveInL.data(), caveInR.data(), caveOutL.data(), caveOutR.data(), count);

            // Long quiet: the echoes and the cave have died away; stop them.
            if (! asleep && quietFor > (int) (sr * 14.0))
            {
                asleep = true;
                delay.reset();
                cave.reset();
            }

            for (int i = 0; i < count; ++i)
            {
                const float cl = caveAlive ? caveOutL[(size_t) i] : 0.0f;
                const float cr = caveAlive ? caveOutR[(size_t) i] : 0.0f;
                float yl = dryL[(size_t) i] + cl * tailGain[(size_t) i];
                float yr = dryR[(size_t) i] + cr * tailGain[(size_t) i];

                // The answers' own filter (like a filter on an aux return):
                // left of centre a low-pass closes, right a high-pass opens.
                filterSmooth += 0.002f * (s.filter - filterSmooth);

                if (--filterCountdown <= 0)
                {
                    filterCountdown = 32;
                    const float f = filterSmooth;
                    masterLow.set (f < 0.0f ? 20000.0f * std::exp2 (f * 6.0f) : 20000.0f, 0.75f, sr);
                    masterHigh.set (f > 0.0f ? 20.0f * std::exp2 (f * 7.3f) : 20.0f, 0.75f, sr);
                }

                if (std::abs (filterSmooth) > 0.005f || std::abs (s.filter) > 0.005f)
                {
                    yl = masterHigh.process (0, masterLow.process (0, yl, 0), 1);
                    yr = masterHigh.process (1, masterLow.process (1, yr, 0), 1);
                }

                outL[offset + i] += yl;
                outR[offset + i] += yr;
            }
        }

        // What the editor shows.
        const auto& slot = slots[(size_t) active];

        if (slot.on && ! slot.retreating)
        {
            shownState = answering;
            shownTrick = (int) slot.trick;
            shownProgress = juce::jlimit (0.0f, 1.0f, (float) slot.age / (float) slot.length);
        }
        else if (slot.on)
        {
            shownState = retreating;
        }
        else if (pendingAt >= 0)
        {
            shownState = waiting;
            shownWait = 1.0f;
        }
        else if (inPhrase)
        {
            shownState = shownWait > 0.25f ? waiting : hearing;
        }
        else
        {
            shownState = idle;
        }
    }
}
