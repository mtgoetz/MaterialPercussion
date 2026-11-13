#include "Waveguide.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace mp
{
namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr double kMinFreq = 20.0;
constexpr double kInputGain = 0.7;  // full mallet hit -> displacement around 1, like the modal bank

using Complex = std::complex<double>;

Complex onePoleLossResponse (double a, double omega) noexcept  // (1 + a) / (1 + a z^-1), unit DC gain
{
    return (1.0 + a) / (1.0 + a * std::polar (1.0, -omega));
}

Complex dcBlockerResponse (double R, double omega) noexcept    // (1+R)/2 (1 - z^-1) / (1 - R z^-1), gain <= 1
{
    const Complex z1 = std::polar (1.0, -omega);
    return 0.5 * (1.0 + R) * (1.0 - z1) / (1.0 - R * z1);
}

Complex allpassResponse (double c, double omega) noexcept      // (c + z^-1) / (1 + c z^-1)
{
    const Complex z1 = std::polar (1.0, -omega);
    return (c + z1) / (1.0 + c * z1);
}

double phaseDelay (Complex h, double omega) noexcept
{
    return -std::arg (h) / omega;
}

int nextPowerOfTwo (int n) noexcept
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}
} // namespace

void Waveguide::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    const int size = nextPowerOfTwo ((int) (sampleRate / (kMinFreq * 0.9)) + 16);
    for (auto& loop : loops)
    {
        loop.buf.assign ((size_t) size, 0.0f);
        loop.mask = size - 1;
    }
    history.assign ((size_t) size, 0.0f);
    historyMask = size - 1;

    leak = 1.0 - kTwoPi * 5.0 / sampleRate;
    dcR = 1.0 - kTwoPi * 6.0 / sampleRate;
    reset();
}

void Waveguide::reset() noexcept
{
    for (auto& loop : loops)
    {
        std::fill (loop.buf.begin(), loop.buf.end(), 0.0f);
        loop.lp = loop.dcX = loop.dcY = loop.x = loop.xStereo = 0.0;
        std::fill (std::begin (loop.apX), std::end (loop.apX), 0.0);
        std::fill (std::begin (loop.apY), std::end (loop.apY), 0.0);
    }
    std::fill (history.begin(), history.end(), 0.0f);
    u = uStereo = v = 0.0;
}

bool Waveguide::isBlownUp() const noexcept
{
    return ! std::isfinite (u) || std::abs (u) > 1.0e3;
}

void Waveguide::setupLoop (Loop& loop, const ModeParams& p, double f0, double length, double sign,
                           bool dispersive, double strikeX) noexcept
{
    loop.length = length;
    loop.sign = sign;
    loop.dcBlock = sign > 0.0; // an inverting loop cannot build up DC
    const double omega1 = kTwoPi * f0 / sampleRate;

    // --- Loss filter: match the decay law at the fundamental and at an upper harmonic.
    const double loopTime = length / sampleRate;
    const double kRatio = std::min (sign > 0.0 ? 8.0 : 7.0, 0.35 * sampleRate / f0);
    const double g1 = std::pow (10.0, -3.0 * loopTime / modeDecaySeconds (p, 1.0));
    double a = 0.0;
    if (kRatio > 1.5)
    {
        const double gk = std::pow (10.0, -3.0 * loopTime / modeDecaySeconds (p, kRatio));
        const double target = gk / g1;
        auto ratioFor = [&] (double coeff)
        {
            return std::abs (onePoleLossResponse (coeff, omega1 * kRatio)) / std::abs (onePoleLossResponse (coeff, omega1));
        };
        if (target < 1.0)
        {
            double lo = -0.95, hi = 0.0; // ratio falls as the coefficient goes negative
            if (ratioFor (lo) > target)
                hi = lo;
            else
                for (int it = 0; it < 40; ++it)
                {
                    const double mid = 0.5 * (lo + hi);
                    if (ratioFor (mid) > target) hi = mid;
                    else                         lo = mid;
                }
            a = hi;
        }
    }
    // Very short loops (high notes) cannot absorb a slow filter's delay: back off. Keep a
    // little high-frequency loss so the interpolator can never push the loop gain past 1.
    while (a < -0.06 && phaseDelay (onePoleLossResponse (a, omega1), omega1) > 0.3 * length)
        a *= 0.8;
    a = std::min (a, -0.05);
    loop.a = a;

    double gain = g1 / std::abs (onePoleLossResponse (a, omega1));
    if (loop.dcBlock)
        gain /= std::abs (dcBlockerResponse (dcR, omega1));
    loop.g = std::min (gain, 0.99995);

    // --- Dispersion (stiffness): allpasses delay low partials more than high ones.
    loop.numAllpass = dispersive ? 4 : 0;
    double c = -0.62 * p.shape;
    double apDelay = 0.0;
    for (int tries = 0; dispersive && tries < 30; ++tries)
    {
        apDelay = 4.0 * phaseDelay (allpassResponse (c, omega1), omega1);
        if (apDelay < 0.45 * length)
            break;
        c *= 0.8;
    }
    loop.apC = c;

    // --- Tuning: subtract the filters' phase delay at the fundamental from the delay line.
    double comp = phaseDelay (onePoleLossResponse (a, omega1), omega1);
    if (loop.dcBlock)
        comp += phaseDelay (dcBlockerResponse (dcR, omega1), omega1);
    if (dispersive)
        comp += apDelay;
    loop.comp = comp;

    loop.combTap = std::max (1, (int) std::lround (strikeX * length));
    loop.stereoTap = std::max (1, (int) std::lround (0.37 * length));
}

