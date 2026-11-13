#include "ModeModels.h"

#include <algorithm>
#include <vector>

namespace mp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

const ModelInfo kModelInfos[kNumModels] = {
    // name       blurb                                                      shape        shapeHint                                              decay  dMin  dMax  gain
    { "Membrane", "A stretched drum head: kicks, toms, snares, timpani",     "Tuning",    "Raw drum head (left) to tuned kettle drum (right)",   0.6f,  0.3f, 1.6f, 1.0f },
    { "Wood",     "A wooden bar: woodblocks, claves, marimba",               "Tuning",    "Free bar / woodblock (left) to tuned marimba bar",    0.35f, 0.8f, 2.6f, 1.1f },
    { "Metal",    "A metal plate: sheets, gongs, clangs",                    "Aspect",    "Square plate (left) to long rectangular plate",       1.5f,  0.0f, 1.0f, 0.6f },
    { "Glass",    "A glass bowl or wine glass: pure tones that beat",        "Thickness", "Thin wine glass (left) to thick bowl (right)",        1.8f,  0.1f, 1.0f, 0.6f },
    { "Bell",     "A cast bell: rich, slightly dissonant partials",          "Profile",   "Minor-third bell (left) to major-third bell",         2.0f,  0.3f, 1.2f, 0.6f },
    { "String",   "A struck string: piano, dulcimer, wire",                  "Stiffness", "Flexible string (left) to stiff wire (right)",        1.2f,  0.2f, 1.4f, 0.8f },
    { "Tube",     "A waveguide pipe: slap tubes, boomwhackers, blown pipes", "Bore",      "Stopped pipe, odd harmonics (left) to open pipe",     1.0f,  0.3f, 1.6f, 0.9f },
    { "Wire",     "A waveguide string: twangy, bends with Tension, buzzes with Rattle",
                                                                             "Stiffness", "Slack gut string (left) to stiff steel wire (right)", 1.4f,  0.3f, 1.5f, 0.9f },
};

//==============================================================================
// Bessel function of the first kind via its integral representation. The trapezoid
// rule is spectrally accurate here because the integrand is smooth and periodic.
double besselJ (int m, double x) noexcept
{
    constexpr int n = 128;
    double sum = 0.0;
    for (int k = 0; k <= n; ++k)
    {
        const double t = kPi * k / n;
        const double w = (k == 0 || k == n) ? 0.5 : 1.0;
        sum += w * std::cos (m * t - x * std::sin (t));
    }
    return sum / n;
}

struct MembraneMode
{
    int m = 0;      // angular order (number of nodal diameters)
    double j = 0.0; // Bessel zero -> frequency is proportional to this
};

struct MembraneTable
{
    std::array<MembraneMode, kMaxModes> modes {};
    std::array<double, kMaxModes> norm {};     // |J_{m+1}(j)|, modal-mass normalisation
    std::array<double, 32> firstZero {};       // j_{m,1} for each angular order
};

const MembraneTable& membraneTable()
{
    static const MembraneTable table = []
    {
        constexpr int maxOrder = 24;
        MembraneTable t;
        std::vector<MembraneMode> all;

        for (int m = 0; m <= maxOrder; ++m)
        {
            // j_{m,1} > m, and starting there avoids J_m's tiny values near zero.
            double prevX = std::max (0.5, (double) m);
            double prevV = besselJ (m, prevX);
            bool first = true;

            for (double x = prevX + 0.1; x < 40.0; x += 0.1)
            {
                const double v = besselJ (m, x);
                if ((prevV < 0.0) != (v < 0.0))
                {
                    double lo = prevX, hi = x, vLo = prevV;
                    for (int it = 0; it < 48; ++it)
                    {
                        const double mid = 0.5 * (lo + hi);
                        const double vMid = besselJ (m, mid);
                        if ((vMid < 0.0) == (vLo < 0.0)) { lo = mid; vLo = vMid; }
                        else                             { hi = mid; }
                    }
                    const double zero = 0.5 * (lo + hi);
                    if (first)
                    {
                        t.firstZero[(size_t) m] = zero;
                        first = false;
                    }
                    all.push_back ({ m, zero });
                }
                prevX = x;
                prevV = v;
            }
        }

        std::sort (all.begin(), all.end(), [] (const MembraneMode& a, const MembraneMode& b) { return a.j < b.j; });

        for (size_t i = 0; i < t.modes.size() && i < all.size(); ++i)
        {
            t.modes[i] = all[i];
            t.norm[i] = std::abs (besselJ (all[i].m + 1, all[i].j));
        }
        return t;
    }();
    return table;
}

