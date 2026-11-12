#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
float percentValue (const juce::String& t) { return t.getFloatValue() / 100.0f; }

juce::String hzText (float v, int)
{
    return v < 1000.0f ? juce::String (v, 1) + " Hz" : juce::String (v / 1000.0f, 2) + " kHz";
}
float hzValue (const juce::String& t)
{
    const float v = t.getFloatValue();
    return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
}

juce::String secondsText (float v, int)
{
    return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s";
}
float secondsValue (const juce::String& t)
{
    const float v = t.getFloatValue();
    return t.containsIgnoreCase ("ms") ? v / 1000.0f : v;
}

juce::String dbText (float v, int) { return juce::String (v, 1) + " dB"; }

// Transparent below the knee, smoothly saturating to a ceiling of 1.0 above it.
inline float softClip (float x) noexcept
{
    constexpr float knee = 0.8f;
    const float a = std::abs (x);
    if (a <= knee)
        return x;
    const float y = knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee));
    return std::copysign (y, x);
}
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MaterialPercussionProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray modelNames;
    for (int i = 0; i < mp::kNumModels; ++i)
        modelNames.add (mp::getModelInfo ((mp::Model) i).name);

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::model, 1 }, "Model", modelNames, 0));

    NormalisableRange<float> pitchRange (20.0f, 2000.0f);
    pitchRange.setSkewForCentre (200.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::pitch, 1 }, "Pitch", pitchRange, 120.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (hzText).withValueFromStringFunction (hzValue)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::keyTrack, 1 }, "Key Track", true));

    NormalisableRange<float> decayRange (0.05f, 10.0f);
    decayRange.setSkewForCentre (1.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::decay, 1 }, "Decay", decayRange, 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (secondsText).withValueFromStringFunction (secondsValue)));

    auto addPercent = [&layout] (const char* id, const char* name, float defaultValue)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), defaultValue,
            AudioParameterFloatAttributes().withStringFromValueFunction (percentText).withValueFromStringFunction (percentValue)));
    };

    addPercent (ids::damping, "Damping", 0.5f);
    addPercent (ids::shape, "Shape", 0.0f);
    addPercent (ids::position, "Position", 0.3f);
    addPercent (ids::hardness, "Hardness", 0.5f);
    addPercent (ids::noise, "Noise", 0.0f);
    addPercent (ids::drop, "Pitch Drop", 0.1f);
    addPercent (ids::width, "Width", 0.3f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::gain, 1 }, "Output", NormalisableRange<float> (-36.0f, 6.0f), -6.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    return layout;
}

MaterialPercussionProcessor::MaterialPercussionProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MaterialPercussion", createParameterLayout())
{
    mp::initModeTables();

    pModel    = apvts.getRawParameterValue (ids::model);
    pPitch    = apvts.getRawParameterValue (ids::pitch);
    pKeyTrack = apvts.getRawParameterValue (ids::keyTrack);
    pDecay    = apvts.getRawParameterValue (ids::decay);
    pDamping  = apvts.getRawParameterValue (ids::damping);
    pShape    = apvts.getRawParameterValue (ids::shape);
    pPosition = apvts.getRawParameterValue (ids::position);
    pHardness = apvts.getRawParameterValue (ids::hardness);
    pNoise    = apvts.getRawParameterValue (ids::noise);
    pDrop     = apvts.getRawParameterValue (ids::drop);
    pWidth    = apvts.getRawParameterValue (ids::width);
    pGain     = apvts.getRawParameterValue (ids::gain);

    for (auto& l : modeLevels)
        l.store (0.0f);

    lastModeParams = getModeParams();
    mp::computeModes (lastModeParams, modeSet);
}

//==============================================================================
mp::ModeParams MaterialPercussionProcessor::getModeParams() const noexcept
{
    mp::ModeParams p;
    p.model = (mp::Model) juce::jlimit (0, mp::kNumModels - 1, (int) std::lround (pModel->load()));
    p.shape = pShape->load();
    p.position = pPosition->load();
    p.decay = pDecay->load();
    p.damping = pDamping->load();
    return p;
}

