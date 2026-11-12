#include "Reverb.h"
#include <complex>

namespace gargoyle
{
    namespace
    {
        constexpr float speedOfSound = 343.0f;

        const Space spaces[] {
            //  L     W     H    refl  open   lines ms     air     lows  highs
            { 42.0f, 20.0f, 26.0f, 0.84f, false, 41.0f, 127.0f,  7000.0f, 1.25f, 0.45f },   // Moonlit Nave
            { 11.0f,  7.0f,  3.6f, 0.72f, false, 11.0f,  37.0f,  4500.0f, 1.10f, 0.35f },   // The Crypt
            {  7.0f,  7.0f, 34.0f, 0.90f, false, 19.0f,  83.0f, 11000.0f, 0.90f, 0.70f },   // Bell Tower
            { 30.0f, 30.0f,  7.0f, 0.62f, true,  23.0f,  89.0f,  9000.0f, 0.80f, 0.50f },   // Cloister
            // The Vestry: a real room, the size of a living room. Plaster and
            // wood, a cupboard of robes, a rug: three orders of reflections,
            // irregular and duller as they bounce, and a short, even tail.
            {  6.2f,  4.4f,  2.9f, 0.80f, false,  6.5f,  27.0f,  9500.0f, 1.10f, 0.60f, 3, 0.4f }
        };

        constexpr float diffuserMsL[] { 4.71f, 3.59f, 12.73f, 9.31f };
        constexpr float diffuserMsR[] { 5.13f, 3.17f, 11.87f, 8.69f };

        // Bilinear one-pole coefficient for a corner frequency.
        float poleFor (float hz, double sr)
        {
            const float t = std::tan (juce::MathConstants<float>::pi * juce::jmin (hz, (float) sr * 0.45f) / (float) sr);
            return (1.0f - t) / (1.0f + t);
        }

        // Fast Walsh-Hadamard transform, scaled to be orthonormal.
        template <size_t N>
        void hadamard (std::array<float, N>& v)
        {
            for (size_t h = 1; h < N; h *= 2)
                for (size_t i = 0; i < N; i += h * 2)
                    for (size_t j = i; j < i + h; ++j)
                    {
                        const float a = v[j], b = v[j + h];
                        v[j] = a + b;
                        v[j + h] = a - b;
                    }

            const float scale = 1.0f / std::sqrt ((float) N);

            for (auto& x : v)
                x *= scale;
        }

        struct Point3
        {
            float x, y, z;
            Point3 operator- (Point3 o) const { return { x - o.x, y - o.y, z - o.z }; }
            float length() const { return std::sqrt (x * x + y * y + z * z); }
        };

        // Diffuse (0..1) sets both: a light smear on the way in (heavy input
        // allpasses ring on their own), most of it inside the loop, where
        // it thickens the tail without a pattern.
        float inputDiffusionFor (float diffuse) { return 0.05f + 0.25f * diffuse; }
        float loopDiffusionFor (float diffuse)  { return 0.45f + 0.25f * diffuse; }

        // The nearest prime at or above n: delay lengths with no common
        // factors never line their echoes up into a pitched ring.
        int primeAtLeast (int n)
        {
            auto isPrime = [] (int x)
            {
                if (x < 2) return false;
                if (x % 2 == 0) return x == 2;
                for (int d = 3; d * d <= x; d += 2)
                    if (x % d == 0) return false;
                return true;
            };

            while (! isPrime (n))
                ++n;

            return n;
        }

        // The tail's lines, in ms: spread between the space's shortest and
        // longest, scaled with the size, but never short enough to comb
        // (under ~7 ms a delay network sings a pitch of its own).
        float lineMs (const Space& sp, float factor, int i, int numLines)
        {
            const float ms = sp.lineMinMs * std::pow (sp.lineMaxMs / sp.lineMinMs, (float) i / (float) (numLines - 1))
                           * (1.0f + 0.035f * std::sin (2.7f * (float) i)) * factor;
            return juce::jmax (ms, 7.0f * std::pow (3.4f, (float) i / (float) (numLines - 1)));
        }

        // The loop allpasses, in ms at size 1: all different, 1.1 to 6.9 ms,
        // scattered by the golden ratio.
        float loopDiffuserMs (int i)
        {
            return 1.1f + 5.8f * std::fmod (0.13f + 0.618034f * (float) i, 1.0f);
        }
    }

