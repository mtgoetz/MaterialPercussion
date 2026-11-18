#pragma once

#include "VoiceSettings.h"

#include <cstdint>

namespace mp
{
// What the exciter needs to know about the object it is driving, from the previous sample.
struct ExciterContext
{
    double velocity = 0.0;   // object velocity at the contact point (normalised: ~1 after a full hit)
    double admittance = 1.0; // how much one unit of force changes that velocity on the next sample
    double noiseGain = 1.0;  // RMS response of the object to unit-variance white-noise force
    double outputLevel = 0.0; // recent peak output level of the voice
};

// Produces the force that drives the object, one sample at a time.
//  - Mallet:    raised-cosine impact (+ bounces, + noise burst)
//  - Bow:       stick-slip friction against the object's own motion
//  - Scrape:    rough-surface noise, from smooth hiss to sparse ticks
//  - Particles: random grain collisions (shakers, rain, brushes)
//  - Feedback:  the object's own vibration amplified and fed back in (howls, drones)
// The continuous exciters run for `length` seconds, or for as long as the key is held.
class Exciter
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void trigger (float velocity, const VoiceSettings& settings) noexcept;
    void release() noexcept { gate = false; }

    // Call once per block (or control interval) before ticking.
    void update (const VoiceSettings& settings) noexcept;

    // The recorded attack for the Sample exciter. Call every block: the data can change.
    void setSample (const SampleSource& source) noexcept;

    double tick (const ExciterContext& ctx) noexcept;

    // Sound that goes straight to the output rather than into the object (the recorded
    // attack of the Sample exciter). Valid after each tick.
    double getDirect() const noexcept { return direct; }

    bool isActive() const noexcept;
    float getEnvelope() const noexcept { return (float) env; }

private:
    enum class Stage { Idle, Attack, Hold, Release };

    void startPulse (float hardness, float amplitude) noexcept;
    void startImpulse (float amplitude) noexcept;
    double nextPulse() noexcept;
    double nextSample() noexcept;
    double colorFilter (double x) noexcept;
    double white() noexcept;      // uniform, unit variance
    double uniform01() noexcept;  // (0, 1]
    void advanceEnvelope() noexcept;

    double sampleRate = 44100.0;
    ExciterType type = ExciterType::Mallet;
    float velocity = 0.0f;
    float hardness = 0.5f, noise = 0.0f;

    // Mallet pulse and bounces
    int pulseLength = 0, pulsePos = 0;
    double pulseScale = 0.0;
    float malletHardness = 0.5f;
    int bouncesLeft = 0;
    double bounceTimer = 0.0, bounceInterval = 0.0;
    float bounceAmp = 0.0f;

    // Noise burst at the start of a hit
    double noiseLevel = 0.0, noiseDecay = 0.0;

    // Envelope for the continuous exciters
    Stage stage = Stage::Idle;
    bool gate = false;
    double env = 0.0, attackInc = 0.0, releaseCoef = 0.0;
    double elapsed = 0.0, holdSamples = 0.0;

    // Colour filter (one-pole low-pass below the middle, high-pass above it)
    double colorCoef = 0.0, colorState = 0.0;
    bool colorHighPass = false, colorBypass = true;

    // Scrape spikes, particle grains
    double spikeTimer = 0.0, spikeValue = 0.0;
    double grainTimer = 0.0;
    int grainLength = 0, grainPos = 0;
    double grainScale = 0.0;

    // Feedback
    double fbDcX = 0.0, fbDcY = 0.0, dcCoef = 0.0;

    // Sample playback
    SampleSource sample;
    double samplePos = -1.0, sampleStep = 1.0;
    float sampleGain = 0.0f;
    double direct = 0.0;

    std::uint32_t rng = 0x2545f491u;
};
} // namespace mp