//==============================================================================
struct DecayLaw
{
    float base;  // seconds at ratio 1
    float alpha; // decay ~ ratio^-alpha

    float operator() (double ratio) const noexcept
    {
        return std::max (0.01f, base * std::pow ((float) std::max (ratio, 0.1), -alpha));
    }
};

DecayLaw makeDecay (const ModeParams& p) noexcept
{
    const auto& info = getModelInfo (p.model);
    return { p.decay * info.decayScale, std::lerp (info.dampMin, info.dampMax, p.damping) };
}

void setMode (ModeSet& out, int index, double ratio, double amp, float t60) noexcept
{
    auto& m = out.modes[(size_t) index];
    m.ratio = (float) ratio;
    m.amp = (float) amp;
    m.t60 = t60;
}

//==============================================================================
void computeMembrane (const ModeParams& p, ModeSet& out) noexcept
{
    const auto& t = membraneTable();
    const auto decay = makeDecay (p);
    const double j01 = t.modes[0].j;
    const double j11 = t.firstZero[1];
    const double r = membraneRadius (p.position);
    const double s = p.shape;

    for (int i = 0; i < kMaxModes; ++i)
    {
        const auto& mode = t.modes[(size_t) i];

        // "Tuning" pulls each angular family towards the timpani series, where air
        // loading puts the (m,1) modes near 1 : 1.5 : 2 : 2.5 (relative to (1,1)).
        double factor = 1.0;
        if (mode.m >= 1)
            factor = std::lerp (1.0, j11 * (mode.m + 1) * 0.5 / t.firstZero[(size_t) mode.m], s);

        const double ratio = mode.j * factor / j01;
        double amp = besselJ (mode.m, mode.j * r) / std::max (1.0e-3, t.norm[(size_t) i]);
        amp *= std::pow (ratio, -0.8);

        float t60 = decay (ratio);
        if (mode.m == 0)
        {
            // Axisymmetric modes radiate strongly and die fast in a kettle drum.
            amp *= 1.0 - 0.85 * s;
            t60 *= (float) (1.0 - 0.75 * s);
        }
        setMode (out, i, ratio, amp, std::max (0.01f, t60));
    }
    out.count = kMaxModes;
}

// Free-free Euler-Bernoulli beam mode shape, written in a form that stays
// numerically stable for large beta (no catastrophic cosh - sinh cancellation).
double freeFreeShape (double beta, double x) noexcept
{
    const double bx = beta * x;
    const double sinhB = std::sinh (beta), sinB = std::sin (beta), cosB = std::cos (beta);
    const double denom = sinhB - sinB;
    const double sigma = (std::cosh (beta) - cosB) / denom;
    const double oneMinusSigma = (cosB - sinB - std::exp (-beta)) / denom;
    return std::cos (bx) - sigma * std::sin (bx) + std::exp (-bx) + oneMinusSigma * std::sinh (bx);
}

double barBeta (int k) noexcept
{
    static constexpr double exact[] = { 4.730040745, 7.853204624, 10.99560784, 14.13716549 };
    return k < 4 ? exact[k] : (2 * k + 3) * kPi * 0.5;
}

