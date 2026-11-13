#pragma once

#include <juce_audio_basics/juce_audio_basics.h> // juce::Decibels

#include "Theme.h"
#include "dsp/VoiceSettings.h"

#include <array>

namespace ui
{
// Spectrum of the object's modes on a log-frequency axis:
//  - bar height  = how loudly a hit excites that mode (strike position x exciter spectrum)
//  - dot size    = how long the mode rings
//  - bright fill = what is actually ringing right now
//  - shaded curve = the exciter's spectrum (hardness / colour)
//  - diamonds    = the coupled body's modes, when there is one
class ModeDisplay : public juce::Component
{
public:
    static constexpr int kMaxBodyModes = 32;

    ModeDisplay();

    void setTheme (const Theme& t);
    void setHint (const juce::String& text);

    struct State
    {
        const mp::ModeSet* modes = nullptr;
        float baseHz = 120.0f;
        const mp::ModeSet* bodyModes = nullptr; // null when the body is off
        float bodyHz = 120.0f;
        mp::ExciterType exciter = mp::ExciterType::Mallet;
        float hardness = 0.5f, color = 0.5f;
        const float* liveLevels = nullptr;      // kMaxModes
        const float* bodyLevels = nullptr;      // kMaxBodyModes
    };
    void update (const State& state);

    void paint (juce::Graphics&) override;

private:
    float exciterResponse (float hz) const;

    Theme theme;
    mp::ModeSet modes, bodyModes;
    bool hasBody = false;
    std::array<float, mp::kMaxModes> levels {};
    std::array<float, kMaxBodyModes> bodyLevels {};
    float baseHz = 120.0f, bodyHz = 120.0f, hardness = 0.5f, color = 0.5f;
    mp::ExciterType exciter = mp::ExciterType::Mallet;
    juce::String hint;
};
} // namespace ui
