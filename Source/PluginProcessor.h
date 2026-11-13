#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Params.h"
#include "dsp/Voice.h"

#include <array>
#include <atomic>
#include <string_view>
#include <unordered_map>

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
    std::array<std::atomic<float>, mp::Voice::kBodyModes> bodyLevels {};

    mp::ModeParams getModeParams() const noexcept;
    bool getBodyParams (mp::ModeParams& out) const noexcept; // false when the body is off
    mp::VoiceSettings readVoiceSettings() const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaterialPercussionProcessor)
};
