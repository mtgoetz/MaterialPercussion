#pragma once

#include "ModeModels.h"

namespace mp
{
//==============================================================================
// How the object is set in motion.
enum class ExciterType : int { Mallet = 0, Bow, Scrape, Particles, Feedback };
constexpr int kNumExciters = 5;

struct ExciterInfo
{
    const char* name;
    const char* blurb;
    const char* amountName;  // what the Hardness knob means for this exciter
    const char* amountHint;
    const char* colorHint;
    bool continuous;         // runs for Length / while the key is held
};

const ExciterInfo& getExciterInfo (ExciterType type) noexcept;

// Magnitude response (0..1) of the exciter's tone for the display.
float exciterSpectrum (ExciterType type, float hz, float hardness, float color) noexcept;

//==============================================================================
// Block-rate settings shared by every voice. Things that change the mode set itself live
// in ModeParams instead.
struct VoiceSettings
{
    float pitchHz = 120.0f;   // frequency of the lowest mode at MIDI note 60
    bool keyTrack = true;

    // Exciter
    ExciterType exciter = ExciterType::Mallet;
    float hardness = 0.5f;    // hardness / pressure / roughness / density / drive
    float color = 0.5f;       // exciter tone: dark .. neutral .. bright
    float length = 1.0f;      // seconds, continuous exciters
    float bounce = 0.0f;      // mallet bounces
    float noise = 0.0f;

    // Nonlinear
    float drop = 0.0f;        // impact pitch jump
    float tension = 0.0f;     // amplitude-dependent stiffness: pitch glide + energy cascade
    float rattle = 0.0f;      // contact buzz amount
    float gap = 0.3f;         // how far the object must move before it buzzes

    // Body (second, coupled resonator)
    bool bodyOn = false;
    float bodyTune = 0.0f;    // semitones relative to the main object
    float bodyBlend = 0.0f;   // 0 = exciter hits the object, 1 = it hits the body
    float bodyCouple = 0.0f;  // spring between object and body
    float bodyGain = 1.0f;

    float width = 0.3f;
    float gain = 1.0f;        // model loudness compensation
};
} // namespace mp
