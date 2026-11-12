#pragma once

#include "ModeModels.h"

#include <cstdint>

namespace mp
{
// Per-block settings that do not change the mode set itself.
struct VoiceSettings
{
    float pitchHz = 120.0f;  // frequency of the lowest mode at MIDI note 60
    bool keyTrack = true;
    float hardness = 0.5f;
    float noise = 0.0f;
    float drop = 0.0f;       // pitch jump at impact (tension modulation)
    float width = 0.0f;
    float gain = 1.0f;       // model loudness compensation
};

// One struck object: a raised-cosine mallet pulse (+ optional noise burst) driving a
// bank of two-pole resonators, one per mode. Re-striking a ringing voice adds energy
// to the existing vibration, just like hitting a real drum twice.
class ModalVoice
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void strike (int note, float velocity, const VoiceSettings& settings) noexcept;

    // Adds into left/right. `modeSetVersion` changes whenever `modes` is rebuilt.
    void render (float* left, float* right, int numSamples,
                 const ModeSet& modes, int modeSetVersion, const VoiceSettings& settings) noexcept;

    bool isActive() const noexcept { return active; }
    int getNote() const noexcept { return note; }
    float getLevel() const noexcept { return level; }

    float getModePeak (int index) const noexcept { return modePeaks[index]; }
    void clearModePeaks() noexcept;

private:
    void updateCoefficients (const ModeSet& modes, float baseFreq, float width, float freqMul) noexcept;
    float nextExcitation() noexcept;
    void silence() noexcept;

    double sampleRate = 44100.0;
    bool active = false;
    int note = 60;
    float level = 0.0f;

    // Resonator bank (double precision keeps low, long-ringing modes stable)
    double y1[kMaxModes] {}, y2[kMaxModes] {};
    double b1[kMaxModes] {}, b2[kMaxModes] {}, inGain[kMaxModes] {};
    float panL[kMaxModes] {}, panR[kMaxModes] {};
    float modePeaks[kMaxModes] {};

    // Exciter
    int pulseLength = 0, pulsePos = 0;
    float pulseScale = 0.0f;
    float noiseLevel = 0.0f, noiseDecay = 0.0f;
    std::uint32_t rng = 0x9e3779b9u;

    // Pitch drop envelope
    float dropEnv = 0.0f, dropDecay = 0.0f;
    bool dropWasActive = false;

    // Coefficient cache keys
    int lastVersion = -1;
    float lastBaseFreq = -1.0f;
    float lastWidth = -1.0f;
};
} // namespace mp