void computeWood (const ModeParams& p, ModeSet& out) noexcept
{
    constexpr int numModes = 24;
    const auto decay = makeDecay (p);
    const double b0 = barBeta (0);
    const double free2 = std::pow (barBeta (2) / b0, 2.0);
    const double s = p.shape;
    const double x = barX (p.position);

    for (int k = 0; k < numModes; ++k)
    {
        const double beta = barBeta (k);
        const double free = std::pow (beta / b0, 2.0);

        // Marimba bars are undercut so the first three modes land on 1 : 4 : 10.
        double ratio = free;
        if (k == 1)      ratio = std::lerp (free, 4.0, s);
        else if (k == 2) ratio = std::lerp (free, 10.0, s);
        else if (k > 2)  ratio = free * std::lerp (1.0, 10.0 / free2, s);

        const double amp = 0.5 * freeFreeShape (beta, x) * std::pow (ratio, -0.6);
        setMode (out, k, ratio, amp, decay (ratio));
    }
    out.count = numModes;
}

void computeMetal (const ModeParams& p, ModeSet& out) noexcept
{
    struct Candidate { double f; int m; int n; };
    constexpr int grid = 16;
    std::array<Candidate, grid * grid> candidates {};

    const double a = plateAspect (p.shape);
    for (int m = 1; m <= grid; ++m)
        for (int n = 1; n <= grid; ++n)
            candidates[(size_t) ((m - 1) * grid + (n - 1))] = { (m / a) * (m / a) + (double) (n * n), m, n };

    std::partial_sort (candidates.begin(), candidates.begin() + kMaxModes, candidates.end(),
                       [] (const Candidate& l, const Candidate& r) { return l.f < r.f; });

    const auto decay = makeDecay (p);
    const double f11 = candidates[0].f;
    const double x = plateX (p.position);
    const double y = plateY (p.position);

    for (int i = 0; i < kMaxModes; ++i)
    {
        const auto& c = candidates[(size_t) i];
        const double ratio = c.f / f11;
        const double amp = std::sin (c.m * kPi * x) * std::sin (c.n * kPi * y) * std::pow (ratio, -0.3);
        setMode (out, i, ratio, amp, decay (ratio));
    }
    out.count = kMaxModes;
}

void computeGlass (const ModeParams& p, ModeSet& out) noexcept
{
    const auto decay = makeDecay (p);
    auto ringFreq = [] (double n) { return n * (n * n - 1.0) / std::sqrt (n * n + 1.0); };
    const double f2 = ringFreq (2.0);
    const double stretch = 0.85 + 0.3 * p.shape;
    // Real glasses are never perfectly round: each mode splits into a beating pair.
    const double split = 0.002 + 0.006 * (1.0 - p.shape);

    int i = 0;
    for (int order = 2; i + 1 < kMaxModes; ++order)
    {
        const double ratio = std::pow (ringFreq (order) / f2, stretch);
        const double tilt = std::pow (ratio, -1.1 + 0.9 * p.position);   // low on the bowl = dark, rim = bright
        const double comb = 0.6 + 0.4 * std::cos (order * (1.0 - p.position) * 1.3);
        const double amp = tilt * comb;
        setMode (out, i++, ratio * (1.0 - 0.5 * split), amp * 0.8, decay (ratio));
        setMode (out, i++, ratio * (1.0 + 0.5 * split), amp * 0.6, decay (ratio));
    }
    out.count = i;
}

void computeBell (const ModeParams& p, ModeSet& out) noexcept
{
    // Partials relative to the "prime"; hum, prime, tierce, quint, nominal, ...
    static constexpr double partials[24] = { 0.500, 1.000, 1.183, 1.506, 2.000, 2.514, 2.662, 3.011,
                                             4.166, 4.844, 5.433, 6.059, 6.814, 7.556, 8.215, 9.140,
                                             10.12, 11.06, 12.30, 13.41, 14.73, 16.05, 17.40, 18.90 };
    static constexpr double levels[9] = { 0.55, 0.7, 0.9, 0.45, 1.0, 0.5, 0.6, 0.45, 0.5 };

    const auto decay = makeDecay (p);
    const double s = p.shape;
    int i = 0;
    for (int k = 0; k < 24; ++k)
    {
        double r = partials[k];
        if (k == 2)      r = std::lerp (1.183, 1.25, s);   // minor -> major third
        else if (k >= 5) r *= std::lerp (1.0, 1.05, s);

        const double ratio = r / partials[0];
        const double level = k < 9 ? levels[k] : 0.5 * std::pow (0.88, k - 8);
        const double amp = level * std::pow (ratio, (p.position - 0.5) * 0.8);  // waist = dark, lip = bright
        const double split = 0.0015 + 0.0007 * (k % 3);
        setMode (out, i++, ratio * (1.0 - 0.5 * split), amp * 0.75, decay (ratio));
        setMode (out, i++, ratio * (1.0 + 0.5 * split), amp * 0.55, decay (ratio));
    }
    out.count = i;
}

