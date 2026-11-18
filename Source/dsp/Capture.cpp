#include "Capture.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace mp
{
namespace
{
using cd = std::complex<double>;
constexpr double kPi = 3.14159265358979323846;
constexpr double kLn1000 = 6.907755278982137;

constexpr double kMaxSeconds = 4.0;          // analysed after the onset
constexpr double kMinSeconds = 0.05;
constexpr int kMaxCandidates = 260;          // spectral peaks that get a fit
constexpr double kMaxT60 = 60.0;
constexpr double kResidualMaxSeconds = 0.6;

bool cancelled (const std::atomic<bool>* c) { return c != nullptr && c->load (std::memory_order_relaxed); }

//==============================================================================
// Small numerical toolkit

void fft (std::vector<cd>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }

    std::vector<cd> twiddle;
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const size_t half = len / 2;
        twiddle.resize (half);
        for (size_t k = 0; k < half; ++k)
            twiddle[k] = std::polar (1.0, -2.0 * kPi * (double) k / (double) len);

        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < half; ++k)
            {
                const cd u = a[i + k], v = a[i + k + half] * twiddle[k];
                a[i + k] = u + v;
                a[i + k + half] = u - v;
            }
    }
}

// Gaussian elimination with partial pivoting. `a` is n x n, row-major.
bool solve (std::vector<cd> a, std::vector<cd> b, std::vector<cd>& x)
{
    const int n = (int) b.size();
    for (int col = 0; col < n; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < n; ++r)
            if (std::abs (a[(size_t) (r * n + col)]) > std::abs (a[(size_t) (pivot * n + col)]))
                pivot = r;
        if (std::abs (a[(size_t) (pivot * n + col)]) < 1.0e-300)
            return false;
        if (pivot != col)
        {
            for (int c = 0; c < n; ++c)
                std::swap (a[(size_t) (col * n + c)], a[(size_t) (pivot * n + c)]);
            std::swap (b[(size_t) col], b[(size_t) pivot]);
        }
        for (int r = col + 1; r < n; ++r)
        {
            const cd f = a[(size_t) (r * n + col)] / a[(size_t) (col * n + col)];
            for (int c = col; c < n; ++c)
                a[(size_t) (r * n + c)] -= f * a[(size_t) (col * n + c)];
            b[(size_t) r] -= f * b[(size_t) col];
        }
    }

    x.assign ((size_t) n, cd());
    for (int r = n - 1; r >= 0; --r)
    {
        cd s = b[(size_t) r];
        for (int c = r + 1; c < n; ++c)
            s -= a[(size_t) (r * n + c)] * x[(size_t) c];
        x[(size_t) r] = s / a[(size_t) (r * n + r)];
    }
    return true;
}

// A little diagonal loading, so nearly singular systems give a sensible small answer.
void regularise (std::vector<cd>& a, int n, double amount)
{
    double trace = 0.0;
    for (int i = 0; i < n; ++i)
        trace += std::abs (a[(size_t) (i * n + i)]);
    const double ridge = amount * trace / n + 1.0e-300;
    for (int i = 0; i < n; ++i)
        a[(size_t) (i * n + i)] += ridge;
}

// Roots of z^p + c[0] z^(p-1) + ... + c[p-1] (Durand-Kerner iteration).
std::vector<cd> polyRoots (const std::vector<cd>& c)
{
    const int p = (int) c.size();
    if (p == 1)
        return { -c[0] };

    std::vector<cd> z ((size_t) p);
    const cd seed (0.4, 0.9);
    cd s (1.0, 0.0);
    for (auto& zi : z) { zi = s; s *= seed; }

    auto eval = [&c] (cd x)
    {
        cd v (1.0, 0.0);
        for (const auto& ci : c)
            v = v * x + ci;
        return v;
    };

    for (int it = 0; it < 800; ++it)
    {
        double change = 0.0;
        for (int i = 0; i < p; ++i)
        {
            cd den (1.0, 0.0);
            for (int j = 0; j < p; ++j)
                if (j != i)
                    den *= z[(size_t) i] - z[(size_t) j];
            if (std::abs (den) < 1.0e-200)
                den = cd (1.0e-12, 0.0);
            const cd d = eval (z[(size_t) i]) / den;
            z[(size_t) i] -= d;
            change = std::max (change, std::abs (d));
        }
        if (change < 1.0e-13)
            break;
    }
    return z;
}

