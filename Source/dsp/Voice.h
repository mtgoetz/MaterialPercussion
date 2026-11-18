#pragma once

#include "Exciter.h"
#include "ModalBank.h"
#include "Waveguide.h"

namespace mp
{
// Everything a voice needs for one block, shared by all voices.
struct RenderContext
{
    const ModeSet* modes = nullptr;     // main object (also drives the waveguide's meters)
    int modesVersion = 0;
    const ModeSet* bodyModes = nullptr; // coupled body (only used when settings.bodyOn)
    int bodyVersion = 0;
    ModeParams params;                  // main object parameters (the waveguide needs them)
    SampleSource sample;                // recorded attack for the Sample exciter
    VoiceSettings settings;
};

// One sounding object:
//
//   exciter --(1 - blend)--> object (modal bank or waveguide) <--spring--> body (modal bank)
//      \______(blend)________________________________________________________/
//
// plus a rattle (one-sided contact against the object's motion) and pitch modulation from
// the impact (Pitch Drop) and from amplitude (Tension). Re-striking a ringing voice adds
// energy to it, like hitting a real drum twice.
class Voice
{
public:
    static constexpr int kBodyModes = 32;

    void prepare (double sampleRate);
    void reset() noexcept;

    void strike (int note, float velocity, const RenderContext& ctx) noexcept;
    void release (int note) noexcept;

    // Adds into left/right.
    void render (float* left, float* right, int numSamples, const RenderContext& ctx) noexcept;

    bool isActive() const noexcept { return active; }
    int getNote() const noexcept { return note; }
    float getLevel() const noexcept { return level; }

    float getModePeak (int index) const noexcept;
    float getBodyPeak (int index) const noexcept { return body.getPeak (index); }
    void clearPeaks() noexcept;

private:
    void silence() noexcept;

    double sampleRate = 44100.0;
    bool active = false;
    int note = 60;
    float level = 0.0f;

    Exciter exciter;
    ModalBank<kMaxModes> modal;
    ModalBank<kBodyModes> body;
    Waveguide waveguide;
    bool useWaveguide = false;
    float waveguidePeaks[kMaxModes] {};

    // Pitch drop envelope and waveguide tension follower
    double dropEnv = 0.0, dropDecay = 0.0;
    bool dropWasActive = false;
    double energy = 0.0, energyCoef = 0.0;
    double outputLevel = 0.0, levelRelease = 0.0; // for the feedback exciter's level control

    // Rattle
    double rattleExcess = 0.0, buzzEnv = 0.0, buzzDecay = 0.0, rattleHp = 0.0;
    double rattleLp = 0.0, clickCoef = 0.0;
    std::uint32_t rng = 0x68e31da4u;

    // Coefficient cache keys
    int lastVersion = -1, lastBodyVersion = -1;
    double lastBaseFreq = -1.0, lastBodyFreq = -1.0;
    float lastWidth = -1.0f;
    bool bodyWasOn = false;
};
} // namespace mp