void computeString (const ModeParams& p, ModeSet& out) noexcept
{
    const auto decay = makeDecay (p);
    const double b = 0.004 * p.shape * p.shape;  // inharmonicity coefficient
    const double norm = std::sqrt (1.0 + b);
    const double x = stringX (p.position);

    for (int k = 0; k < kMaxModes; ++k)
    {
        const double n = k + 1.0;
        const double ratio = n * std::sqrt (1.0 + b * n * n) / norm;
        const double amp = std::sin (n * kPi * x) / std::pow (n, 0.8);
        setMode (out, k, ratio, amp, decay (ratio));
    }
    out.count = kMaxModes;
}

// Waveguide models: the harmonic series their delay loops produce. Used for the display,
// for the Body resonator and to shape the per-mode meters.
void computeTube (const ModeParams& p, ModeSet& out) noexcept
{
    const auto decay = makeDecay (p);
    const double open = p.shape;
    const double x = tubeX (p.position);
    int i = 0;
    for (int n = 1; n <= kMaxModes && i < kMaxModes; ++n)
    {
        // Stopped pipes only have odd harmonics; open pipes have all of them.
        const double weight = (n % 2 == 1) ? std::sqrt (1.0 - open) + std::sqrt (open) : std::sqrt (open);
        if (weight < 1.0e-3)
            continue;
        setMode (out, i++, (double) n, weight * std::sin (n * kPi * x) / n, decay ((double) n));
    }
    out.count = i;
}

void computeWire (const ModeParams& p, ModeSet& out) noexcept
{
    const auto decay = makeDecay (p);
    const double b = 0.0012 * p.shape * p.shape; // roughly what the dispersion filters give
    const double norm = std::sqrt (1.0 + b);
    const double x = wireX (p.position);
    for (int k = 0; k < kMaxModes; ++k)
    {
        const double n = k + 1.0;
        const double ratio = n * std::sqrt (1.0 + b * n * n) / norm;
        setMode (out, k, ratio, std::sin (n * kPi * x) / n, decay (ratio));
    }
    out.count = kMaxModes;
}

void normalise (ModeSet& out) noexcept
{
    double sumSq = 0.0;
    for (int i = 0; i < out.count; ++i)
        sumSq += (double) out.modes[(size_t) i].amp * out.modes[(size_t) i].amp;

    if (sumSq > 1.0e-12)
    {
        const float scale = (float) (1.0 / std::sqrt (sumSq));
        for (int i = 0; i < out.count; ++i)
            out.modes[(size_t) i].amp *= scale;
    }
}
} // namespace

//==============================================================================
const ModelInfo& getModelInfo (Model model) noexcept
{
    return kModelInfos[std::clamp ((int) model, 0, kNumModels - 1)];
}

float modeDecaySeconds (const ModeParams& params, double ratio) noexcept
{
    return makeDecay (params) (ratio);
}

void initModeTables()
{
    (void) membraneTable();
}

void computeModes (const ModeParams& params, ModeSet& out) noexcept
{
    for (auto& m : out.modes)
        m = {};
    out.count = 0;

    switch (params.model)
    {
        case Model::Membrane: computeMembrane (params, out); break;
        case Model::Wood:     computeWood (params, out);     break;
        case Model::Metal:    computeMetal (params, out);    break;
        case Model::Glass:    computeGlass (params, out);    break;
        case Model::Bell:     computeBell (params, out);     break;
        case Model::String:   computeString (params, out);   break;
        case Model::Tube:     computeTube (params, out);     break;
        case Model::Wire:     computeWire (params, out);     break;
    }
    normalise (out);
}
} // namespace mp