void Waveguide::configure (const ModeParams& p, double baseFreq, float newWidth) noexcept
{
    const double f0 = std::clamp (baseFreq, kMinFreq, sampleRate / 16.0);
    s0 = std::sin (kTwoPi * f0 / sampleRate);
    kin = kInputGain / s0;
    width = newWidth;

    if (p.model == Model::Tube)
    {
        numLoops = 2;
        const double open = std::clamp ((double) p.shape, 0.0, 1.0);
        setupLoop (loops[0], p, f0, sampleRate / (2.0 * f0), -1.0, false, tubeX (p.position));
        setupLoop (loops[1], p, f0, sampleRate / f0, 1.0, false, tubeX (p.position));
        loops[0].weight = std::sqrt (1.0 - open);
        loops[1].weight = std::sqrt (open);
    }
    else
    {
        numLoops = 1;
        setupLoop (loops[0], p, f0, sampleRate / f0, 1.0, true, wireX (p.position));
        loops[0].weight = 1.0;
    }

    // RMS response to unit white-noise force (approximate: comb x loop x integrator).
    const double g1 = std::pow (10.0, -3.0 / (f0 * modeDecaySeconds (p, 1.0)));
    noiseRms = kin * std::sqrt (6.6 * f0 / sampleRate / std::max (1.0e-4, 1.0 - g1 * g1));

    setFrequencyScale (1.0);
}

void Waveguide::setFrequencyScale (double scale) noexcept
{
    for (int k = 0; k < numLoops; ++k)
    {
        auto& loop = loops[k];
        loop.delay = std::clamp (loop.length / std::max (0.1, scale) - loop.comp, 3.0, (double) loop.mask - 4.0);
    }
}

void Waveguide::process (Loop& loop, double in) noexcept
{
    // 4-point Hermite read of the sample written `delay` samples ago
    const double readPos = (double) loop.writePos - loop.delay;
    const double floorPos = std::floor (readPos);
    const double t = readPos - floorPos;
    const int i = (int) floorPos;
    const double ym1 = loop.buf[(size_t) ((i - 1) & loop.mask)];
    const double y0 = loop.buf[(size_t) (i & loop.mask)];
    const double y1 = loop.buf[(size_t) ((i + 1) & loop.mask)];
    const double y2 = loop.buf[(size_t) ((i + 2) & loop.mask)];
    const double c1 = 0.5 * (y1 - ym1);
    const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
    const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
    double x = ((c3 * t + c2) * t + c1) * t + y0;

    loop.lp = loop.g * (1.0 + loop.a) * x - loop.a * loop.lp;
    x = loop.lp;

    if (loop.dcBlock)
    {
        const double y = 0.5 * (1.0 + dcR) * (x - loop.dcX) + dcR * loop.dcY;
        loop.dcX = x;
        loop.dcY = y;
        x = y;
    }

    for (int k = 0; k < loop.numAllpass; ++k)
    {
        const double y = loop.apC * x + loop.apX[k] - loop.apC * loop.apY[k];
        loop.apX[k] = x;
        loop.apY[k] = y;
        x = y;
    }

    x = loop.sign * x + in;
    loop.buf[(size_t) loop.writePos] = (float) x;
    loop.writePos = (loop.writePos + 1) & loop.mask;
    loop.x = x;
    loop.xStereo = loop.buf[(size_t) ((loop.writePos - 1 - loop.stereoTap) & loop.mask)];
}

void Waveguide::tick (double e, double f, double& outL, double& outR) noexcept
{
    const double in = kin * e + f / s0;
    history[(size_t) historyPos] = (float) in;

    double x = 0.0, xs = 0.0;
    for (int k = 0; k < numLoops; ++k)
    {
        auto& loop = loops[k];
        const double delayed = history[(size_t) ((historyPos - loop.combTap) & historyMask)];
        process (loop, loop.weight * (in - delayed));
        x += loop.weight * loop.x;
        xs += loop.weight * loop.xStereo;
    }
    historyPos = (historyPos + 1) & historyMask;

    v = x;
    u = leak * u + x * s0;
    uStereo = leak * uStereo + xs * s0;

    const double side = 0.5 * width * (uStereo - u);
    outL = u - side;
    outR = u + side;
}
} // namespace mp