//==============================================================================
// One narrow band of the recording, shifted down to 0 Hz and decimated.
struct Subband
{
    std::vector<cd> z;
    int decimation = 1;
    int firstIndex = 0; // full-rate sample index that z[0] belongs to
};

// Heterodyne by fc, three cascaded length-D moving averages, keep every D-th output.
// A damped exponential passes through this unchanged apart from a known complex gain,
// which is what makes the per-band fit exact rather than approximate.
Subband extractBand (const std::vector<double>& y, double fc, double sr, int D)
{
    Subband s;
    s.decimation = D;
    s.firstIndex = 3 * (D - 1);
    const size_t n = y.size();
    if ((size_t) s.firstIndex >= n)
        return s;
    s.z.reserve ((n - (size_t) s.firstIndex) / (size_t) D + 1);

    std::vector<cd> r1 ((size_t) D), r2 ((size_t) D), r3 ((size_t) D);
    cd a1, a2, a3;
    const cd step = std::polar (1.0, -2.0 * kPi * fc / sr);
    cd rot (1.0, 0.0);
    const double invD = 1.0 / D;
    size_t idx = 0;
    int countdown = s.firstIndex;

    for (size_t i = 0; i < n; ++i)
    {
        const cd x = y[i] * rot;
        rot *= step;
        if ((i & 1023) == 1023)
            rot /= std::abs (rot);

        a1 += x - r1[idx];  r1[idx] = x;
        const cd o1 = a1 * invD;
        a2 += o1 - r2[idx]; r2[idx] = o1;
        const cd o2 = a2 * invD;
        a3 += o2 - r3[idx]; r3[idx] = o2;
        if (++idx == (size_t) D)
            idx = 0;

        if (countdown-- == 0)
        {
            s.z.push_back (a3 * invD);
            countdown = D - 1;
        }
    }
    return s;
}

struct Pole
{
    cd q;            // per decimated sample
    cd c;            // complex amplitude at the start of the fit
    double energy = 0.0;
};

// Least-squares amplitudes for z[k0 + k] = sum_i c_i q_i^k. Returns the residual energy.
double fitAmplitudes (const std::vector<cd>& z, int k0, int k1, std::vector<Pole>& poles)
{
    const int p = (int) poles.size(), n = k1 - k0;
    std::vector<cd> basis ((size_t) (p * n));
    for (int i = 0; i < p; ++i)
    {
        cd v (1.0, 0.0);
        for (int k = 0; k < n; ++k)
        {
            basis[(size_t) (i * n + k)] = v;
            v *= poles[(size_t) i].q;
            if (std::abs (v) > 1.0e100)
                v *= 1.0e100 / std::abs (v);
        }
    }

    std::vector<cd> g ((size_t) (p * p)), h ((size_t) p), c;
    for (int i = 0; i < p; ++i)
    {
        const cd* bi = &basis[(size_t) (i * n)];
        for (int j = 0; j < p; ++j)
        {
            const cd* bj = &basis[(size_t) (j * n)];
            cd s;
            for (int k = 0; k < n; ++k)
                s += std::conj (bi[k]) * bj[k];
            g[(size_t) (i * p + j)] = s;
        }
        cd s;
        for (int k = 0; k < n; ++k)
            s += std::conj (bi[k]) * z[(size_t) (k0 + k)];
        h[(size_t) i] = s;
    }
    regularise (g, p, 1.0e-10);
    if (! solve (g, h, c))
        c.assign ((size_t) p, cd());

    double residual = 0.0;
    for (int k = 0; k < n; ++k)
    {
        cd model;
        for (int i = 0; i < p; ++i)
            model += c[(size_t) i] * basis[(size_t) (i * n + k)];
        residual += std::norm (z[(size_t) (k0 + k)] - model);
    }
    for (int i = 0; i < p; ++i)
    {
        double e = 0.0;
        for (int k = 0; k < n; ++k)
            e += std::norm (basis[(size_t) (i * n + k)]);
        poles[(size_t) i].c = c[(size_t) i];
        poles[(size_t) i].energy = std::norm (c[(size_t) i]) * e;
    }
    return residual;
}

