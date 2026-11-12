#include "ModalVoice.h"

#include <algorithm>
#include <cmath>

namespace mp
{
namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr int kControlInterval = 16;     // samples between coefficient updates while pitch glides
constexpr float kSilenceLevel = 1.0e-5f; // ~ -100 dB
constexpr float kMaxDropOctaves = 0.75f;
} // namespace

void ModalVoice::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    noiseDecay = (float) std::exp (-1.0 / (0.012 * sampleRate));
    dropDecay = (float) std::exp (-1.0 / (0.12 * sampleRate));
    reset();
}

void ModalVoice::reset() noexcept
{
    silence();
    clearModePeaks();
    level = 0.0f;
}

void ModalVoice::silence() noexcept
{
    active = false;
    std::fill (std::begin (y1), std::end (y1), 0.0);
    std::fill (std::begin (y2), std::end (y2), 0.0);
    pulsePos = pulseLength = 0;
    noiseLevel = 0.0f;
    dropEnv = 0.0f;
    dropWasActive = false;
    lastVersion = -1;
}

void ModalVoice::clearModePeaks() noexcept
{
    std::fill (std::begin (modePeaks), std::end (modePeaks), 0.0f);
}

void ModalVoice::strike (int newNote, float velocity, const VoiceSettings& s) noexcept
{
    if (! active || newNote != note)
        silence();

    note = newNote;
    active = true;
    lastVersion = -1; // force a coefficient refresh

    const float vel = std::clamp (velocity, 0.0f, 1.0f);

    // Harder hits flatten the mallet more, so velocity also brightens the strike.
    const float hardness = std::clamp (s.hardness + 0.3f * (vel - 0.75f), 0.0f, 1.0f);
    pulseLength = std::max (2, (int) std::lround (strikeWidthSeconds (hardness) * sampleRate));
    pulsePos = 0;
    pulseScale = vel / (float) pulseLength; // unit-area pulse scaled by velocity

    noiseLevel = s.noise * vel * 0.08f;
    dropEnv = std::min (1.0f, dropEnv + vel);
}

float ModalVoice::nextExcitation() noexcept
{
    float e = 0.0f;

    if (pulsePos < pulseLength)
    {
        const double phase = kTwoPi * (pulsePos + 0.5) / pulseLength;
        e += pulseScale * (float) (1.0 - std::cos (phase));
        ++pulsePos;
    }

    if (noiseLevel > 1.0e-6f)
    {
        rng = rng * 1664525u + 1013904223u;
        const float white = (float) (rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
        e += white * noiseLevel;
        noiseLevel *= noiseDecay;
    }

    return e;
}

void ModalVoice::updateCoefficients (const ModeSet& modes, float baseFreq, float width, float freqMul) noexcept
{
    const double nyquistLimit = 0.45 * sampleRate;

    for (int m = 0; m < kMaxModes; ++m)
    {
        if (m >= modes.count)
        {
            b1[m] = b2[m] = inGain[m] = 0.0;
            y1[m] = y2[m] = 0.0;
            continue;
        }

        const auto& mode = modes.modes[(size_t) m];
        const double f = (double) baseFreq * mode.ratio * freqMul;

        if (f >= nyquistLimit || f < 10.0)
        {
            b1[m] = b2[m] = inGain[m] = 0.0;
            y1[m] = y2[m] = 0.0;
            continue;
        }

        const double theta = kTwoPi * f / sampleRate;
        const double r = std::exp (-6.907755278982137 / (std::max (0.005, (double) mode.t60) * sampleRate));
        b1[m] = 2.0 * r * std::cos (theta);
        b2[m] = -r * r;
        inGain[m] = mode.amp * std::sin (theta); // unit-amplitude impulse response

        // Spread upper modes alternately left/right; keep the fundamental centred.
        const float side = m == 0 ? 0.0f : ((m & 1) ? 1.0f : -1.0f) * width * std::min (1.0f, m / 4.0f);
        panL[m] = 1.0f - 0.5f * side;
        panR[m] = 1.0f + 0.5f * side;
    }
}

void ModalVoice::render (float* left, float* right, int numSamples,
                         const ModeSet& modes, int modeSetVersion, const VoiceSettings& s) noexcept
{
    if (! active)
        return;

    const float baseFreq = s.keyTrack ? s.pitchHz * std::exp2 ((float) (note - 60) / 12.0f) : s.pitchHz;
    const int count = modes.count;
    float peak = 0.0f;

    for (int start = 0; start < numSamples; start += kControlInterval)
    {
        const int n = std::min (kControlInterval, numSamples - start);
        const bool dropActive = s.drop * dropEnv > 1.0e-4f;

        if (modeSetVersion != lastVersion || baseFreq != lastBaseFreq || s.width != lastWidth
            || dropActive || dropWasActive)
        {
            updateCoefficients (modes, baseFreq, s.width, std::exp2 (s.drop * dropEnv * kMaxDropOctaves));
            lastVersion = modeSetVersion;
            lastBaseFreq = baseFreq;
            lastWidth = s.width;
        }
        dropWasActive = dropActive;

        for (int k = 0; k < n; ++k)
        {
            const double e = nextExcitation();
            double l = 0.0, r = 0.0;

            for (int m = 0; m < count; ++m)
            {
                const double y = b1[m] * y1[m] + b2[m] * y2[m] + inGain[m] * e;
                y2[m] = y1[m];
                y1[m] = y;
                l += y * panL[m];
                r += y * panR[m];

                const float a = (float) std::abs (y);
                if (a > modePeaks[m])
                    modePeaks[m] = a;
            }

            const float outL = (float) l * s.gain;
            const float outR = (float) r * s.gain;
            left[start + k] += outL;
            right[start + k] += outR;
            peak = std::max (peak, std::max (std::abs (outL), std::abs (outR)));

            dropEnv *= dropDecay;
        }
    }

    level = peak;

    const bool excitationDone = pulsePos >= pulseLength && noiseLevel <= 1.0e-6f;
    if (excitationDone && peak < kSilenceLevel)
        silence();
}
} // namespace mp
