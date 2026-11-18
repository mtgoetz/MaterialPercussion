#pragma once

#include <array>
#include <cmath>
#include <vector>

// Modal descriptions of struck objects. Everything here is plain C++ (no JUCE) so the
// same functions drive both the audio engine and the UI visualisation.
namespace mp
{
constexpr int kMaxModes = 128;  // capacity of a mode set (captured sounds use all of it)
constexpr int kModelModes = 64; // modes the built-in physical models generate
constexpr int kNumModels = 9;

// Membrane..String are modal (a bank of resonators); Tube and Wire are digital waveguides
// (delay-line loops), which bend and buzz more naturally under the nonlinear controls.
// Captured is modal, with modes measured from a recording (see Capture.h).
enum class Model : int { Membrane = 0, Wood, Metal, Glass, Bell, String, Tube, Wire, Captured };

// Modes measured from a recording: absolute frequencies, so the Pitch knob can transpose.
struct CapturedMode
{
    float hz = 100.0f;
    float amp = 0.0f;  // signed, relative
    float t60 = 1.0f;  // seconds
};

struct CaptureModes
{
    std::vector<CapturedMode> modes; // sorted by frequency
    float refHz = 100.0f;            // the frequency that plays at the Pitch knob's value
};

inline bool isWaveguide (Model m) noexcept { return m == Model::Tube || m == Model::Wire; }

struct ModelInfo
{
    const char* name;
    const char* blurb;      // one-line description for the UI
    const char* shapeName;  // what the Shape knob means for this model
    const char* shapeHint;
    float decayScale;       // multiplies the Decay control
    float dampMin;          // decay-vs-frequency exponent at Damping = 0
    float dampMax;          // ... at Damping = 1
    float gain;             // rough loudness match between models
};

const ModelInfo& getModelInfo (Model model) noexcept;

struct ModeParams
{
    Model model = Model::Membrane;
    float shape = 0.0f;     // 0..1, meaning depends on the model
    float position = 0.25f; // 0 = centre / dull spot, 1 = edge / bright spot
    float decay = 1.0f;     // seconds, T60 of the lowest mode before model scaling
    float damping = 0.5f;   // 0..1, how much faster the high modes die

    // Captured model only. The id changes whenever a new recording is loaded.
    const CaptureModes* capture = nullptr;
    int captureId = 0;

    bool operator== (const ModeParams&) const = default;
};

struct Mode
{
    float ratio = 1.0f; // frequency relative to the fundamental
    float amp = 0.0f;   // excitation gain at the strike point (signed, normalised)
    float t60 = 1.0f;   // seconds
};

struct ModeSet
{
    std::array<Mode, kMaxModes> modes {};
    int count = 0;
};

// Builds lazily-computed tables. Call once from a non-audio thread before use.
void initModeTables();

// Pure, allocation-free: safe to call on the audio thread.
void computeModes (const ModeParams& params, ModeSet& out) noexcept;

// T60 of a mode at `ratio` x the fundamental (the model's decay law).
float modeDecaySeconds (const ModeParams& params, double ratio) noexcept;

//==============================================================================
// Strike-position mappings shared by the DSP and the clickable object view.
inline double membraneRadius (double p) noexcept { return 0.02 + 0.9 * p; }  // fraction of head radius
inline double barX (double p) noexcept           { return 0.5 - 0.48 * p; }  // fraction of bar length
inline double plateX (double p) noexcept         { return 0.5 - 0.42 * p; }
inline double plateY (double p) noexcept         { return 0.5 - 0.31 * p; }
inline double stringX (double p) noexcept        { return 0.5 - 0.48 * p; }
inline double tubeX (double p) noexcept          { return 0.5 - 0.46 * p; }  // fraction of tube length
inline double wireX (double p) noexcept          { return 0.5 - 0.48 * p; }
inline double plateAspect (double shape) noexcept { return 1.013 * (1.0 + 1.5 * shape); }

// Mallet contact time: soft felt (6 ms) to hard stick (0.08 ms).
inline float strikeWidthSeconds (float hardness) noexcept
{
    return 0.006f * std::pow (0.08f / 6.0f, hardness);
}

// Magnitude spectrum of the raised-cosine strike pulse (1 at DC).
inline float strikeSpectrum (float hz, float widthSeconds) noexcept
{
    constexpr float pi = 3.14159265f;
    const float x = hz * widthSeconds;
    const float d = 1.0f - x * x;
    if (std::abs (d) < 1.0e-3f)
        return 0.5f;
    const float sinc = x < 1.0e-6f ? 1.0f : std::sin (pi * x) / (pi * x);
    return std::abs (sinc / d);
}
} // namespace mp
