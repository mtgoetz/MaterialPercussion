#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Params.h"
#include "dsp/ModalVoice.h"

#include <array>
#include <atomic>

class MaterialPercussionProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumVoices = 8;

    MaterialPercussionProcessor();
    ~MaterialPercussionProcessor() override = default;

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

    mp::ModeParams getModeParams() const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void handleMidi (const juce::MidiMessage& message) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void renderVoices (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept;
    mp::VoiceSettings readVoiceSettings() const noexcept;
    void publishModeLevels() noexcept;

    std::array<mp::ModalVoice, kNumVoices> voices;
    mp::ModeSet modeSet;
    mp::ModeParams lastModeParams;
    int modeSetVersion = 0;
    mp::VoiceSettings settings;
    juce::SmoothedValue<float> outputGain { 1.0f };

    std::atomic<float>* pModel = nullptr;
    std::atomic<float>* pPitch = nullptr;
    std::atomic<float>* pKeyTrack = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pDamping = nullptr;
    std::atomic<float>* pShape = nullptr;
    std::atomic<float>* pPosition = nullptr;
    std::atomic<float>* pHardness = nullptr;
    std::atomic<float>* pNoise = nullptr;
    std::atomic<float>* pDrop = nullptr;
    std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pGain = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaterialPercussionProcessor)
};
