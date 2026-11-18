#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstring>

namespace
{
constexpr const char* kCaptureTag = "Capture"; // child of the saved state holding the capture

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

juce::String semitoneText (float v, int)
{
    return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st";
}

juce::String colorText (float v, int)
{
    if (v < 0.47f) return "Dark " + juce::String (juce::roundToInt ((0.47f - v) / 0.47f * 100.0f)) + "%";
    if (v > 0.53f) return "Bright " + juce::String (juce::roundToInt ((v - 0.53f) / 0.47f * 100.0f)) + "%";
    return "Neutral";
}

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

const char* const kAllParamIds[] = {
    ids::model, ids::pitch, ids::keyTrack, ids::decay, ids::damping, ids::shape, ids::position,
    ids::exciter, ids::hardness, ids::color, ids::length, ids::bounce, ids::noise,
    ids::drop, ids::tension, ids::rattle, ids::gap,
    ids::bodyModel, ids::bodyTune, ids::bodyDecay, ids::bodyBlend, ids::bodyCouple,
    ids::width, ids::gain,
};
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MaterialPercussionProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addPercent = [&layout] (const char* id, const char* name, float defaultValue)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), defaultValue,
            AudioParameterFloatAttributes().withStringFromValueFunction (percentText).withValueFromStringFunction (percentValue)));
    };

    auto addSeconds = [&layout] (const char* id, const char* name, float lo, float hi, float centre, float defaultValue)
    {
        NormalisableRange<float> range (lo, hi);
        range.setSkewForCentre (centre);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, defaultValue,
            AudioParameterFloatAttributes().withStringFromValueFunction (secondsText).withValueFromStringFunction (secondsValue)));
    };

    // --- Object
    StringArray modelNames, bodyNames { "Off" };
    for (int i = 0; i < mp::kNumModels; ++i)
    {
        modelNames.add (mp::getModelInfo ((mp::Model) i).name);
        bodyNames.add (mp::getModelInfo ((mp::Model) i).name);
    }
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::model, 1 }, "Model", modelNames, 0));

    NormalisableRange<float> pitchRange (20.0f, 2000.0f);
    pitchRange.setSkewForCentre (200.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::pitch, 1 }, "Pitch", pitchRange, 120.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (hzText).withValueFromStringFunction (hzValue)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::keyTrack, 1 }, "Key Track", true));
    addSeconds (ids::decay, "Decay", 0.05f, 10.0f, 1.0f, 1.0f);
    addPercent (ids::damping, "Damping", 0.5f);
    addPercent (ids::shape, "Shape", 0.0f);
    addPercent (ids::position, "Position", 0.3f);

    // --- Exciter
    StringArray exciterNames;
    for (int i = 0; i < mp::kNumExciters; ++i)
        exciterNames.add (mp::getExciterInfo ((mp::ExciterType) i).name);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::exciter, 1 }, "Exciter", exciterNames, 0));

    addPercent (ids::hardness, "Hardness", 0.5f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::color, 1 }, "Colour", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction (colorText)));
    addSeconds (ids::length, "Length", 0.02f, 8.0f, 0.6f, 1.0f);
    addPercent (ids::bounce, "Bounce", 0.0f);
    addPercent (ids::noise, "Noise", 0.0f);

    // --- Nonlinear
    addPercent (ids::drop, "Pitch Drop", 0.1f);
    addPercent (ids::tension, "Tension", 0.0f);
    addPercent (ids::rattle, "Rattle", 0.0f);
    addPercent (ids::gap, "Gap", 0.3f);

    // --- Body
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::bodyModel, 1 }, "Body", bodyNames, 0));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::bodyTune, 1 }, "Body Tune", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (semitoneText)));
    addSeconds (ids::bodyDecay, "Body Decay", 0.05f, 10.0f, 1.0f, 0.6f);
    addPercent (ids::bodyBlend, "Body Blend", 0.0f);
    addPercent (ids::bodyCouple, "Body Couple", 0.5f);

    // --- Output
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

    for (const char* id : kAllParamIds)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        rawParams[id] = p;
    }

    for (auto& l : modeLevels)
        l.store (0.0f);
    for (auto& l : bodyLevels)
        l.store (0.0f);

    lastModeParams = getModeParams (audioCapture.get());
    mp::computeModes (lastModeParams, modeSet);
    lastBodyOn = getBodyParams (lastBodyParams, audioCapture.get());
    mp::computeModes (lastBodyParams, bodyModeSet);
}

MaterialPercussionProcessor::~MaterialPercussionProcessor()
{
    cancelLoad = true;
    loader.removeAllJobs (true, 10000);
    cancelPendingUpdate();
}

float MaterialPercussionProcessor::param (const char* id) const noexcept
{
    const auto it = rawParams.find (id);
    return it != rawParams.end() ? it->second->load() : 0.0f;
}