    const Space& space (int index) { return spaces[juce::jlimit (0, numSpaces() - 1, index)]; }
    int numSpaces() { return (int) std::size (spaces); }

    //==========================================================================
    void Reverb::DelayBuffer::setSize (int minimumLength)
    {
        int size = 1;

        while (size < minimumLength)
            size <<= 1;

        data.assign ((size_t) size, 0.0f);
        mask = size - 1;
        write = 0;
    }

    float Reverb::DelayBuffer::readLinear (float delay) const
    {
        float p = (float) (write - 1) - delay;

        if (p < 0.0f)
            p += (float) (mask + 1);

        const int i0 = (int) p;
        const float frac = p - (float) i0;
        const float a = data[(size_t) (i0 & mask)];
        const float b = data[(size_t) ((i0 + 1) & mask)];
        return a + frac * (b - a);
    }

    float Reverb::DelayBuffer::readCubic (float delay) const
    {
        float p = (float) (write - 1) - delay;

        if (p < 0.0f)
            p += (float) (mask + 1);

        const int i0 = (int) p;
        const float t = p - (float) i0;
        const float xm = data[(size_t) ((i0 - 1) & mask)];
        const float x0 = data[(size_t) (i0 & mask)];
        const float x1 = data[(size_t) ((i0 + 1) & mask)];
        const float x2 = data[(size_t) ((i0 + 2) & mask)];

        // Catmull-Rom (cubic Hermite): keeps the highs a linear read dulls.
        const float c1 = 0.5f * (x1 - xm);
        const float c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    float Reverb::Allpass::process (float x, float g)
    {
        const float delayed = buffer.readLinear (length);
        const float w = x + g * delayed;
        buffer.push (w);
        return delayed - g * w;
    }

    float Reverb::Shelf::process (float x, float lowGain, float highGain, float aLow, float aHigh)
    {
        const float lp = 0.5f * (1.0f - aLow) * (x + xl) + aLow * zl;
        xl = x;
        zl = lp;
        const float y = x + (lowGain - 1.0f) * lp;

        const float hp = 0.5f * (1.0f + aHigh) * (y - xh) + aHigh * zh;
        xh = y;
        zh = hp;
        return y + (highGain - 1.0f) * hp;
    }

    //==========================================================================
    void Reverb::prepare (double sampleRate)
    {
        sr = sampleRate;
        const float msToSamples = (float) sr / 1000.0f;

        inputL.setSize ((int) (sr * 1.0) + 8);
        inputR.setSize ((int) (sr * 1.0) + 8);
        mid.setSize ((int) (sr * 1.0) + 8);

        for (int i = 0; i < numDiffusers; ++i)
        {
            diffL[(size_t) i].buffer.setSize ((int) (20.0f * msToSamples) + 8);
            diffR[(size_t) i].buffer.setSize ((int) (20.0f * msToSamples) + 8);
        }

        for (auto& line : lines)
            line.setSize ((int) (240.0f * msToSamples) + 64);

        for (auto& d : loopDiffusers)
            d.buffer.setSize ((int) (20.0f * msToSamples) + 8);

        feedL.setSize ((int) (30.0f * msToSamples) + 8);
        feedR.setSize ((int) (30.0f * msToSamples) + 8);

        {
            // Fixed pseudo-random signs (the same every time).
            uint32_t h = 0x9e3779b9u;
            auto nextSign = [&h] { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (h & 0x10000u) != 0 ? 1.0f : -1.0f; };

            for (int i = 0; i < numLines; ++i)
            {
                feedSign[(size_t) i] = nextSign();
                outSignL[(size_t) i] = nextSign();
                outSignR[(size_t) i] = nextSign();
            }
        }

        for (int i = 0; i < numLines; ++i)
            feedTap[(size_t) i] = i == 0 ? 0 : primeAtLeast ((int) (23.0f * std::fmod (0.37f + 0.754877f * (float) i, 1.0f) * msToSamples));

        for (int i = 0; i < numLines; ++i)
        {
            const float hz = 0.1f * std::pow (1.08f, (float) i);   // 0.1 to 1.1 Hz
            const float w = juce::MathConstants<float>::twoPi * hz / (float) sr;
            const float phase = juce::MathConstants<float>::twoPi * std::fmod ((float) i * 0.618034f, 1.0f);
            modRotC[(size_t) i] = std::cos (w);
            modRotS[(size_t) i] = std::sin (w);
            modCos[(size_t) i] = std::cos (phase);
            modSin[(size_t) i] = std::sin (phase);
        }

        lowcutA = std::exp (-juce::MathConstants<float>::twoPi * 25.0f / (float) sr);
        shelfLowA = poleFor (250.0f, sr);
        shelfHighA = poleFor (4000.0f, sr);
        glide = 1.0f - std::exp (-(float) controlInterval / (0.2f * (float) sr));

        reset();
    }

    void Reverb::reset()
    {
        inputL.clear(); inputR.clear(); mid.clear();

        for (auto& d : diffL) d.buffer.clear();
        for (auto& d : diffR) d.buffer.clear();
        for (auto& line : lines) line.clear();
        for (auto& d : loopDiffusers) d.buffer.clear();
        feedL.clear(); feedR.clear();
        for (auto& s : shelves) s = {};

        airL.z = airR.z = brightL.z = brightR.z = lateAirL.z = lateAirR.z = 0.0f;
        lowcutL = lowcutR = 0.0f;

        // Jump straight to the targets: nothing is playing yet.
        tapsDirty = true;
        computeTaps();
        tapDelayL = tapTargetL; tapDelayR = tapTargetR;
        tapGainL = tapGainTargetL; tapGainR = tapGainTargetR;

        const auto& sp = currentSpace();
        const float factor = sizeFactor (target.size);
        const float msToSamples = (float) sr / 1000.0f;

        for (int i = 0; i < numLines; ++i)
        {
            lineTarget[(size_t) i] = lineLength[(size_t) i] = (float) primeAtLeast ((int) (lineMs (sp, factor, i, numLines) * msToSamples));
            auto& d = loopDiffusers[(size_t) i];
            d.length = d.target = (float) primeAtLeast ((int) (loopDiffuserMs (i) * juce::jlimit (0.6f, 1.6f, std::sqrt (factor)) * msToSamples));
        }

        for (int i = 0; i < numDiffusers; ++i)
        {
            const float scale = std::sqrt (factor) * msToSamples;
            diffL[(size_t) i].length = diffL[(size_t) i].target = diffuserMsL[i] * scale;
            diffR[(size_t) i].length = diffR[(size_t) i].target = diffuserMsR[i] * scale;
        }

        predelay = target.predelayMs * msToSamples;
        earlyGain = target.early;
        injectGain = target.freeze ? 0.0f : 1.0f;
        width = target.width;
        diffusion = inputDiffusionFor (target.diffuse);
        loopDiffusion = loopDiffusionFor (target.diffuse);
        controlCounter = 0;
        updateControl();
    }

    void Reverb::setSettings (const Settings& s)
    {
        if (s.space != target.space || std::abs (s.size - target.size) > 1.0e-4f
            || std::abs (s.predelayMs - target.predelayMs) > 1.0e-3f || s.captured != target.captured)
            tapsDirty = true;

        target = s;
    }

    void Reverb::computeTaps()
    {
        if (! tapsDirty)
            return;

        tapsDirty = false;

        const auto& sp = currentSpace();
        const float factor = sizeFactor (target.size);
        const float L = sp.length * factor, W = sp.width * factor, H = sp.height * factor;

        const float z = juce::jmin (1.7f, 0.4f * H);
        const Point3 source { 0.42f * L, 0.33f * W, z };
        const Point3 ears[2] { { 0.58f * L - 0.6f, 0.62f * W, z }, { 0.58f * L + 0.6f, 0.62f * W, z } };
        const float direct = 0.5f * ((source - ears[0]).length() + (source - ears[1]).length());

        auto image = [] (int n, float s, float size) { return (float) n * size + ((n & 1) != 0 ? size - s : s); };

        const float samplesPerMetre = (float) sr / speedOfSound;
        const float pre = target.predelayMs * (float) sr / 1000.0f;

        for (int ear = 0; ear < 2; ++ear)
        {
            auto& delays = ear == 0 ? tapTargetL : tapTargetR;
            auto& gains = ear == 0 ? tapGainTargetL : tapGainTargetR;
            int k = 0;
            float energy = 0.0f;

            const int maxOrder = juce::jlimit (1, 3, sp.maxOrder);

            for (int nx = -maxOrder; nx <= maxOrder; ++nx)
                for (int ny = -maxOrder; ny <= maxOrder; ++ny)
                    for (int nz = -maxOrder; nz <= maxOrder; ++nz)
                    {
                        const int order = std::abs (nx) + std::abs (ny) + std::abs (nz);

                        if (order == 0 || order > maxOrder || k >= numTaps)
                            continue;

                        const Point3 img { image (nx, source.x, L), image (ny, source.y, W), image (nz, source.z, H) };
                        float d = (img - ears[ear]).length();
                        const bool lost = sp.openRoof && ! (nz == 0 || nz == -1);
                        float g = std::pow (sp.reflect, (float) order);

                        // Things in the room break up the regular pattern a
                        // bare box would ring with (no flutter).
                        // Every room has something in it: a little scatter
                        // everywhere keeps a bare box from fluttering.
                        const float scatter = juce::jmax (0.3f, sp.scatter);

                        {
                            const auto h = (uint32_t) ((nx + 7) * 961 + (ny + 7) * 31 + (nz + 7)) * 2654435761u;
                            const float u1 = (float) ((h >> 8) & 0xffff) / 65535.0f;
                            const float u2 = (float) ((h >> 20) & 0xfff) / 4095.0f;
                            d *= 1.0f + scatter * 0.07f * (u1 - 0.5f);
                            g *= 1.0f - scatter * 0.45f * u2;
                        }

                        delays[(size_t) k] = pre + juce::jmax (0.0f, d - direct) * samplesPerMetre + 1.0f;
                        gains[(size_t) k] = lost || target.captured ? 0.0f : g * direct / juce::jmax (d, 0.1f);
                        tapLate[(size_t) k] = order >= 2;
                        energy += gains[(size_t) k] * gains[(size_t) k];
                        ++k;
                    }

            for (; k < numTaps; ++k)
            {
                delays[(size_t) k] = pre + 1.0f;
                gains[(size_t) k] = 0.0f;
            }

            const float norm = energy > 0.0f ? 0.7f / std::sqrt (energy) : 0.0f;

            for (auto& g : gains)
                g *= norm;
        }

        const float airHz = sp.air / std::sqrt (factor);
        airL.setLowpass (airHz, sr);
        airR.setLowpass (airHz, sr);

        // Later bounces have met more surfaces: duller, more so in a
        // furnished room.
        const float lateHz = airHz * (0.75f - 0.35f * sp.scatter);
        lateAirL.setLowpass (lateHz, sr);
        lateAirR.setLowpass (lateHz, sr);
    }

    void Reverb::updateControl()
    {
        computeTaps();

        const auto& sp = currentSpace();
        const float factor = sizeFactor (target.size);
        const float msToSamples = (float) sr / 1000.0f;

        for (int i = 0; i < numLines; ++i)
        {
            lineTarget[(size_t) i] = (float) primeAtLeast ((int) (lineMs (sp, factor, i, numLines) * msToSamples));
            loopDiffusers[(size_t) i].target = (float) primeAtLeast ((int) (loopDiffuserMs (i) * juce::jlimit (0.6f, 1.6f, std::sqrt (factor)) * msToSamples));

            const float depth = target.drift * 0.00035f * (float) sr * (0.5f + 0.5f * std::fmod ((float) i * 0.618034f, 1.0f));
            modDepth[(size_t) i] = depth;

            // Renormalise the rotating oscillator so it never drifts in level.
            const float m = 1.0f / std::sqrt (modCos[(size_t) i] * modCos[(size_t) i] + modSin[(size_t) i] * modSin[(size_t) i]);
            modCos[(size_t) i] *= m;
            modSin[(size_t) i] *= m;
        }

        for (int i = 0; i < numDiffusers; ++i)
        {
            const float scale = std::sqrt (factor) * msToSamples;
            diffL[(size_t) i].target = diffuserMsL[i] * scale;
            diffR[(size_t) i].target = diffuserMsR[i] * scale;
        }

        const float rt = juce::jlimit (0.1f, 100.0f, target.decay);
        const float rtLow = juce::jlimit (0.1f, 100.0f, rt * target.lows * sp.lows);
        const float rtHigh = juce::jlimit (0.05f, 100.0f, rt * target.highs * sp.highs);

        for (int i = 0; i < numLines; ++i)
        {
            const float seconds = lineSeconds (i);

            if (target.freeze)
            {
                gainMid[(size_t) i] = 1.0f;
                ratioLow[(size_t) i] = ratioHigh[(size_t) i] = 1.0f;
                continue;
            }

            // Loop gains wanted at DC, 1 kHz and Nyquist. The shelves are
            // gentle, so they also touch 1 kHz: scale until it lands.
            const float gl = std::pow (10.0f, -3.0f * seconds / rtLow);
            const float gm = std::pow (10.0f, -3.0f * seconds / rt);
            const float gh = std::pow (10.0f, -3.0f * seconds / rtHigh);
            float c = gm;

            for (int pass = 0; pass < 3; ++pass)
                c = gm / shelfMagnitudeAtMid (gl / c, gh / c);

            gainMid[(size_t) i] = c;
            ratioLow[(size_t) i] = gl / c;
            ratioHigh[(size_t) i] = gh / c;
        }

        brightL.setLowpass (target.brightness, sr);
        brightR.setLowpass (target.brightness, sr);
    }

    float Reverb::lineSeconds (int i) const
    {
        // An allpass delays everything by its length on average: it is part
        // of the loop's time, so of how fast the loop decays.
        return (lineLength[(size_t) i] + loopDiffusers[(size_t) i].length) / (float) sr;
    }

    float Reverb::shelfMagnitudeAtMid (float lowGain, float highGain) const
    {
        using C = std::complex<float>;
        const C zi = std::polar (1.0f, -juce::MathConstants<float>::twoPi * 1000.0f / (float) sr);   // z^-1
        const C lp = 0.5f * (1.0f - shelfLowA) * (1.0f + zi) / (1.0f - shelfLowA * zi);
        const C hp = 0.5f * (1.0f + shelfHighA) * (1.0f - zi) / (1.0f - shelfHighA * zi);
        return std::abs ((1.0f + (lowGain - 1.0f) * lp) * (1.0f + (highGain - 1.0f) * hp));
    }

    void Reverb::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                          const float* tailL, const float* tailR, const float* earlyL, const float* earlyR)
    {
        const float msToSamples = (float) sr / 1000.0f;
        const float k = glide;

        // Per-sample steps toward the glided targets, refreshed every control step.
        std::array<float, numLines> lineStep {};
        std::array<float, numTaps> stepL {}, stepR {}, gainStepL {}, gainStepR {};
        std::array<float, numDiffusers> diffStepL {}, diffStepR {};
        float preStep = 0.0f, earlyStep = 0.0f, injectStep = 0.0f, widthStep = 0.0f, diffusionStep = 0.0f;

        auto plan = [&]
        {
            updateControl();
            const float n = 1.0f / (float) controlInterval;

            for (int i = 0; i < numLines; ++i)
                lineStep[(size_t) i] = (lineTarget[(size_t) i] - lineLength[(size_t) i]) * k * n;

            for (int i = 0; i < numTaps; ++i)
            {
                stepL[(size_t) i] = (tapTargetL[(size_t) i] - tapDelayL[(size_t) i]) * k * n;
                stepR[(size_t) i] = (tapTargetR[(size_t) i] - tapDelayR[(size_t) i]) * k * n;
                gainStepL[(size_t) i] = (tapGainTargetL[(size_t) i] - tapGainL[(size_t) i]) * k * n;
                gainStepR[(size_t) i] = (tapGainTargetR[(size_t) i] - tapGainR[(size_t) i]) * k * n;
            }

            for (int i = 0; i < numDiffusers; ++i)
            {
                diffStepL[(size_t) i] = (diffL[(size_t) i].target - diffL[(size_t) i].length) * k * n;
                diffStepR[(size_t) i] = (diffR[(size_t) i].target - diffR[(size_t) i].length) * k * n;
            }

            preStep = (target.predelayMs * msToSamples - predelay) * k * n;
            earlyStep = (target.early - earlyGain) * k * n;
            injectStep = ((target.freeze ? 0.0f : 1.0f) - injectGain) * juce::jmin (1.0f, k * 4.0f) * n;
            widthStep = (target.width - width) * k * n;
            loopDiffusion = loopDiffusionFor (target.diffuse);
            diffusionStep = (inputDiffusionFor (target.diffuse) - diffusion) * k * n;
        };

        if (controlCounter == 0)
            plan();

        std::array<float, numLines> y {}, f {};
        // Level kept as it was with 16 lines: in and out scale with the count.
        const float inject = 0.5f * std::sqrt (16.0f / (float) numLines);
        const float outScale = 1.0f / std::sqrt ((float) numLines);

        for (int s = 0; s < numSamples; ++s)
        {
            if (controlCounter == controlInterval)
            {
                controlCounter = 0;
                plan();
            }

            ++controlCounter;

            const float xl = inL[s], xr = inR[s];
            inputL.push (xl);
            inputR.push (xr);
            mid.push (0.5f * (xl + xr));

            // Early reflections from the room's image sources.
            float el = 0.0f, er = 0.0f, ll = 0.0f, lr = 0.0f;

            for (int t = 0; t < numTaps; ++t)
            {
                const float a = mid.readLinear (tapDelayL[(size_t) t]) * tapGainL[(size_t) t];
                const float b = mid.readLinear (tapDelayR[(size_t) t]) * tapGainR[(size_t) t];

                if (tapLate[(size_t) t])
                {
                    ll += a;
                    lr += b;
                }
                else
                {
                    el += a;
                    er += b;
                }

                tapDelayL[(size_t) t] += stepL[(size_t) t];
                tapDelayR[(size_t) t] += stepR[(size_t) t];
                tapGainL[(size_t) t] += gainStepL[(size_t) t];
                tapGainR[(size_t) t] += gainStepR[(size_t) t];
            }

            el = airL.lowpass (el + lateAirL.lowpass (ll));
            er = airR.lowpass (er + lateAirR.lowpass (lr));

            // A captured room's own reflections, as recorded.
            if (earlyL != nullptr)
            {
                el += earlyL[s];
                er += earlyR[s];
            }

            // Tail input: the predelayed signal, coloured, lowcut and lightly
            // diffused. Not the early reflections: a box's reflections come
            // at regular spacings, and fed into the tail that pattern rings
            // on in it (measured: it was the main source of the metallic
            // tone). They are heard on their own instead.
            float tl = brightL.lowpass (0.75f * inputL.readLinear (predelay));
            float tr = brightR.lowpass (0.75f * inputR.readLinear (predelay));
            el *= earlyGain;
            er *= earlyGain;
            lowcutL = tl + lowcutA * (lowcutL - tl);
            lowcutR = tr + lowcutA * (lowcutR - tr);
            tl -= lowcutL;
            tr -= lowcutR;

            if (tailL != nullptr)
            {
                tl += tailL[s];
                tr += tailR[s];
            }

            for (int i = 0; i < numDiffusers; ++i)
            {
                tl = diffL[(size_t) i].process (tl, diffusion);
                tr = diffR[(size_t) i].process (tr, diffusion);
                diffL[(size_t) i].length += diffStepL[(size_t) i];
                diffR[(size_t) i].length += diffStepR[(size_t) i];
            }

            // Feedback delay network.
            for (int i = 0; i < numLines; ++i)
            {
                auto& c = modCos[(size_t) i];
                auto& sn = modSin[(size_t) i];
                const float nc = c * modRotC[(size_t) i] - sn * modRotS[(size_t) i];
                sn = sn * modRotC[(size_t) i] + c * modRotS[(size_t) i];
                c = nc;

                auto& ap = loopDiffusers[(size_t) i];
                y[(size_t) i] = ap.process (lines[(size_t) i].readCubic (lineLength[(size_t) i] + modDepth[(size_t) i] * sn), loopDiffusion);
                ap.length += (ap.target - ap.length) * 0.001f;
                f[(size_t) i] = shelves[(size_t) i].process (y[(size_t) i], ratioLow[(size_t) i], ratioHigh[(size_t) i], shelfLowA, shelfHighA)
                                * gainMid[(size_t) i];
                lineLength[(size_t) i] += lineStep[(size_t) i];
            }

            hadamard (f);

            float ol = 0.0f, orr = 0.0f;

            feedL.push (tl);
            feedR.push (tr);

            for (int i = 0; i < numLines; ++i)
            {
                const auto& feed = (i & 1) != 0 ? feedR : feedL;
                const float in = feed.data[(size_t) ((feed.write - 1 - feedTap[(size_t) i]) & feed.mask)];
                lines[(size_t) i].push (f[(size_t) i] + in * injectGain * feedSign[(size_t) i] * inject);
                ol += y[(size_t) i] * outSignL[(size_t) i];
                orr += y[(size_t) i] * outSignR[(size_t) i];
            }

            ol = ol * outScale + el;
            orr = orr * outScale + er;

            const float m = 0.5f * (ol + orr);
            const float sd = 0.5f * (ol - orr) * width;
            outL[s] = m + sd;
            outR[s] = m - sd;

            predelay += preStep;
            earlyGain += earlyStep;
            injectGain += injectStep;
            width += widthStep;
            diffusion += diffusionStep;
        }
    }