// Poles of an order-p forward linear predictor fitted to z[k0..k1). The extra order beyond
// the one or two real modes soaks up noise, which keeps the real poles unbiased.
std::vector<cd> predictionPoles (const std::vector<cd>& z, int k0, int k1, int p)
{
    std::vector<cd> r ((size_t) (p * p)), rhs ((size_t) p), a;
    for (int k = k0 + p; k < k1; ++k)
        for (int i = 1; i <= p; ++i)
        {
            const cd zi = std::conj (z[(size_t) (k - i)]);
            for (int j = 1; j <= p; ++j)
                r[(size_t) ((i - 1) * p + (j - 1))] += zi * z[(size_t) (k - j)];
            rhs[(size_t) (i - 1)] -= zi * z[(size_t) k];
        }
    regularise (r, p, 1.0e-9);
    if (! solve (r, rhs, a))
        return {};
    return polyRoots (a);
}

//==============================================================================
struct Found
{
    double hz = 0.0, amp = 0.0, phase = 0.0;
    double alpha = 0.0; // decay rate, 1/s
    double t60 = 1.0;
    double energy = 0.0;
};

Found toMode (const Pole& pole, double fc, double sr, const Subband& band, int k0, bool bloomed)
{
    const int D = band.decimation;
    const double mag = std::min (std::abs (pole.q), 1.0 - 1.0e-12);
    const double logR = std::log (mag) / D;    // per full-rate sample
    const double w = std::arg (pole.q) / D;    // baseband rad per full-rate sample
    const cd logP (logR, w);

    // Gain of the three moving averages at this pole: ((1/D) sum_i p^-i)^3
    const cd pInv = std::exp (-logP);
    cd sum;
    if (std::abs (1.0 - pInv) < 1.0e-9)
        sum = (double) D;
    else
        sum = (1.0 - std::exp (-logP * (double) D)) / (1.0 - pInv);
    const cd gain = std::pow (sum / (double) D, 3);

    // Back to the onset in two steps: from the fit start (the band's peak) to the band's first
    // sample, which is extrapolation over data the fit did not see (so limited to 12 dB),
    // then over the filters' start-up, where the model is exact.
    const cd atFit = pole.c / gain;                             // at full-rate sample firstIndex + k0 * D
    cd atFirst = atFit / std::exp (logP * ((double) k0 * D));   // at full-rate sample firstIndex
    if (bloomed || ! std::isfinite (std::abs (atFirst)))
        atFirst = std::polar (std::abs (atFit), std::arg (atFirst)); // grew after the hit: don't extrapolate
    else if (std::abs (atFirst) > 4.0 * std::abs (atFit))
        atFirst *= 4.0 * std::abs (atFit) / std::abs (atFirst);
    cd atOnset = atFirst / std::exp (logP * (double) band.firstIndex);
    if (! std::isfinite (std::abs (atOnset)))
        atOnset = atFirst;

    Found f;
    f.hz = fc + w * sr / (2.0 * kPi);
    f.alpha = -logR * sr;
    f.t60 = f.alpha > kLn1000 / kMaxT60 ? kLn1000 / f.alpha : kMaxT60;
    f.alpha = kLn1000 / f.t60;
    f.amp = 2.0 * std::abs (atOnset);
    f.phase = std::arg (atOnset);
    f.energy = f.amp * f.amp * std::min (f.t60, 4.0);
    return f;
}

