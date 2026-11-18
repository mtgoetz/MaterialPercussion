#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Knob.h"
#include "ui/ModeDisplay.h"
#include "ui/ObjectView.h"
#include "ui/Theme.h"

class MaterialPercussionEditor : public juce::AudioProcessorEditor,
                                 public juce::FileDragAndDropTarget,
                                 private juce::Timer
{
public:
    explicit MaterialPercussionEditor (MaterialPercussionProcessor&);
    ~MaterialPercussionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Dropping a sound file anywhere on the plugin captures it.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void chooseCaptureFile();
    struct Section
    {
        juce::String title;
        std::vector<ui::Knob*> knobs;
        juce::Rectangle<int> header;
        int headerReserve = 0; // pixels at the right of the header taken by controls
    };

    void timerCallback() override;
    void applyTheme (int index);
    void setChoice (const char* paramId, int index);
    void strikeAt (float position);
    juce::String currentHint() const;
    ui::Knob* addKnob (Section& section, const char* paramId, const char* name, const char* hint);
    void layoutSection (Section& section, juce::Rectangle<int> area);
    juce::OwnedArray<juce::TextButton> makeSegmented (const juce::StringArray& names, std::function<void (int)> onPick);

    MaterialPercussionProcessor& proc;
    ui::MPLookAndFeel lnf; // declared before the components so it outlives them

    juce::OwnedArray<juce::TextButton> modelButtons, exciterButtons;
    juce::ComboBox themeBox, bodyBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bodyAttachment;
    ui::ObjectView objectView;
    ui::ModeDisplay modeDisplay;
    juce::ToggleButton keyTrackButton { "Key track" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keyTrackAttachment;

    juce::OwnedArray<ui::Knob> knobs;
    Section objectSection { "OBJECT" }, exciterSection { "EXCITER" }, nonlinearSection { "NONLINEAR" },
            bodySection { "BODY" }, outputSection { "OUTPUT" };
    ui::Knob* shapeKnob = nullptr;
    ui::Knob* hardnessKnob = nullptr;
    ui::Knob* colorKnob = nullptr;
    ui::Knob* lengthKnob = nullptr;
    ui::Knob* bounceKnob = nullptr;
    ui::Knob* noiseKnob = nullptr;
    ui::Knob* decayKnob = nullptr;
    ui::Knob* dampingKnob = nullptr;
    ui::Knob* positionKnob = nullptr;

    juce::TextButton loadButton { "Load sound..." };
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::shared_ptr<const mp::CaptureData> displayCapture; // keeps displayParams' capture alive

    juce::Rectangle<int> headerArea;

    mp::ModeSet displayModes, displayBodyModes;
    mp::ModeParams displayParams, displayBodyParams;
    bool displayBodyOn = false, haveDisplayModes = false;
    std::array<float, mp::kMaxModes> shownLevels {};
    std::array<float, mp::Voice::kBodyModes> shownBodyLevels {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaterialPercussionEditor)
};
