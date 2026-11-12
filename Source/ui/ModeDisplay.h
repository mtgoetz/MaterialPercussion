#pragma once

#include "Theme.h"
#include "dsp/ModeModels.h"

#include <array>

namespace ui
{
// Spectrum of the object's modes on a log-frequency axis:
//  - bar height  = how loudly a hit excites that mode (strike position x mallet spectrum)
//  - dot size    = how long the mode rings
//  - bright fill = what is actually ringing right now
//  - shaded curve = the mallet's spectrum (hardness)
class ModeDisplay : public juce::Component
{
public:
    ModeDisplay();

    void setTheme (const Theme& t);
    void setHint (const juce::String& text);
    void update (const mp::ModeSet& modes, float baseHz, float hardness, const float* liveLevels);

    void paint (juce::Graphics&) override;

private:
    Theme theme;
    mp::ModeSet modes;
    std::array<float, mp::kMaxModes> levels {};
    float baseHz = 120.0f, hardness = 0.5f;
    juce::String hint;
};
} // namespace ui