mp::VoiceSettings MaterialPercussionProcessor::readVoiceSettings() const noexcept
{
    mp::VoiceSettings s;
    s.pitchHz = pPitch->load();
    s.keyTrack = pKeyTrack->load() > 0.5f;
    s.hardness = pHardness->load();
    s.noise = pNoise->load();
    s.drop = pDrop->load();
    s.width = pWidth->load();
    s.gain = mp::getModelInfo (lastModeParams.model).gain;
    return s;
}

void MaterialPercussionProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& v : voices)
        v.prepare (sampleRate);

    lastModeParams = getModeParams();
    mp::computeModes (lastModeParams, modeSet);
    ++modeSetVersion;

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    keyboardState.reset();
}

bool MaterialPercussionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void MaterialPercussionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2)
        return;

    // Merge in hits triggered from the UI (clicking the object).
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    const auto params = getModeParams();
    if (! (params == lastModeParams))
    {
        mp::computeModes (params, modeSet);
        lastModeParams = params;
        ++modeSetVersion;
    }
    settings = readVoiceSettings();

    for (auto& v : voices)
        v.clearModePeaks();

    // Render between MIDI events for sample-accurate hits.
    int position = 0;
    for (const auto meta : midi)
    {
        const int eventPos = juce::jlimit (0, numSamples, meta.samplePosition);
        if (eventPos > position)
        {
            renderVoices (buffer, position, eventPos - position);
            position = eventPos;
        }
        handleMidi (meta.getMessage());
    }
    if (position < numSamples)
        renderVoices (buffer, position, numSamples - position);

    // Output gain + safety soft clip
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGain.getNextValue();
        left[i] = softClip (left[i] * g);
        right[i] = softClip (right[i] * g);
    }

    publishModeLevels();
}

void MaterialPercussionProcessor::renderVoices (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept
{
    auto* left = buffer.getWritePointer (0, start);
    auto* right = buffer.getWritePointer (1, start);
    for (auto& v : voices)
        v.render (left, right, numSamples, modeSet, modeSetVersion, settings);
}

void MaterialPercussionProcessor::handleMidi (const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
        noteOn (message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isAllSoundOff())
        for (auto& v : voices)
            v.reset();
    // Note-offs are ignored on purpose: struck objects ring out naturally.
}

void MaterialPercussionProcessor::noteOn (int note, float velocity) noexcept
{
    mp::ModalVoice* target = nullptr;

    // Re-strike an object that is already ringing on this note...
    for (auto& v : voices)
        if (v.isActive() && v.getNote() == note) { target = &v; break; }

    // ...otherwise use a free voice...
    if (target == nullptr)
        for (auto& v : voices)
            if (! v.isActive()) { target = &v; break; }

    // ...otherwise steal the quietest one.
    if (target == nullptr)
        target = &*std::min_element (voices.begin(), voices.end(),
                                     [] (const auto& a, const auto& b) { return a.getLevel() < b.getLevel(); });

    target->strike (note, velocity, settings);
}

void MaterialPercussionProcessor::publishModeLevels() noexcept
{
    for (int m = 0; m < mp::kMaxModes; ++m)
    {
        float peak = 0.0f;
        for (const auto& v : voices)
            peak = std::max (peak, v.getModePeak (m));

        auto& slot = modeLevels[(size_t) m];
        if (peak > slot.load (std::memory_order_relaxed))
            slot.store (peak, std::memory_order_relaxed);
    }
}

//==============================================================================
juce::AudioProcessorEditor* MaterialPercussionProcessor::createEditor()
{
    return new MaterialPercussionEditor (*this);
}

void MaterialPercussionProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MaterialPercussionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MaterialPercussionProcessor();
}