// Fits one or two modes in the band around fc (half-width about bw Hz).
void fitCandidate (const std::vector<double>& y, double sr, double fc, double bw, std::vector<Found>& found)
{
    const int n = (int) y.size();
    double rateTarget = 2.0 * bw;

    for (int attempt = 0; attempt < 5; ++attempt, rateTarget *= 2.0)
    {
        int D = std::max (1, (int) (sr / rateTarget));
        if ((n - 3 * D) / D < 24)
            D = std::max (1, n / 27);

        const auto band = extractBand (y, fc, sr, D);
        const auto& z = band.z;
        const int N = (int) z.size();
        if (N < 12)
            return;
        const double rate = sr / D;

        int kPeak = 0;
        double peak = 0.0;
        for (int k = 0; k < N; ++k)
            if (std::abs (z[(size_t) k]) > peak) { peak = std::abs (z[(size_t) k]); kPeak = k; }
        if (peak < 1.0e-9)
            return;

        // Fit from the peak until the band sinks into its noise floor (measured at the end).
        std::vector<double> tail;
        for (int k = N - std::max (4, N / 5); k < N; ++k)
            tail.push_back (std::abs (z[(size_t) k]));
        std::nth_element (tail.begin(), tail.begin() + (long) (tail.size() / 2), tail.end());
        const double tailLevel = tail[tail.size() / 2];

        int k1 = N;
        if (tailLevel < 0.05 * peak) // still ringing at the end? then use everything
        {
            const double floorLevel = std::max (peak * 1.0e-3, 4.0 * tailLevel);
            k1 = kPeak + 1;
            for (int k = N - 1; k > kPeak; --k)
                if (std::abs (z[(size_t) k]) > floorLevel) { k1 = k + 1; break; }
        }
        const int k0 = kPeak;
        if (k1 - k0 < 10)
        {
            if (D == 1)
                return;
            continue; // dies too fast for this band: try a wider one
        }

        const int order = std::clamp ((k1 - k0) / 4, 1, 6);
        const auto roots = predictionPoles (z, k0, k1, order);
        if (roots.empty())
            return;

        std::vector<Pole> all;
        for (const auto& q : roots)
            all.push_back ({ q, {}, 0.0 });
        fitAmplitudes (z, k0, k1, all);

        // Plausible modes: decaying (or holding), and inside the band
        std::vector<Pole> good;
        for (const auto& pl : all)
        {
            const double offset = std::arg (pl.q) * rate / (2.0 * kPi);
            if (std::abs (pl.q) < 1.02 && std::abs (pl.q) > 1.0e-6 && std::abs (offset) < 0.4 * rate)
                good.push_back (pl);
        }
        if (good.empty())
            continue;
        std::sort (good.begin(), good.end(), [] (const Pole& a, const Pole& b) { return a.energy > b.energy; });

        double total = 0.0;
        for (int k = k0; k < k1; ++k)
            total += std::norm (z[(size_t) k]);

        std::vector<Pole> chosen { good[0] };
        double fitResidual = fitAmplitudes (z, k0, k1, chosen);
        const double r1 = fitResidual;

        // A second mode close by (a beating doublet) if it explains much more of the band
        if (good.size() > 1 && r1 > 1.0e-3 * total)
        {
            std::vector<Pole> two { good[0], good[1] };
            const double r2 = fitAmplitudes (z, k0, k1, two);
            const double a1 = std::abs (two[0].c), a2 = std::abs (two[1].c), single = std::abs (chosen[0].c);
            const double sepHz = std::abs (std::arg (two[0].q) - std::arg (two[1].q)) * rate / (2.0 * kPi);
            if (r2 < 0.5 * r1 && std::min (a1, a2) > 0.08 * std::max (a1, a2)
                && std::max (a1, a2) < 3.0 * single && sepHz > 0.05)
            {
                chosen = two;
                fitResidual = r2;
            }
        }

        // A band of noise (the click, hiss) gives poles too, but they explain little of it.
        if (fitResidual > 0.4 * total)
            return;

        const bool bloomed = k0 > 0 && std::abs (z[0]) < 0.5 * peak;
        for (const auto& pl : chosen)
            found.push_back (toMode (pl, fc, sr, band, k0, bloomed));
        return;
    }
}
} // namespace

