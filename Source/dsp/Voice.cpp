#include "Voice.h"

#include <algorithm>
#include <cmath>

namespace mp
{
namespace
{
constexpr int kControlInterval = 16;     // samples between pitch/coefficient updates
constexpr float kSilenceLevel = 1.0e-5f; // ~ -100 dB
constexpr double kMaxDropOctaves = 0.75;

// Tuning of the nonlinear and coupling sections
constexpr double kTensionGlide = 1.0;      // pitch rise per unit Tension x energy (1 = full hit)
constexpr double kTensionCascade = 60.0;   // share of energy moved up per second per unit Tension x energy
constexpr double kCoupleStiffness = 0.8;   // body spring, relative to the object's fundamental
constexpr double kCoupleTransmit = 0.25;   // share of the impact that goes straight into the body
constexpr double kRattleStiffness = 6.0;   // contact stop, relative to the fundamental
constexpr double kRattleClick = 1.2;       // level of the contact clicks
constexpr double kRattleNoise = 0.5;       // level of the sizzle

double rattleGap (float gap) noexcept { return 0.02 * std::pow (40.0, (double) gap); }
} // namespace

void Voice::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    exciter.prepare (sampleRate);
    waveguide.prepare (sampleRate);
    dropDecay = std::exp (-1.0 / (0.12 * sampleRate));
    energyCoef = 1.0 - std::exp (-1.0 / (0.012 * sampleRate));
    buzzDecay = std::exp (-1.0 / (0.003 * sampleRate));
    clickCoef = 1.0 - std::exp (-6.283185307179586 * 300.0 / sampleRate);
    levelRelease = std::exp (-1.0 / (0.05 * sampleRate));
    reset();
}

void Voice::reset() noexcept
{
    silence();
    clearPeaks();
    level = 0.0f;
}

void Voice::silence() noexcept
{
    active = false;
    exciter.reset();
    modal.reset();
    body.reset();
    waveguide.reset();
    dropEnv = 0.0;
    dropWasActive = false;
    energy = outputLevel = 0.0;
    rattleExcess = buzzEnv = rattleHp = rattleLp = 0.0;
    lastVersion = lastBodyVersion = -1;
}

void Voice::clearPeaks() noexcept
{
    modal.clearPeaks();
    body.clearPeaks();
    std::fill (std::begin (waveguidePeaks), std::end (waveguidePeaks), 0.0f);
}

float Voice::getModePeak (int index) const noexcept
{
    return useWaveguide ? waveguidePeaks[index] : modal.getPeak (index);
}

void Voice::strike (int newNote, float velocity, const RenderContext& ctx) noexcept
{
    if (! active || newNote != note)
        silence();

    note = newNote;
    active = true;
    lastVersion = lastBodyVersion = -1; // force a coefficient refresh

    exciter.setSample (ctx.sample);
    exciter.trigger (velocity, ctx.settings);
    dropEnv = std::min (1.0, dropEnv + std::clamp ((double) velocity, 0.0, 1.0));
}

void Voice::release (int releasedNote) noexcept
{
    if (active && releasedNote == note)
        exciter.release();
}

