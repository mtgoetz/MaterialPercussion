#include "Exciter.h"

#include <algorithm>
#include <cmath>

namespace mp
{
namespace
{
constexpr double kTwoPi = 6.283185307179586;

const ExciterInfo kExciterInfos[kNumExciters] = {
    { "Mallet",    "Strike it once: felt, rubber, wood or metal beater",
      "Hardness",  "Mallet: soft felt to hard stick - sets which modes get excited",
      "Colour: has no effect on the mallet (use Hardness)", false },
    { "Bow",       "Bow it: friction grabs and releases the surface (singing bowls, bowed metal)",
      "Pressure",  "Bow pressure: light and airy (left) to heavy and gritty (right)",
      "Colour: darkens or brightens the bow, changing which modes it locks on to", true },
    { "Scrape",    "Drag something across it: hiss, rub, scrape, guiro ticks",
      "Roughness", "Surface: smooth rub (left) to coarse ticking scrape (right)",
      "Colour: dark rumble (left) to thin scratch (right)", true },
    { "Particles", "Shake grains against it: shakers, rain, sand, brushes",
      "Density",   "Grains per second: a few pebbles (left) to a dense hiss of sand (right)",
      "Colour: grain size - soft pebbles (left) to sharp sand (right)", true },
    { "Feedback",  "Feed its own sound back in: howls, drones, e-bow sustain",
      "Drive",     "Feedback gain: gentle sustain (left) to screaming overdrive (right)",
      "Colour: picks which modes take over - low drone (left) to high whistle (right)", true },
};

// Which mode-picking filter the Colour knob selects. bypass when near the middle.
struct ColorShape { bool bypass; bool highPass; double cutoff; };

ColorShape colorShape (float color) noexcept
{
    if (color < 0.47f) return { false, false, 150.0 * std::pow (20000.0 / 150.0, color / 0.47) };
    if (color > 0.53f) return { false, true, 20.0 * std::pow (150.0, (color - 0.53) / 0.47) };
    return { true, false, 0.0 };
}

double onePoleMag (const ColorShape& c, double hz) noexcept
{
    if (c.bypass) return 1.0;
    const double r = hz / c.cutoff;
    return c.highPass ? r / std::sqrt (1.0 + r * r) : 1.0 / std::sqrt (1.0 + r * r);
}

// Friction curve (after the STK bow table): grips at small slip velocities, lets go at large.
constexpr double kBowSpeed = 0.3;       // bow velocity at full key velocity (object velocity units)
constexpr double kBowSlipScale = 1.0;   // how quickly the grip falls off with slip velocity
} // namespace

const ExciterInfo& getExciterInfo (ExciterType type) noexcept
{
    return kExciterInfos[std::clamp ((int) type, 0, kNumExciters - 1)];
}

float exciterSpectrum (ExciterType type, float hz, float hardness, float color) noexcept
{
    switch (type)
    {
        case ExciterType::Mallet:    return strikeSpectrum (hz, strikeWidthSeconds (hardness));
        case ExciterType::Particles: return strikeSpectrum (hz, strikeWidthSeconds (color));
        case ExciterType::Bow:
        case ExciterType::Scrape:
        case ExciterType::Feedback:  break;
    }
    return (float) onePoleMag (colorShape (color), hz);
}

//==============================================================================
void Exciter::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    noiseDecay = std::exp (-1.0 / (0.012 * sampleRate));
    dcCoef = 1.0 - kTwoPi * 10.0 / sampleRate;
    reset();
}

void Exciter::reset() noexcept
{
    pulseLength = pulsePos = 0;
    bouncesLeft = 0;
    noiseLevel = 0.0;
    stage = Stage::Idle;
    gate = false;
    env = 0.0;
    colorState = 0.0;
    grainLength = grainPos = 0;
    fbDcX = fbDcY = 0.0;
}