//==============================================================================
namespace
{
// Only the Captured model looks at the capture, so other models don't recompute when it changes.
void attachCapture (mp::ModeParams& p, const mp::CaptureData* capture) noexcept
{
    const bool use = p.model == mp::Model::Captured && capture != nullptr;
    p.capture = use ? &capture->modes : nullptr;
    p.captureId = use ? capture->id : 0;
}
} // namespace

mp::ModeParams MaterialPercussionProcessor::getModeParams (const mp::CaptureData* capture) const noexcept
{
    mp::ModeParams p;
    p.model = (mp::Model) juce::jlimit (0, mp::kNumModels - 1, (int) std::lround (param (ids::model)));
    p.shape = param (ids::shape);
    p.position = param (ids::position);
    p.decay = param (ids::decay);
    p.damping = param (ids::damping);
    attachCapture (p, capture);
    return p;
}

bool MaterialPercussionProcessor::getBodyParams (mp::ModeParams& out, const mp::CaptureData* capture) const noexcept
{
    const int choice = juce::jlimit (0, mp::kNumModels, (int) std::lround (param (ids::bodyModel)));
    out = getModeParams (nullptr);
    out.model = (mp::Model) juce::jmax (0, choice - 1);
    out.decay = param (ids::bodyDecay);
    out.shape = 0.5f;
    attachCapture (out, capture);
    return choice > 0;
}

mp::VoiceSettings MaterialPercussionProcessor::readVoiceSettings() const noexcept
{
    mp::VoiceSettings s;
    s.pitchHz = param (ids::pitch);
    s.keyTrack = param (ids::keyTrack) > 0.5f;

    s.exciter = (mp::ExciterType) juce::jlimit (0, mp::kNumExciters - 1, (int) std::lround (param (ids::exciter)));
    s.hardness = param (ids::hardness);
    s.color = param (ids::color);
    s.length = param (ids::length);
    s.bounce = param (ids::bounce);
    s.noise = param (ids::noise);

    s.drop = param (ids::drop);
    s.tension = param (ids::tension);
    s.rattle = param (ids::rattle);
    s.gap = param (ids::gap);

    mp::ModeParams bodyParams;
    s.bodyOn = getBodyParams (bodyParams, nullptr);
    s.bodyTune = param (ids::bodyTune);
    s.bodyBlend = param (ids::bodyBlend);
    s.bodyCouple = param (ids::bodyCouple);
    s.bodyGain = mp::getModelInfo (bodyParams.model).gain;

    s.width = param (ids::width);
    s.gain = mp::getModelInfo (getModeParams (nullptr).model).gain;
    return s;
}

void MaterialPercussionProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& v : voices)
        v.prepare (sampleRate);

    lastModeParams = getModeParams (audioCapture.get());
    mp::computeModes (lastModeParams, modeSet);
    ++modeSetVersion;
    lastBodyOn = getBodyParams (lastBodyParams, audioCapture.get());
    mp::computeModes (lastBodyParams, bodyModeSet);
    ++bodyModeSetVersion;

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (ids::gain)));
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

    // Pick up a newly loaded capture. Swapping shared_ptrs neither allocates nor frees; the
    // old one goes back to the message thread through pendingCapture.
    if (captureSwapLock.tryEnter())
    {
        if (pendingCaptureChanged)
        {
            std::swap (audioCapture, pendingCapture);
            pendingCaptureChanged = false;
        }
        captureSwapLock.exit();
    }

    const auto params = getModeParams (audioCapture.get());
    if (! (params == lastModeParams))
    {
        mp::computeModes (params, modeSet);
        lastModeParams = params;
        ++modeSetVersion;
    }

    mp::ModeParams bodyParams;
    const bool bodyOn = getBodyParams (bodyParams, audioCapture.get());
    if (bodyOn != lastBodyOn || ! (bodyParams == lastBodyParams))
    {
        mp::computeModes (bodyParams, bodyModeSet);
        lastBodyParams = bodyParams;
        lastBodyOn = bodyOn;
        ++bodyModeSetVersion;
    }

    context.modes = &modeSet;
    context.modesVersion = modeSetVersion;
    context.bodyModes = &bodyModeSet;
    context.bodyVersion = bodyModeSetVersion;
    context.params = params;
    context.settings = readVoiceSettings();
    context.sample = {};
    if (audioCapture != nullptr && ! audioCapture->residual.empty())
        context.sample = { audioCapture->residual.data(), (int) audioCapture->residual.size(),
                           audioCapture->residualRate, audioCapture->residualForceNorm };

    for (auto& v : voices)
        v.clearPeaks();

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
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (param (ids::gain)));
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
        v.render (left, right, numSamples, context);
}

