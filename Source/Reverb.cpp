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
            { 30.0f, 30.0f,  7.0f, 0.62f, true,  23.0f,  89.0f,  9000.0f, 0.80f, 0.50f }    // Cloister
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

        float sign (bool negative) { return negative ? -1.0f : 1.0f; }
        int parity (int x) { int p = 0; while (x != 0) { p ^= (x & 1); x >>= 1; } return p; }
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

        for (int i = 0; i < numLines; ++i)
        {
            const float hz = 0.1f * std::pow (1.173f, (float) i);
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
        for (auto& s : shelves) s = {};

        airL.z = airR.z = brightL.z = brightR.z = 0.0f;
        lowcutL = lowcutR = 0.0f;

        // Jump straight to the targets: nothing is playing yet.
        tapsDirty = true;
        computeTaps();
        tapDelayL = tapTargetL; tapDelayR = tapTargetR;
        tapGainL = tapGainTargetL; tapGainR = tapGainTargetR;

        const auto& sp = space (target.space);
        const float factor = sizeFactor (target.size);
        const float msToSamples = (float) sr / 1000.0f;

        for (int i = 0; i < numLines; ++i)
        {
            const float ms = sp.lineMinMs * std::pow (sp.lineMaxMs / sp.lineMinMs, (float) i / (float) (numLines - 1))
                           * (1.0f + 0.035f * std::sin (2.7f * (float) i));
            lineTarget[(size_t) i] = lineLength[(size_t) i] = ms * factor * msToSamples;
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
        diffusion = target.diffuse * 0.72f;
        controlCounter = 0;
        updateControl();
    }

    void Reverb::setSettings (const Settings& s)
    {
        if (s.space != target.space || std::abs (s.size - target.size) > 1.0e-4f
            || std::abs (s.predelayMs - target.predelayMs) > 1.0e-3f)
            tapsDirty = true;

        target = s;
    }

    void Reverb::computeTaps()
    {
        if (! tapsDirty)
            return;

        tapsDirty = false;

        const auto& sp = space (target.space);
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

            for (int nx = -2; nx <= 2; ++nx)
                for (int ny = -2; ny <= 2; ++ny)
                    for (int nz = -2; nz <= 2; ++nz)
                    {
                        const int order = std::abs (nx) + std::abs (ny) + std::abs (nz);

                        if (order == 0 || order > 2 || k >= numTaps)
                            continue;

                        const Point3 img { image (nx, source.x, L), image (ny, source.y, W), image (nz, source.z, H) };
                        const float d = (img - ears[ear]).length();
                        const bool lost = sp.openRoof && ! (nz == 0 || nz == -1);

                        delays[(size_t) k] = pre + juce::jmax (0.0f, d - direct) * samplesPerMetre + 1.0f;
                        gains[(size_t) k] = lost ? 0.0f : std::pow (sp.reflect, (float) order) * direct / juce::jmax (d, 0.1f);
                        energy += gains[(size_t) k] * gains[(size_t) k];
                        ++k;
                    }

            const float norm = energy > 0.0f ? 0.7f / std::sqrt (energy) : 0.0f;

            for (auto& g : gains)
                g *= norm;
        }

        const float airHz = sp.air / std::sqrt (factor);
        airL.setLowpass (airHz, sr);
        airR.setLowpass (airHz, sr);
    }

    void Reverb::updateControl()
    {
        computeTaps();

        const auto& sp = space (target.space);
        const float factor = sizeFactor (target.size);
        const float msToSamples = (float) sr / 1000.0f;

        for (int i = 0; i < numLines; ++i)
        {
            const float ms = sp.lineMinMs * std::pow (sp.lineMaxMs / sp.lineMinMs, (float) i / (float) (numLines - 1))
                           * (1.0f + 0.035f * std::sin (2.7f * (float) i));
            lineTarget[(size_t) i] = ms * factor * msToSamples;

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
            const float seconds = lineLength[(size_t) i] / (float) sr;

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

    float Reverb::shelfMagnitudeAtMid (float lowGain, float highGain) const
    {
        using C = std::complex<float>;
        const C zi = std::polar (1.0f, -juce::MathConstants<float>::twoPi * 1000.0f / (float) sr);   // z^-1
        const C lp = 0.5f * (1.0f - shelfLowA) * (1.0f + zi) / (1.0f - shelfLowA * zi);
        const C hp = 0.5f * (1.0f + shelfHighA) * (1.0f - zi) / (1.0f - shelfHighA * zi);
        return std::abs ((1.0f + (lowGain - 1.0f) * lp) * (1.0f + (highGain - 1.0f) * hp));
    }

    void Reverb::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples)
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
            diffusionStep = (target.diffuse * 0.72f - diffusion) * k * n;
        };

        if (controlCounter == 0)
            plan();

        std::array<float, numLines> y {}, f {};

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
            float el = 0.0f, er = 0.0f;

            for (int t = 0; t < numTaps; ++t)
            {
                el += mid.readLinear (tapDelayL[(size_t) t]) * tapGainL[(size_t) t];
                er += mid.readLinear (tapDelayR[(size_t) t]) * tapGainR[(size_t) t];
                tapDelayL[(size_t) t] += stepL[(size_t) t];
                tapDelayR[(size_t) t] += stepR[(size_t) t];
                tapGainL[(size_t) t] += gainStepL[(size_t) t];
                tapGainR[(size_t) t] += gainStepR[(size_t) t];
            }

            el = airL.lowpass (el);
            er = airR.lowpass (er);

            // Tail input: the predelayed signal and its reflections, so the
            // tail grows out of the room. Coloured, lowcut and diffused.
            float tl = brightL.lowpass (0.6f * inputL.readLinear (predelay) + 0.8f * el);
            float tr = brightR.lowpass (0.6f * inputR.readLinear (predelay) + 0.8f * er);
            el *= earlyGain;
            er *= earlyGain;
            lowcutL = tl + lowcutA * (lowcutL - tl);
            lowcutR = tr + lowcutA * (lowcutR - tr);
            tl -= lowcutL;
            tr -= lowcutR;

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

                y[(size_t) i] = lines[(size_t) i].readCubic (lineLength[(size_t) i] + modDepth[(size_t) i] * sn);
                f[(size_t) i] = shelves[(size_t) i].process (y[(size_t) i], ratioLow[(size_t) i], ratioHigh[(size_t) i], shelfLowA, shelfHighA)
                                * gainMid[(size_t) i];
                lineLength[(size_t) i] += lineStep[(size_t) i];
            }

            hadamard (f);

            float ol = 0.0f, orr = 0.0f;

            for (int i = 0; i < numLines; ++i)
            {
                const float in = (i & 1) != 0 ? tr : tl;
                lines[(size_t) i].push (f[(size_t) i] + in * injectGain * sign (((i >> 1) & 1) != 0) * 0.5f);
                ol += y[(size_t) i] * sign (parity (i & 6) != 0);
                orr += y[(size_t) i] * sign (parity (i & 10) != 0);
            }

            ol = ol * 0.25f + el;
            orr = orr * 0.25f + er;

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

        // Living: quiet playing leaves a longer, darker space; loud playing
        // a brighter, tighter one.
        rtScale = (1.0f + 0.8f * s.charge * stored * awake)
                * std::pow (2.0f, s.living * (0.5f - heldLevel) * 0.8f);

        const float open = 2500.0f * std::pow (16000.0f / 2500.0f, heldLevel);
        brightHz = std::exp (std::log (9000.0f) + s.living * (std::log (open) - std::log (9000.0f)));
    }
}
