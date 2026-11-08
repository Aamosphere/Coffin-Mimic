// Pitch shift quality: MimicStretchProbe
#include <JuceHeader.h>
#include "../Source/Answer.h"

int main()
{
    const double sr = 48000.0;
    mimic::Capture c; c.prepare (sr, 4.5);
    const int n = (int) (2.0 * sr);
    c.length = n;
    // A sung-ish tone: 220 Hz saw with a few harmonics, soft.
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        ph += 220.0 / sr; ph -= std::floor (ph);
        float x = 0; for (int h = 1; h <= 12; ++h) x += (float) (std::sin (6.283185307 * ph * h) / h);
        c.data[0][(size_t) i] = c.data[1][(size_t) i] = 0.2f * x;
    }
    // smooth copy = same (no content above sr/4 anyway at 12 harmonics of 220)
    for (int ch = 0; ch < 2; ++ch) c.smooth[ch] = c.data[ch];

    for (float pitch : { 2.0f, 0.5f, 1.4983f })
      for (int align = 0; align < 2; ++align)
      {
        mimic::Stretch s; s.prepare (sr);
        s.start (&c, 0, n - 1, 1.0, pitch, false, align == 1);
        std::vector<float> y;
        float l, r;
        while (s.render (l, r) || s.active()) { y.push_back (l); if (y.size() > (size_t) (3 * sr)) break; }
        // envelope ripple over the middle (5 ms rms)
        const double period = sr / (220.0 * pitch); const int w = (int) std::lround (period * std::ceil (0.01 * sr / period));
        std::vector<double> e;
        for (size_t i = (size_t) (0.2 * sr); i + (size_t) w < y.size() - (size_t) (0.2 * sr); i += (size_t) w)
        { double s2 = 0; for (int k = 0; k < w; ++k) s2 += y[i + (size_t) k] * y[i + (size_t) k]; e.push_back (std::sqrt (s2 / w)); }
        double mean = 0; for (auto v : e) mean += v; mean /= (double) e.size();
        double var = 0; for (auto v : e) var += (v - mean) * (v - mean); var /= (double) e.size();
        // frequency by zero crossings (positive-going) of the lowpassed signal
        int zc = 0; float lp = 0, prev = 0; const size_t a = (size_t) (0.2 * sr), b = y.size() - (size_t) (0.2 * sr);
        for (size_t i = a; i < b; ++i) { lp += 0.05f * (y[i] - lp); if (prev <= 0 && lp > 0) ++zc; prev = lp; }
        const double hz = zc / ((double) (b - a) / sr);
        std::printf ("pitch %.3f align %d: %.1f Hz (want %.1f)  length %.2fs  ripple %.1f %%\n", pitch, align, hz, 220.0 * pitch, y.size() / sr, 100.0 * std::sqrt (var) / mean);
      }
}