void MaterialPercussionProcessor::handleMidi (const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
        noteOn (message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff())
        for (auto& v : voices)
            v.release (message.getNoteNumber()); // only ends bows/scrapes; struck objects ring on
    else if (message.isAllSoundOff())
        for (auto& v : voices)
            v.reset();
}

void MaterialPercussionProcessor::noteOn (int note, float velocity) noexcept
{
    mp::Voice* target = nullptr;

    // Re-strike an object that is already ringing on this note...
    for (auto& v : voices)
        if (v.isActive() && v.getNote() == note) { target = &v; break; }

    // ...otherwise use a free voice...
    if (target == nullptr)
        for (auto& v : voices)
            if (! v.isActive()) { target = &v; break; }

    // ...otherwise steal the quietest one.
    if (target == nullptr)
    {
        target = &*std::min_element (voices.begin(), voices.end(),
                                     [] (const auto& a, const auto& b) { return a.getLevel() < b.getLevel(); });
        target->reset();
    }

    target->strike (note, velocity, context);
}

void MaterialPercussionProcessor::publishModeLevels() noexcept
{
    auto publish = [] (auto& slots, int index, float peak)
    {
        auto& slot = slots[(size_t) index];
        if (peak > slot.load (std::memory_order_relaxed))
            slot.store (peak, std::memory_order_relaxed);
    };

    for (int m = 0; m < mp::kMaxModes; ++m)
    {
        float peak = 0.0f;
        for (const auto& v : voices)
            peak = std::max (peak, v.getModePeak (m));
        publish (modeLevels, m, peak);
    }
    for (int m = 0; m < mp::Voice::kBodyModes; ++m)
    {
        float peak = 0.0f;
        for (const auto& v : voices)
            peak = std::max (peak, v.getBodyPeak (m));
        publish (bodyLevels, m, peak);
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
    if (auto capture = getCapture())
        state.appendChild (captureToTree (*capture), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MaterialPercussionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            const auto captureTree = tree.getChildWithName (kCaptureTag);
            if (captureTree.isValid())
                tree.removeChild (captureTree, nullptr);
            apvts.replaceState (tree);

            // A state without a capture clears the current one: it belongs to the old patch.
            installCapture (captureTree.isValid() ? captureFromTree (captureTree) : nullptr, false);
        }
}

//==============================================================================
// Capture

namespace
{
juce::String floatsToText (const float* data, size_t count)
{
    return juce::MemoryBlock (data, count * sizeof (float)).toBase64Encoding();
}

std::vector<float> textToFloats (const juce::String& text)
{
    juce::MemoryBlock block;
    if (! block.fromBase64Encoding (text))
        return {};
    std::vector<float> out (block.getSize() / sizeof (float));
    std::memcpy (out.data(), block.getData(), out.size() * sizeof (float));
    return out;
}

// Reads up to 30 s of the file, mixed to mono, and analyses it.
juce::String readAndAnalyse (const juce::File& file, mp::CaptureData& out, const std::atomic<bool>& cancel)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return "Can't read " + file.getFileName() + " as audio";
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return file.getFileName() + " has no audio in it";

    const int numSamples = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (30.0 * reader->sampleRate));
    const int numChannels = (int) juce::jlimit (1u, 8u, reader->numChannels);
    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    if (! reader->read (buffer.getArrayOfWritePointers(), numChannels, 0, numSamples))
        return "Can't read " + file.getFileName();

    std::vector<float> mono ((size_t) numSamples, 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::addWithMultiply (mono.data(), buffer.getReadPointer (ch), 1.0f / (float) numChannels, numSamples);

    const auto error = mp::analyseRecording (mono.data(), numSamples, reader->sampleRate, out, &cancel);
    out.name = file.getFileNameWithoutExtension().toStdString();
    return juce::String (error);
}
} // namespace

juce::String MaterialPercussionProcessor::getCaptureWildcard()
{
    return "*.wav;*.aif;*.aiff;*.flac;*.ogg";
}

void MaterialPercussionProcessor::loadCaptureFile (const juce::File& file)
{
    // Only one analysis at a time: stop any that is still running.
    cancelLoad = true;
    loader.removeAllJobs (true, 10000);
    cancelLoad = false;

    {
        const juce::ScopedLock sl (loadLock);
        loadBusy = true;
        loadMessage = "Analysing " + file.getFileName() + "...";
    }

    loader.addJob ([this, file]
    {
        auto data = std::make_shared<mp::CaptureData>();
        const auto error = readAndAnalyse (file, *data, cancelLoad);
        if (cancelLoad)
            return;
        {
            const juce::ScopedLock sl (loadLock);
            loadBusy = false;
            loadMessage = error;
            if (error.isEmpty())
                loadedCapture = std::move (data);
        }
        triggerAsyncUpdate();
    });
}

