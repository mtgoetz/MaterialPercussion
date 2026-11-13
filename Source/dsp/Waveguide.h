#pragma once

#include "ModeModels.h"

#include <vector>

namespace mp
{
// Digital waveguide resonator: velocity waves circulating in a delay-line loop with a
// fitted loss filter (decay + damping), optional dispersion (stiffness) and a DC blocker.
//  - Wire: one loop; Shape adds allpass dispersion so upper partials go sharp.
//  - Tube: a stopped loop (inverting, odd harmonics) cross-faded with an open loop (all
//    harmonics); Shape is the bore.
// The excitation goes in through a comb set by the strike position. The output is the
// integrated (displacement) signal, so partials fall off like a real struck string or pipe.
class Waveguide
{
public:
    void prepare (double sampleRate);  // allocates
    void reset() noexcept;

    void configure (const ModeParams& params, double baseFreq, float width) noexcept;
    void setFrequencyScale (double scale) noexcept; // pitch drop / tension, around 1

    // e: exciter force, f: contact force (same meaning as ModalBank::tick)
    void tick (double e, double f, double& outL, double& outR) noexcept;

    double contactDisplacement() const noexcept { return u; }
    double contactVelocity() const noexcept { return v; }
    double getAdmittance() const noexcept { return kin; }
    double getNoiseGain() const noexcept { return noiseRms; }
    double getSinTheta0() const noexcept { return s0; }
    bool isBlownUp() const noexcept;

private:
    struct Loop
    {
        std::vector<float> buf;
        int mask = 0, writePos = 0;
        double length = 100.0;  // round-trip samples at the nominal pitch
        double comp = 0.0;      // phase delay of the loop filters at the fundamental
        double delay = 100.0;   // current read delay
        double g = 0.99, a = 0.0, lp = 0.0;
        double sign = 1.0;
        bool dcBlock = true;
        double dcX = 0.0, dcY = 0.0;
        int numAllpass = 0;
        double apC = 0.0, apX[4] {}, apY[4] {};
        int combTap = 1, stereoTap = 1;
        double weight = 1.0;
        double x = 0.0, xStereo = 0.0;
    };

    void setupLoop (Loop& loop, const ModeParams& p, double f0, double length, double sign,
                    bool dispersive, double strikeX) noexcept;
    void process (Loop& loop, double in) noexcept;

    double sampleRate = 44100.0;
    Loop loops[2];
    int numLoops = 1;

    std::vector<float> history; // input, for the strike-position comb
    int historyMask = 0, historyPos = 0;

    double kin = 1.0, s0 = 0.01, noiseRms = 1.0;
    double u = 0.0, uStereo = 0.0, v = 0.0, leak = 0.999, dcR = 0.999;
    float width = 0.0f;
};
} // namespace mp
