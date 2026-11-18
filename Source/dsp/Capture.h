#pragma once

#include "ModeModels.h"

#include <atomic>
#include <string>
#include <vector>

namespace mp
{
// Everything measured from one recorded hit. Plain C++ (no JUCE), immutable once built.
struct CaptureData
{
    int id = 0;                     // set by whoever installs it; changes with every load
    std::string name;
    CaptureModes modes;             // up to kMaxModes, sorted by frequency

    // The part of the recording the modes do not explain: stick click, scrape, air.
    // Scaled to the normalised mode set, so a unit strike plus this residual at unit level
    // reproduces the recording's balance of attack and ring.
    std::vector<float> residual;
    double residualRate = 48000.0;
    float residualForceNorm = 1.0f; // makes the residual unit-energy when it drives an object

    std::vector<float> overview;    // peak envelope of the analysed part (0..1), for drawing
    float seconds = 0.0f;           // how much of the recording was analysed
};

// Finds the modes of a recorded hit:
//   1. find the onset;
//   2. pick spectral peaks from one long FFT;
//   3. for each peak, shift its band down to 0 Hz, decimate it, and fit damped
//      exponentials by linear prediction. That gives frequency, decay, amplitude and phase,
//      and splits close pairs of modes (the beating doublets of bells and glasses) that
//      the FFT alone cannot resolve;
//   4. keep the strongest kMaxModes, and subtract them from the recording to get the
//      residual attack.
// Takes up to a second or two: call it on a background thread. `cancel` is polled.
// Returns an empty string on success, otherwise a short description of the problem.
std::string analyseRecording (const float* samples, int numSamples, double sampleRate,
                              CaptureData& out, const std::atomic<bool>* cancel = nullptr);
} // namespace mp