void Voice::render (float* left, float* right, int numSamples, const RenderContext& ctx) noexcept
{
    if (! active)
        return;

    const auto& s = ctx.settings;
    const double baseFreq = s.keyTrack ? s.pitchHz * std::exp2 ((note - 60) / 12.0) : s.pitchHz;
    const double bodyFreq = baseFreq * std::exp2 (s.bodyTune / 12.0);
    const bool bodyOn = s.bodyOn && ctx.bodyModes != nullptr;

    const bool wantWaveguide = isWaveguide (ctx.params.model);
    if (wantWaveguide != useWaveguide)
    {
        modal.reset();
        waveguide.reset();
        useWaveguide = wantWaveguide;
        lastVersion = -1;
    }
    if (bodyOn != bodyWasOn)
    {
        body.reset();
        bodyWasOn = bodyOn;
        lastBodyVersion = -1;
    }

    exciter.update (s);
    exciter.setSample (ctx.sample);
    const double blend = bodyOn ? s.bodyBlend : 0.0;
    const double couple = bodyOn ? s.bodyCouple : 0.0;
    const double gap = rattleGap (s.gap);
    const double rattle = s.rattle;
    float peak = 0.0f;

    for (int start = 0; start < numSamples; start += kControlInterval)
    {
        const int n = std::min (kControlInterval, numSamples - start);
        const bool dropActive = s.drop * dropEnv > 1.0e-4;
        const double dropMul = std::exp2 (s.drop * dropEnv * kMaxDropOctaves);
        const bool changed = ctx.modesVersion != lastVersion || baseFreq != lastBaseFreq || s.width != lastWidth;
        const bool tensionActive = s.tension > 0.0f;

        if (useWaveguide)
        {
            if (changed)
                waveguide.configure (ctx.params, baseFreq, s.width);
            waveguide.setFrequencyScale (dropMul * (1.0 + s.tension * kTensionGlide * std::min (1.5, 2.0 * energy)));
        }
        else
        {
            double glide = 1.0;
            if (tensionActive)
            {
                // Louder = stiffer = sharper; pitch falls back as the energy drains away,
                // while some of that energy cascades up into higher modes.
                const double e = std::min (4.0, modal.energy());
                glide = 1.0 + s.tension * kTensionGlide * e;
                modal.cascade (s.tension * kTensionCascade * e * n / sampleRate, rng);
            }
            if (changed || dropActive || dropWasActive || tensionActive)
                modal.configure (*ctx.modes, baseFreq, sampleRate, s.width, dropMul * glide);
        }
        lastVersion = ctx.modesVersion;
        lastBaseFreq = baseFreq;
        dropWasActive = dropActive;

        if (bodyOn && (ctx.bodyVersion != lastBodyVersion || bodyFreq != lastBodyFreq || s.width != lastWidth))
        {
            body.configure (*ctx.bodyModes, bodyFreq, sampleRate, s.width, 1.0);
            lastBodyVersion = ctx.bodyVersion;
            lastBodyFreq = bodyFreq;
        }
        lastWidth = s.width;

        // Per-block constants that depend on the object's current tuning
        const double s0 = useWaveguide ? waveguide.getSinTheta0() : modal.getSinTheta0();
        const double stopStiffness = rattle * std::min (0.05, kRattleStiffness * s0 * s0);
        const double springStiffness = couple * std::min (0.05, kCoupleStiffness * s0 * s0);
        const double mainAdmit = useWaveguide ? waveguide.getAdmittance() : modal.getAdmittance();
        const double mainNoise = useWaveguide ? waveguide.getNoiseGain() : modal.getNoiseGain();

        ExciterContext ectx;
        ectx.admittance = (1.0 - blend) * (1.0 - blend) * mainAdmit + (bodyOn ? blend * blend * body.getAdmittance() : 0.0);
        ectx.noiseGain = std::sqrt ((1.0 - blend) * (1.0 - blend) * mainNoise * mainNoise
                                    + (bodyOn ? blend * blend * body.getNoiseGain() * body.getNoiseGain() : 0.0));

        for (int k = 0; k < n; ++k)
        {
            const double uMain = useWaveguide ? waveguide.contactDisplacement() : modal.contactDisplacement();
            const double vMain = useWaveguide ? waveguide.contactVelocity() : modal.contactVelocity();
            ectx.velocity = (1.0 - blend) * vMain + (bodyOn ? blend * body.contactVelocity() : 0.0);
            ectx.outputLevel = outputLevel;

            const double e = exciter.tick (ectx);

            // Rattle: a loose part the object slaps against once it moves past the gap.
            double contactForce = 0.0, rattleOut = 0.0;
            if (rattle > 0.0)
            {
                const double excess = uMain - std::clamp (uMain, -gap, gap);
                const double slap = std::min (2.0, std::abs (excess - rattleExcess) / s0); // penetration speed
                rattleExcess = excess;
                contactForce -= stopStiffness * excess;

                // The clipped part of the motion, high-passed, is the buzz itself...
                const double click = excess - rattleLp;
                rattleLp += clickCoef * (excess - rattleLp);

                // ...and every slap also shakes loose a little burst of sizzle.
                buzzEnv = std::max (slap, buzzEnv * buzzDecay);
                rng = rng * 1664525u + 1013904223u;
                const double white = (double) (rng >> 8) * (1.0 / 8388608.0) - 1.0;
                const double hp = white - rattleHp;
                rattleHp += 0.3 * (white - rattleHp);
                rattleOut = rattle * (kRattleClick * click + kRattleNoise * buzzEnv * hp);
            }

            // Body: a spring between the two contact points, plus part of the impact.
            double bodyL = 0.0, bodyR = 0.0;
            if (bodyOn)
            {
                const double spring = springStiffness * (uMain - body.contactDisplacement());
                // The reaction on a delay-line loop is not passive (it can pump energy round the
                // loop), so a waveguide object drives its body one way, as if it were heavy.
                if (! useWaveguide)
                    contactForce -= spring;
                body.tick (e * (blend + couple * kCoupleTransmit * (1.0 - blend)), spring, bodyL, bodyR);
            }

            double l = 0.0, r = 0.0;
            if (useWaveguide)
                waveguide.tick ((1.0 - blend) * e, contactForce, l, r);
            else
                modal.tick ((1.0 - blend) * e, contactForce, l, r);

            const double direct = exciter.getDirect(); // recorded attack, heard as is
            const float outL = (float) (((l + direct) * s.gain + bodyL * s.bodyGain) + rattleOut);
            const float outR = (float) (((r + direct) * s.gain + bodyR * s.bodyGain) + rattleOut);
            left[start + k] += outL;
            right[start + k] += outR;
            const float a = std::max (std::abs (outL), std::abs (outR));
            peak = std::max (peak, a);
            outputLevel = std::max ((double) a, outputLevel * levelRelease);

            energy += energyCoef * (uMain * uMain - energy);
            dropEnv *= dropDecay;
        }
    }

    level = peak;

    if (useWaveguide && ctx.modes != nullptr)
        for (int m = 0; m < ctx.modes->count; ++m)
            waveguidePeaks[m] = std::max (waveguidePeaks[m], peak * std::abs (ctx.modes->modes[(size_t) m].amp));

    const bool blownUp = useWaveguide ? waveguide.isBlownUp() : modal.isBlownUp();
    if (blownUp || body.isBlownUp() || ! std::isfinite (peak))
    {
        silence();
        return;
    }

    if (! exciter.isActive() && peak < kSilenceLevel)
        silence();
}
} // namespace mp
