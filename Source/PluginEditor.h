#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Knob.h"
#include "ui/ModeDisplay.h"
#include "ui/ObjectView.h"
#include "ui/Theme.h"

class MaterialPercussionEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MaterialPercussionEditor (MaterialPercussionProcessor&);
    ~MaterialPercussionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyTheme (int index);
    void selectModel (int index);
    void strikeAt (float position);
    juce::String currentHint() const;
    ui::Knob* addKnob (juce::OwnedArray<ui::Knob>& row, const char* paramId, const char* name, const char* hint);

    MaterialPercussionProcessor& proc;
    ui::MPLookAndFeel lnf; // declared before the components so it outlives them

    juce::OwnedArray<juce::TextButton> modelButtons;
    juce::ComboBox themeBox;
    ui::ObjectView objectView;
    ui::ModeDisplay modeDisplay;
    juce::OwnedArray<ui::Knob> objectKnobs, strikeKnobs;
    ui::Knob* shapeKnob = nullptr;
    juce::ToggleButton keyTrackButton { "Key track" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keyTrackAttachment;

    juce::Rectangle<int> headerArea, objectHeader, strikeHeader;

    mp::ModeSet displayModes;
    mp::ModeParams displayParams;
    bool haveDisplayModes = false;
    std::array<float, mp::kMaxModes> shownLevels {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaterialPercussionEditor)
};