void Exciter::update (const VoiceSettings& s) noexcept
{
    hardness = s.hardness;
    noise = s.noise;

    const auto c = colorShape (s.color);
    colorBypass = c.bypass;
    colorHighPass = c.highPass;
    colorCoef = c.bypass ? 1.0 : 1.0 - std::exp (-kTwoPi * std::min (c.cutoff, 0.45 * sampleRate) / sampleRate);
    malletHardness = s.color; // particles use Colour as grain hardness
}

void Exciter::trigger (float newVelocity, const VoiceSettings& s) noexcept
{
    update (s);
    type = s.exciter;
    velocity = std::clamp (newVelocity, 0.0f, 1.0f);

    if (type == ExciterType::Mallet)
    {
        // Harder hits flatten the mallet more, so velocity also brightens the strike.
        const float h = std::clamp (s.hardness + 0.3f * (velocity - 0.75f), 0.0f, 1.0f);
        startPulse (h, velocity);
        malletHardness = h;

        bouncesLeft = (int) std::lround (s.bounce * 10.0f);
        bounceInterval = (0.03 + 0.11 * s.bounce) * sampleRate;
        bounceTimer = bounceInterval;
        bounceAmp = velocity;
        noiseLevel = s.noise * velocity * 0.08;
        return;
    }

    // Continuous exciters: (re)start the envelope from wherever it is now.
    const double len = std::max (0.02, (double) s.length);
    const double attack = std::clamp (0.12 * len, 0.003, 0.25);
    attackInc = 1.0 / (attack * sampleRate);
    holdSamples = 0.65 * len * sampleRate;
    releaseCoef = std::exp (-4.6 / (std::max (0.01, 0.35 * len) * sampleRate));
    elapsed = 0.0;
    gate = true;
    stage = Stage::Attack;
    noiseLevel = 0.0;

    if (type == ExciterType::Feedback)
        startPulse (0.6f, 0.15f * velocity); // a small tap so there is something to feed back
}

void Exciter::startPulse (float h, float amplitude) noexcept
{
    pulseLength = std::max (2, (int) std::lround (strikeWidthSeconds (h) * sampleRate));
    pulsePos = 0;
    pulseScale = amplitude / (double) pulseLength; // unit-area pulse scaled by amplitude
}

double Exciter::nextPulse() noexcept
{
    if (pulsePos >= pulseLength)
        return 0.0;
    const double phase = kTwoPi * (pulsePos + 0.5) / pulseLength;
    ++pulsePos;
    return pulseScale * (1.0 - std::cos (phase));
}

double Exciter::white() noexcept
{
    rng = rng * 1664525u + 1013904223u;
    return ((double) (rng >> 8) * (1.0 / 8388608.0) - 1.0) * 1.7320508; // unit variance
}

double Exciter::uniform01() noexcept
{
    rng = rng * 1664525u + 1013904223u;
    return ((double) (rng >> 8) + 1.0) * (1.0 / 16777216.0);
}

double Exciter::colorFilter (double x) noexcept
{
    if (colorBypass)
        return x;
    colorState += colorCoef * (x - colorState);
    return colorHighPass ? x - colorState : colorState;
}

void Exciter::advanceEnvelope() noexcept
{
    switch (stage)
    {
        case Stage::Idle:
            return;
        case Stage::Attack:
            env += attackInc;
            if (env >= 1.0) { env = 1.0; stage = Stage::Hold; }
            break;
        case Stage::Hold:
            if (! gate && elapsed >= holdSamples)
                stage = Stage::Release;
            break;
        case Stage::Release:
            env *= releaseCoef;
            if (env < 1.0e-4) { env = 0.0; stage = Stage::Idle; }
            break;
    }
    elapsed += 1.0;
}

bool Exciter::isActive() const noexcept
{
    return pulsePos < pulseLength || bouncesLeft > 0 || noiseLevel > 1.0e-6 || stage != Stage::Idle
        || grainPos < grainLength;
}

