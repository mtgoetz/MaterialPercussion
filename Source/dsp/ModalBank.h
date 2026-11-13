#pragma once

#include "ModeModels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mp
{
// A bank of two-pole resonators, one per mode, with a contact point (the strike position)
// through which exciters, the body spring and the rattle push on the object and read its
// motion back.
//
// For the Tension control it can also report its vibrational energy and move energy from
// each mode into slightly higher ones (cascade). This is how gongs and cymbals bloom into a
// crash after a hard hit. The transfer only ever moves energy that is already there, so it
// cannot run away.
template <int MaxModes>
class ModalBank
{
public:
    void reset() noexcept
    {
        std::fill (std::begin (y1), std::end (y1), 0.0);
        std::fill (std::begin (y2), std::end (y2), 0.0);
        uContact = vContact = 0.0;
        clearPeaks();
    }

    // Recomputes coefficients, keeping the vibration state.
    void configure (const ModeSet& modes, double baseFreq, double sampleRate, float width, double freqMul) noexcept
    {
        constexpr double twoPi = 6.283185307179586;
        const double nyquistLimit = 0.45 * sampleRate;
        count = std::min (modes.count, MaxModes);

        s0 = std::clamp (std::sin (twoPi * baseFreq * freqMul / sampleRate), 1.0e-4, 1.0);
        double admit = 0.0, noiseVar = 0.0;

        for (int m = 0; m < MaxModes; ++m)
        {
            const Mode* mode = m < count ? &modes.modes[(size_t) m] : nullptr;
            const double f = mode != nullptr ? baseFreq * mode->ratio * freqMul : 0.0;

            if (mode == nullptr || f >= nyquistLimit || f < 10.0 || mode->amp == 0.0f)
            {
                b1[m] = b2[m] = g[m] = ca[m] = 0.0;
                sinT[m] = 1.0;
                cosT[m] = 0.0;
                y1[m] = y2[m] = 0.0;
                continue;
            }

            const double theta = twoPi * f / sampleRate;
            sinT[m] = std::sin (theta);
            cosT[m] = std::cos (theta);
            const double r = std::exp (-6.907755278982137 / (std::max (0.005, (double) mode->t60) * sampleRate));
            b1[m] = 2.0 * r * cosT[m];
            b2[m] = -r * r;
            ca[m] = mode->amp;
            g[m] = mode->amp * sinT[m]; // unit-amplitude impulse response

            admit += ca[m] * g[m];
            const double r2 = r * r;
            const double den = (1.0 - r2) * ((1.0 + r2) * (1.0 + r2) - 4.0 * r2 * cosT[m] * cosT[m]);
            noiseVar += g[m] * g[m] * (1.0 + r2) / std::max (1.0e-12, den);

            const float side = m == 0 ? 0.0f : ((m & 1) ? 1.0f : -1.0f) * width * std::min (1.0f, m / 4.0f);
            panL[m] = 1.0f - 0.5f * side;
            panR[m] = 1.0f + 0.5f * side;
        }

        admittance = std::max (1.0e-9, admit / s0);
        noiseGain = std::sqrt (noiseVar);
    }

    // e: exciter force (an impulse makes each mode ring at its normalised amplitude)
    // f: contact force (body spring, rattle), applied with reciprocal weights
    void tick (double e, double f, double& outL, double& outR) noexcept
    {
        double l = 0.0, r = 0.0, u = 0.0, v = 0.0;
        for (int m = 0; m < count; ++m)
        {
            const double y = b1[m] * y1[m] + b2[m] * y2[m] + g[m] * e + ca[m] * f;
            u += ca[m] * y;
            v += ca[m] * (y - y1[m]);
            y2[m] = y1[m];
            y1[m] = y;
            l += y * panL[m];
            r += y * panR[m];

            const float a = (float) std::abs (y);
            if (a > peaks[m])
                peaks[m] = a;
        }

        uContact = u;
        vContact = v / s0;
        outL = l;
        outR = r;
    }

    // Sum of squared mode amplitudes (about 1 just after a full, hard hit).
    double energy() const noexcept
    {
        double e = 0.0;
        for (int m = 0; m < count; ++m)
            e += modeEnergy (m);
        return e;
    }

    // Moves `fraction` of each mode's energy into one of the next few modes up, with a
    // random phase.
    void cascade (double fraction, std::uint32_t& rng) noexcept
    {
        if (fraction <= 0.0 || count < 2)
            return;
        fraction = std::min (fraction, 0.5);
        const double keep = std::sqrt (1.0 - fraction);

        for (int m = count - 2; m >= 0; --m) // top down, so energy moves one step per call
        {
            if (b1[m] == 0.0)
                continue;
            rng = rng * 1664525u + 1013904223u;
            const int target = std::min (count - 1, m + 1 + (int) ((rng >> 16) % 4u));
            if (b1[target] == 0.0)
                continue;

            const double moved = modeEnergy (m) * fraction;
            y1[m] *= keep;
            y2[m] *= keep;

            const double kick = std::sqrt (moved) * sinT[target];
            const double sign = (rng & 0x8000u) ? 1.0 : -1.0;
            y1[target] += sign * kick;
        }
    }

    double contactDisplacement() const noexcept { return uContact; }
    double contactVelocity() const noexcept { return vContact; }
    double getAdmittance() const noexcept { return admittance; }
    double getNoiseGain() const noexcept { return noiseGain; }
    double getSinTheta0() const noexcept { return s0; }

    float getPeak (int m) const noexcept { return peaks[m]; }
    void clearPeaks() noexcept { std::fill (std::begin (peaks), std::end (peaks), 0.0f); }

    // Emergency brake if something ever runs away (should not happen).
    bool isBlownUp() const noexcept { return ! std::isfinite (uContact) || std::abs (uContact) > 1.0e3; }

private:
    double modeEnergy (int m) const noexcept
    {
        // Squared amplitude of a sinusoid from two consecutive samples.
        const double num = y1[m] * y1[m] + y2[m] * y2[m] - 2.0 * cosT[m] * y1[m] * y2[m];
        return num / (sinT[m] * sinT[m]);
    }

    int count = 0;
    double y1[MaxModes] {}, y2[MaxModes] {};
    double b1[MaxModes] {}, b2[MaxModes] {}, g[MaxModes] {}, ca[MaxModes] {};
    double sinT[MaxModes] {}, cosT[MaxModes] {};
    float panL[MaxModes] {}, panR[MaxModes] {}, peaks[MaxModes] {};

    double s0 = 0.01, admittance = 1.0, noiseGain = 1.0;
    double uContact = 0.0, vContact = 0.0;
};
} // namespace mp