    //==========================================================================
    void BandSplit::Svf::set (float hz, double sampleRate)
    {
        g = std::tan (juce::MathConstants<float>::pi * juce::jmin (hz, (float) sampleRate * 0.45f) / (float) sampleRate);
        k = 1.41421356f;   // Butterworth
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void BandSplit::Svf::tick (int ch, float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch];
        ic2[ch] = 2.0f * v2 - ic2[ch];
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }

    void BandSplit::prepare (double sampleRate, float lowHz, float highHz)
    {
        for (auto* f : { &lowA, &lowB, &restA, &restB, &allpass })
            f->set (lowHz, sampleRate);

        for (auto* f : { &highA, &highB, &midA, &midB })
            f->set (highHz, sampleRate);

        allpass.set (highHz, sampleRate);
        reset();
    }

    void BandSplit::reset()
    {
        for (auto* f : { &lowA, &lowB, &highA, &highB, &restA, &restB, &midA, &midB, &allpass })
            for (int c = 0; c < 2; ++c)
                f->ic1[c] = f->ic2[c] = 0.0f;
    }

    void BandSplit::process (int ch, float x, float& low, float& mid, float& high)
    {
        float lp, bp, hp;

        // The lows, then the upper crossover's allpass on them.
        lowA.tick (ch, x, lp, bp, hp);
        lowB.tick (ch, lp, lp, bp, hp);
        float l = lp;
        allpass.tick (ch, l, lp, bp, hp);
        low = lp + hp - allpass.k * bp;   // a 2nd-order allpass: lp - k bp + hp

        // Everything above the lower crossover, split again.
        restA.tick (ch, x, lp, bp, hp);
        restB.tick (ch, hp, lp, bp, hp);
        const float rest = hp;

        midA.tick (ch, rest, lp, bp, hp);
        midB.tick (ch, lp, lp, bp, hp);
        mid = lp;

        highA.tick (ch, rest, lp, bp, hp);
        highB.tick (ch, hp, lp, bp, hp);
        high = hp;
    }