double Exciter::tick (const ExciterContext& ctx) noexcept
{
    double e = nextPulse();

    if (type == ExciterType::Mallet)
    {
        if (bouncesLeft > 0 && (bounceTimer -= 1.0) <= 0.0)
        {
            bounceAmp *= 0.62f;
            bounceInterval = std::max (bounceInterval * 0.78, (double) pulseLength + 1.0);
            bounceTimer = bounceInterval;
            startPulse (malletHardness, bounceAmp);
            --bouncesLeft;
        }
        if (noiseLevel > 1.0e-6)
        {
            e += white() * noiseLevel;
            noiseLevel *= noiseDecay;
        }
        return e;
    }

    advanceEnvelope();
    const double level = velocity * env;
    const double noiseNorm = 1.0 / std::max (1.0e-9, ctx.noiseGain);
    const double admit = std::max (1.0e-9, ctx.admittance);

    switch (type)
    {
        case ExciterType::Bow:
        {
            const double bowSpeed = kBowSpeed * level;
            const double slip = bowSpeed - ctx.velocity;
            const double slope = (3.2 - 2.7 * hardness) * kBowSlipScale;
            const double grip = std::min (1.0, std::pow (std::abs (slip * slope) + 0.75, -4.0));
            e += colorFilter (slip * grip * (0.4 + 0.6 * hardness)) / admit;
            e += noise * level * 0.25 * white() * noiseNorm;
            break;
        }

        case ExciterType::Scrape:
        {
            // Sparse ticks (unit variance) blended with smooth hiss as the surface gets rougher.
            const double rate = 60.0 * std::pow (80.0, (double) hardness);
            spikeValue = 0.0;
            if ((spikeTimer -= 1.0) <= 0.0)
            {
                spikeTimer = -std::log (uniform01()) * sampleRate / rate;
                spikeValue = (white() > 0.0 ? 1.0 : -1.0) * std::sqrt (sampleRate / rate) * (0.5 + uniform01());
            }
            const double x = (1.0 - hardness) * white() + hardness * spikeValue;
            e += 0.35 * level * colorFilter (x + noise * white()) * noiseNorm;
            break;
        }

        case ExciterType::Particles:
        {
            const double rate = 3.0 * std::pow (800.0, (double) hardness) * (0.3 + 0.7 * env);
            if (env > 0.0 && (grainTimer -= 1.0) <= 0.0)
            {
                grainTimer = -std::log (uniform01()) * sampleRate / rate;
                const double target = 0.35 * level * noiseNorm / std::sqrt (rate / sampleRate);
                const double amp = std::min ((double) velocity, target) * (0.3 + 0.7 * uniform01());
                grainLength = std::max (2, (int) std::lround (strikeWidthSeconds (malletHardness) * sampleRate));
                grainPos = 0;
                grainScale = (white() > 0.0 ? amp : -amp) / grainLength;
            }
            if (grainPos < grainLength)
            {
                e += grainScale * (1.0 - std::cos (kTwoPi * (grainPos + 0.5) / grainLength));
                ++grainPos;
            }
            e += noise * level * 0.15 * white() * noiseNorm;
            break;
        }

        case ExciterType::Feedback:
        {
            // DC-blocked contact velocity, amplified and fed back. Gain control on the output
            // level makes the howl settle at a level set by velocity instead of growing
            // without bound.
            const double v = ctx.velocity;
            fbDcY = v - fbDcX + dcCoef * fbDcY;
            fbDcX = v;

            const double target = 0.5 * level + 1.0e-9;
            const double drive = 0.02 * std::pow (40.0, (double) hardness);
            const double over = std::max (1.0, ctx.outputLevel / target);
            const double over2 = over * over, over8 = over2 * over2 * over2 * over2;
            const double x = target * std::tanh (drive * fbDcY / (target * over8));
            e += colorFilter (x) / admit;
            e += noise * level * 0.1 * white() * noiseNorm;
            break;
        }

        case ExciterType::Mallet:
            break;
    }
    return e;
}
} // namespace mp