//==============================================================================
std::string analyseRecording (const float* samples, int numSamples, double sr, CaptureData& out,
                              const std::atomic<bool>* cancel)
{
    if (samples == nullptr || numSamples <= 0 || sr <= 0.0)
        return "The file has no audio in it.";

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max (peak, std::abs (samples[i]));
    if (! (peak > 1.0e-5f) || ! std::isfinite (peak))
        return "The recording is silent.";

    // 1. Onset: first sample within 20 dB of the peak, minus 1 ms.
    int hit = 0;
    while (hit < numSamples && std::abs (samples[hit]) < 0.1f * peak)
        ++hit;
    const int onset = std::max (0, hit - (int) (0.001 * sr));
    const int len = std::min (numSamples - onset, (int) (kMaxSeconds * sr));
    if (len < (int) (kMinSeconds * sr))
        return "The sound is too short: it needs at least 50 ms after the hit.";

    std::vector<double> y ((size_t) len);
    double mean = 0.0;
    for (int i = 0; i < len; ++i)
    {
        y[(size_t) i] = samples[onset + i] / (double) peak;
        mean += y[(size_t) i];
    }
    mean /= len;
    for (auto& v : y)
        v -= mean;

    // 2. One long FFT, windowed by the falling half of a Hann window (the hit is at the start).
    size_t maxL = 1;
    while ((double) maxL < 2.5 * sr)
        maxL *= 2;
    size_t L = 1;
    while (L * 2 <= (size_t) len && L * 2 <= maxL)
        L *= 2;
    const size_t M = 2 * L;
    std::vector<cd> spectrum (M);
    for (size_t i = 0; i < L; ++i)
        spectrum[i] = y[i] * 0.5 * (1.0 + std::cos (kPi * (double) i / (double) L));
    fft (spectrum);
    if (cancelled (cancel))
        return "Cancelled.";

    const double df = sr / (double) M;
    const int kMin = std::max (3, (int) (20.0 / df));
    const int kMax = (int) std::min ((double) M / 2 - 3, std::min (0.45 * sr, 18000.0) / df);
    if (kMax <= kMin + 4)
        return "The sample rate is too low to analyse.";

    std::vector<float> db (M / 2);
    float dbMax = -400.0f;
    for (size_t k = 0; k < M / 2; ++k)
    {
        db[k] = (float) (10.0 * std::log10 (std::norm (spectrum[k]) + 1.0e-30));
        if ((int) k >= kMin && (int) k <= kMax)
            dbMax = std::max (dbMax, db[k]);
    }

    // Local floor: median level within +-10% of each frequency, on a 1/24-octave grid.
    const int gridCount = (int) std::ceil (std::log2 ((kMax * df) / 20.0) * 24.0) + 2;
    std::vector<double> gridDb ((size_t) gridCount);
    std::vector<float> window;
    for (int gi = 0; gi < gridCount; ++gi)
    {
        const double f = 20.0 * std::exp2 (gi / 24.0);
        const int lo = std::clamp ((int) (f / 1.1 / df), kMin, kMax);
        const int hi = std::clamp ((int) (f * 1.1 / df) + 1, lo + 1, kMax + 1);
        const int stride = std::max (1, (hi - lo) / 801);
        window.clear();
        for (int k = lo; k < hi; k += stride)
            window.push_back (db[(size_t) k]);
        std::nth_element (window.begin(), window.begin() + (long) (window.size() / 2), window.end());
        gridDb[(size_t) gi] = window[window.size() / 2];
    }
    auto floorAt = [&] (double f)
    {
        const double pos = std::clamp (std::log2 (std::max (f, 20.0) / 20.0) * 24.0, 0.0, gridCount - 1.001);
        const int i = (int) pos;
        return std::lerp (gridDb[(size_t) i], gridDb[(size_t) i + 1], pos - i);
    };

    struct Candidate { double hz; float level; };
    std::vector<Candidate> candidates;
    for (int k = kMin + 2; k <= kMax - 2; ++k)
    {
        const float v = db[(size_t) k];
        if (v > db[(size_t) k - 1] && v >= db[(size_t) k + 1] && v > db[(size_t) k - 2] && v >= db[(size_t) k + 2]
            && v > dbMax - 80.0f && v > floorAt (k * df) + 8.0)
        {
            const double a = db[(size_t) k - 1], b = v, c = db[(size_t) k + 1];
            const double den = a - 2.0 * b + c;
            const double d = std::abs (den) > 1.0e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
            candidates.push_back ({ (k + d) * df, v });
        }
    }
    if (candidates.empty())
        return "No resonances found: is this a pitched or ringing sound?";

    std::sort (candidates.begin(), candidates.end(), [] (const Candidate& a, const Candidate& b) { return a.level > b.level; });
    if ((int) candidates.size() > kMaxCandidates)
        candidates.resize ((size_t) kMaxCandidates);
    std::sort (candidates.begin(), candidates.end(), [] (const Candidate& a, const Candidate& b) { return a.hz < b.hz; });

    // 3. Fit each candidate's band. The band is half the distance to its neighbours.
    std::vector<Found> found;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (cancelled (cancel))
            return "Cancelled.";
        double gap = 1.0e9;
        if (i > 0)
            gap = std::min (gap, candidates[i].hz - candidates[i - 1].hz);
        if (i + 1 < candidates.size())
            gap = std::min (gap, candidates[i + 1].hz - candidates[i].hz);
        const double fc = candidates[i].hz;
        const double bw = std::clamp (0.5 * gap, 3.0, std::min (60.0, std::max (3.0, 0.3 * fc)));
        fitCandidate (y, sr, fc, bw, found);
    }

    // 4. Neighbouring bands can find the same mode: keep the strongest of each cluster.
    //    The recording peaks at 1, so no single mode can be much louder than that: anything
    //    that is is a bad fit.
    std::erase_if (found, [] (const Found& f) { return ! std::isfinite (f.amp) || ! std::isfinite (f.hz) || f.amp > 2.0; });
    std::sort (found.begin(), found.end(), [] (const Found& a, const Found& b) { return a.energy > b.energy; });
    double maxAmp = 0.0;
    for (const auto& f : found)
        maxAmp = std::max (maxAmp, f.amp);

    std::vector<Found> kept;
    for (const auto& f : found)
    {
        if (! std::isfinite (f.amp) || ! std::isfinite (f.hz) || f.amp < 3.0e-4 * maxAmp
            || f.hz < 20.0 || f.hz > std::min (0.45 * sr, 20000.0))
            continue;
        bool duplicate = false;
        for (const auto& k : kept)
            if (std::abs (k.hz - f.hz) < 0.15 + 1.0e-4 * f.hz) { duplicate = true; break; }
        if (duplicate)
            continue;
        kept.push_back (f);
        if ((int) kept.size() >= kMaxModes)
            break;
    }
    if (kept.empty())
        return "No modes could be fitted to this sound.";

    // Reference pitch: the lowest mode with a good share of the energy.
    double refHz = kept.front().hz;
    {
        double lowest = 1.0e9;
        for (const auto& f : kept)
            if (f.energy >= 0.15 * kept.front().energy)
                lowest = std::min (lowest, f.hz);
        refHz = std::clamp (lowest, 20.0, 2000.0);
    }

    // 5. Residual: the recording minus the fitted modes (with their measured phases).
    const int resLen = std::min (len, (int) (1.0 * sr));
    std::vector<double> res (y.begin(), y.begin() + resLen);
    double sumAmp2 = 0.0;
    for (const auto& f : kept)
    {
        sumAmp2 += f.amp * f.amp;
        cd v = std::polar (f.amp, f.phase);
        const cd step = std::polar (std::exp (-f.alpha / sr), 2.0 * kPi * f.hz / sr);
        for (int i = 0; i < resLen; ++i)
        {
            res[(size_t) i] -= v.real();
            v *= step;
        }
    }

    // Keep it until it has fallen 40 dB (at most 0.6 s), with a fade at the end.
    const int block = std::max (1, (int) (0.005 * sr));
    std::vector<double> blockRms;
    for (int start = 0; start < resLen; start += block)
    {
        double s = 0.0;
        const int end = std::min (resLen, start + block);
        for (int i = start; i < end; ++i)
            s += res[(size_t) i] * res[(size_t) i];
        blockRms.push_back (std::sqrt (s / (end - start)));
    }
    const double loudest = *std::max_element (blockRms.begin(), blockRms.end());
    int lastLoud = 0;
    for (int b = 0; b < (int) blockRms.size(); ++b)
        if (blockRms[(size_t) b] > loudest * 0.01)
            lastLoud = b;
    const int resEnd = std::clamp ((lastLoud + 2) * block, std::min (resLen, (int) (0.03 * sr)),
                                   std::min (resLen, (int) (kResidualMaxSeconds * sr)));
    const int fadeStart = (int) (0.7 * resEnd);

    const double modeNorm = 1.0 / std::sqrt (std::max (1.0e-30, sumAmp2));
    out.residual.assign ((size_t) resEnd, 0.0f);
    double residualEnergy = 0.0;
    for (int i = 0; i < resEnd; ++i)
    {
        double w = 1.0;
        if (i >= fadeStart)
            w = 0.5 * (1.0 + std::cos (kPi * (i - fadeStart) / std::max (1, resEnd - fadeStart)));
        const double v = res[(size_t) i] * w * modeNorm;
        out.residual[(size_t) i] = (float) v;
        residualEnergy += v * v;
    }
    out.residualRate = sr;
    out.residualForceNorm = (float) (1.0 / std::sqrt (std::max (1.0e-12, residualEnergy)));

    // 6. Modes, by frequency. The sign of each amplitude follows its measured phase, which
    //    gives the contact point a natural mix of in- and out-of-phase modes.
    std::sort (kept.begin(), kept.end(), [] (const Found& a, const Found& b) { return a.hz < b.hz; });
    out.modes.modes.clear();
    for (const auto& f : kept)
    {
        const double sign = std::cos (f.phase) >= 0.0 ? 1.0 : -1.0;
        out.modes.modes.push_back ({ (float) f.hz, (float) (sign * f.amp * modeNorm), (float) f.t60 });
    }
    out.modes.refHz = (float) refHz;

    // Overview for drawing
    constexpr int overviewSize = 256;
    out.overview.assign (overviewSize, 0.0f);
    float overviewMax = 1.0e-9f;
    for (int i = 0; i < len; ++i)
    {
        auto& slot = out.overview[(size_t) ((long long) i * overviewSize / len)];
        slot = std::max (slot, (float) std::abs (y[(size_t) i]));
        overviewMax = std::max (overviewMax, slot);
    }
    for (auto& v : out.overview)
        v /= overviewMax;

    out.seconds = (float) (len / sr);
    return {};
}
} // namespace mp