    //==========================================================================
    void SoftOctave::prepare (double sampleRate)
    {
        int size = 1;

        while (size < (int) (sampleRate * 0.4))
            size <<= 1;

        for (auto& b : buffer)
            b.assign ((size_t) size, 0.0f);

        mask = size - 1;
        windowSamples = (float) (sampleRate * 0.09);   // long grains: smooth, no warble
        reset();
    }

    void SoftOctave::reset()
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);

        write = 0;
        phase = 0.0f;
    }

    void SoftOctave::process (float inL, float inR, float& outL, float& outR)
    {
        buffer[0][(size_t) write] = inL;
        buffer[1][(size_t) write] = inR;

        // Reading twice as fast: the tap delay shrinks by one sample per
        // sample, wrapping every window.
        phase += 1.0f / windowSamples;
        phase -= std::floor (phase);

        outL = outR = 0.0f;

        for (int t = 0; t < 4; ++t)
        {
            float p = phase + 0.25f * (float) t;
            p -= std::floor (p);
            const float delay = 2.0f + (1.0f - p) * windowSamples;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * p);
            const float pos = (float) write - delay;
            const int i = (int) std::floor (pos);
            const float f = pos - (float) i;

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto& b = buffer[ch];
                const float a = b[(size_t) (i & mask)], c = b[(size_t) ((i + 1) & mask)];
                (ch == 0 ? outL : outR) += w * (a + f * (c - a));
            }
        }

        // Four Hann windows a quarter apart add up to 2.
        outL *= 0.5f;
        outR *= 0.5f;
        write = (write + 1) & mask;
    }

    //==========================================================================
    void Guardian::prepare (double sampleRate)
    {
        sr = sampleRate;
        reset();
    }

    void Guardian::reset()
    {
        env = slowEnv = -120.0f;
        act = stored = 0.0f;
        gain = 1.0f;
        rtScale = 1.0f;
        brightHz = 9000.0f;
        playing = false;
        silentFor = 0.0;
        pause = 2.5f;
        heldLevel = 0.5f;
        bandEnv.fill (-120.0f);
        bandAct.fill (0.0f);
        bandGains.fill (1.0f);
    }

    void Guardian::process (const float* inL, const float* inR, int numSamples, const Settings& s)
    {
        const float dt = (float) numSamples / (float) sr;
        auto follow = [dt] (float& state, float x, float attackS, float releaseS)
        {
            const float tau = x > state ? attackS : releaseS;
            state = x + (state - x) * std::exp (-dt / tau);
        };

        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (inL[i]), std::abs (inR[i]));

        const float level = juce::Decibels::gainToDecibels (peak, -120.0f);
        follow (env, level, 0.003f, 0.08f);
        follow (slowEnv, level, 0.05f, 0.7f);

        // Activity: 0 in silence, 1 when clearly playing. Waking is slow,
        // turning back to stone is fast.
        const float wanted = juce::jlimit (0.0f, 1.0f, (env + 54.0f) / 18.0f);
        follow (act, wanted, 0.008f, juce::jmax (0.02f, s.wakeMs * 0.001f));

        // Watch: learn the typical pause between phrases.
        if (playing ? env < -50.0f : env > -44.0f)
        {
            playing = ! playing;

            if (playing && silentFor >= 0.12 && silentFor <= 30.0)
                pause = std::exp (0.65f * std::log (pause) + 0.35f * std::log ((float) silentFor));

            silentFor = 0.0;
        }

        if (! playing)
            silentFor += dt;
        else
            heldLevel = juce::jlimit (0.0f, 1.0f, (slowEnv + 48.0f) / 42.0f);

        const float awake = 1.0f - act;

        // Charge fills while you play and drains into the bloom after.
        const float fill = act * (1.0f - stored) / 6.0f;
        const float drain = awake * stored / juce::jmax (0.4f, 0.6f * s.decay);
        stored = juce::jlimit (0.0f, 1.0f, stored + (fill - drain) * dt);

        const float duckDb = -30.0f * s.stone * act;
        const float bloomDb = 9.0f * s.charge * stored * awake;
        gain = juce::Decibels::decibelsToGain (duckDb + bloomDb);

        // The same per band: each band of the wet steps aside for its own
        // band of the dry. (The bloom is the whole space's.)
        for (size_t b = 0; b < 3; ++b)
            follow (bandEnv[b], juce::Decibels::gainToDecibels (bandPeak[b], -120.0f), 0.003f, 0.08f);

        const float loudestBand = juce::jmax (bandEnv[0], bandEnv[1], bandEnv[2]);

        for (size_t b = 0; b < 3; ++b)
        {
            // A band counts as playing when it is loud in itself and not far
            // under the loudest band (a bass's spill into the mids is not
            // the mids playing).
            const float absolute = juce::jlimit (0.0f, 1.0f, (bandEnv[b] + 54.0f) / 18.0f);
            const float relative = juce::jlimit (0.0f, 1.0f, (bandEnv[b] - loudestBand + 30.0f) / 12.0f);
            // Linked: every band hears the whole dry signal.
            const float bandWanted = linked ? wanted : juce::jmin (absolute, relative);
            follow (bandAct[b], bandWanted, 0.008f, juce::jmax (0.02f, s.wakeMs * 0.001f));
            bandGains[b] = juce::Decibels::decibelsToGain (-30.0f * s.stone * depth[b] * bandAct[b] + bloomDb);
        }

        // Living: quiet playing leaves a longer, darker space; loud playing
        // a brighter, tighter one.
        rtScale = (1.0f + 0.8f * s.charge * stored * awake)
                * std::pow (2.0f, s.living * (0.5f - heldLevel) * 0.8f);

        const float open = 2500.0f * std::pow (16000.0f / 2500.0f, heldLevel);
        brightHz = std::exp (std::log (9000.0f) + s.living * (std::log (open) - std::log (9000.0f)));
    }
}