void MaterialPercussionProcessor::handleAsyncUpdate()
{
    std::shared_ptr<mp::CaptureData> data;
    {
        const juce::ScopedLock sl (loadLock);
        data = std::move (loadedCapture);
        loadedCapture.reset();
    }
    if (data != nullptr)
        installCapture (std::move (data), true);
}

std::shared_ptr<const mp::CaptureData> MaterialPercussionProcessor::getCapture() const
{
    const juce::ScopedLock sl (uiCaptureLock);
    return uiCapture;
}

bool MaterialPercussionProcessor::isCaptureBusy() const
{
    const juce::ScopedLock sl (loadLock);
    return loadBusy;
}

juce::String MaterialPercussionProcessor::getCaptureMessage() const
{
    const juce::ScopedLock sl (loadLock);
    return loadMessage;
}

void MaterialPercussionProcessor::installCapture (std::shared_ptr<mp::CaptureData> data, bool setKnobs)
{
    if (data != nullptr)
        data->id = nextCaptureId++;
    std::shared_ptr<const mp::CaptureData> capture = std::move (data);

    {
        const juce::ScopedLock sl (uiCaptureLock);
        uiCapture = capture;
    }

    // Hand it to the audio thread. Whatever was pending before is released here, outside
    // the spin lock, on this thread.
    std::shared_ptr<const mp::CaptureData> old = capture;
    {
        const juce::SpinLock::ScopedLockType sl (captureSwapLock);
        std::swap (pendingCapture, old);
        pendingCaptureChanged = true;
    }
    old.reset();

    if (setKnobs && capture != nullptr)
    {
        // Switch to the capture with every knob at its neutral setting, so the first hit
        // sounds like the recording.
        setParameter (ids::model, (float) mp::Model::Captured);
        setParameter (ids::pitch, capture->modes.refHz);
        setParameter (ids::shape, 0.5f);
        setParameter (ids::position, 0.5f);
        setParameter (ids::decay, 1.0f);
        setParameter (ids::damping, 0.5f);
        setParameter (ids::exciter, (float) mp::ExciterType::Sample);
        setParameter (ids::hardness, 1.0f);
        setParameter (ids::color, 0.5f);
        setParameter (ids::bounce, 0.0f);
        setParameter (ids::noise, 1.0f);
    }
}

void MaterialPercussionProcessor::setParameter (const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

juce::ValueTree MaterialPercussionProcessor::captureToTree (const mp::CaptureData& data)
{
    std::vector<float> modes;
    for (const auto& m : data.modes.modes)
        modes.insert (modes.end(), { m.hz, m.amp, m.t60 });

    juce::ValueTree tree (kCaptureTag);
    tree.setProperty ("name", juce::String (data.name), nullptr);
    tree.setProperty ("refHz", data.modes.refHz, nullptr);
    tree.setProperty ("rate", data.residualRate, nullptr);
    tree.setProperty ("forceNorm", data.residualForceNorm, nullptr);
    tree.setProperty ("seconds", data.seconds, nullptr);
    tree.setProperty ("modes", floatsToText (modes.data(), modes.size()), nullptr);
    tree.setProperty ("residual", floatsToText (data.residual.data(), data.residual.size()), nullptr);
    tree.setProperty ("overview", floatsToText (data.overview.data(), data.overview.size()), nullptr);
    return tree;
}

std::shared_ptr<mp::CaptureData> MaterialPercussionProcessor::captureFromTree (const juce::ValueTree& tree)
{
    auto data = std::make_shared<mp::CaptureData>();
    data->name = tree.getProperty ("name").toString().toStdString();
    data->modes.refHz = juce::jlimit (1.0f, 20000.0f, (float) tree.getProperty ("refHz", 100.0f));
    data->residualRate = juce::jlimit (1000.0, 768000.0, (double) tree.getProperty ("rate", 48000.0));
    data->residualForceNorm = (float) tree.getProperty ("forceNorm", 1.0f);
    data->seconds = (float) tree.getProperty ("seconds", 0.0f);

    const auto modes = textToFloats (tree.getProperty ("modes").toString());
    for (size_t i = 0; i + 2 < modes.size() && data->modes.modes.size() < (size_t) mp::kMaxModes; i += 3)
        if (std::isfinite (modes[i]) && std::isfinite (modes[i + 1]) && std::isfinite (modes[i + 2]) && modes[i] > 0.0f)
            data->modes.modes.push_back ({ modes[i], modes[i + 1], juce::jlimit (0.005f, 60.0f, modes[i + 2]) });

    data->residual = textToFloats (tree.getProperty ("residual").toString());
    for (auto& v : data->residual)
        if (! std::isfinite (v))
            v = 0.0f;
    data->overview = textToFloats (tree.getProperty ("overview").toString());

    if (data->modes.modes.empty())
        return nullptr;
    return data;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MaterialPercussionProcessor();
}
