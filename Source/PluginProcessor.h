#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Params.h"
#include "dsp/Capture.h"
#include "dsp/Voice.h"

#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <unordered_map>

class MaterialPercussionProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    static constexpr int kNumVoices = 8;

    MaterialPercussionProcessor();
    ~MaterialPercussionProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Shared with the editor
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // lets the UI trigger hits

    // Peak amplitude of each mode since the UI last read it (UI exchanges with 0).
    std::array<std::atomic<float>, mp::kMaxModes> modeLevels {};
    std::array<std::atomic<float>, mp::Voice::kBodyModes> bodyLevels {};

    // `capture` is what the Captured model reads its modes from: pass getCapture().get()
    // from the message thread (and keep the shared_ptr alive while using the result).
    mp::ModeParams getModeParams (const mp::CaptureData* capture) const noexcept;
    bool getBodyParams (mp::ModeParams& out, const mp::CaptureData* capture) const noexcept; // false when the body is off
    mp::VoiceSettings readVoiceSettings() const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    // Capture: a sound file is analysed on a background thread; the result becomes the
    // Captured model (its modes) and the Sample exciter (its residual attack), and is saved
    // with the plugin state.
    void loadCaptureFile (const juce::File& file);
    std::shared_ptr<const mp::CaptureData> getCapture() const;
    bool isCaptureBusy() const;
    juce::String getCaptureMessage() const; // last error, or empty
    static juce::String getCaptureWildcard();

private:
    void handleAsyncUpdate() override;
    void installCapture (std::shared_ptr<mp::CaptureData> data, bool setKnobs);
    void setParameter (const char* id, float value);
    static juce::ValueTree captureToTree (const mp::CaptureData& data);
    static std::shared_ptr<mp::CaptureData> captureFromTree (const juce::ValueTree& tree);

    void handleMidi (const juce::MidiMessage& message) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void renderVoices (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept;
    void publishModeLevels() noexcept;
    float param (const char* id) const noexcept;

    std::array<mp::Voice, kNumVoices> voices;
    mp::ModeSet modeSet, bodyModeSet;
    mp::ModeParams lastModeParams, lastBodyParams;
    bool lastBodyOn = false;
    int modeSetVersion = 0, bodyModeSetVersion = 0;
    mp::RenderContext context;
    juce::SmoothedValue<float> outputGain { 1.0f };

    // Raw parameter pointers, looked up once (keyed by id text: no allocation on lookup)
    std::unordered_map<std::string_view, std::atomic<float>*> rawParams;

    // Capture hand-off. The message thread owns uiCapture and posts new data to
    // pendingCapture; the audio thread swaps it into audioCapture without allocating or
    // freeing (the old one comes back through pendingCapture and is freed later, on the
    // message thread).
    std::shared_ptr<const mp::CaptureData> uiCapture;
    mutable juce::CriticalSection uiCaptureLock;
    std::shared_ptr<const mp::CaptureData> pendingCapture, audioCapture;
    bool pendingCaptureChanged = false;
    juce::SpinLock captureSwapLock;
    std::atomic<int> nextCaptureId { 1 };

    // Background analysis
    juce::ThreadPool loader { 1 };
    std::atomic<bool> cancelLoad { false };
    mutable juce::CriticalSection loadLock;
    std::shared_ptr<mp::CaptureData> loadedCapture;
    juce::String loadMessage;
    bool loadBusy = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaterialPercussionProcessor)
};
