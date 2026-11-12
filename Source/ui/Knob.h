#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ui
{
// A labelled rotary knob attached to a parameter, with a one-line hint the editor
// shows in the mode display while the knob is hovered or dragged.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
          const juce::String& labelText, const juce::String& hintText)
        : hint (hintText)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        addAndMakeVisible (slider);

        label.setText (labelText, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (juce::FontOptions (13.0f)).boldened());
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

        if (auto* param = state.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }

    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds (b.removeFromTop (18));
        slider.setBounds (b);
    }

    void setLabel (const juce::String& text)
    {
        if (label.getText() != text)
            label.setText (text, juce::dontSendNotification);
    }

    juce::String getLabel() const { return label.getText(); }
    bool isHovered() const { return isMouseOverOrDragging (true); }

    juce::String hint;

private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
} // namespace ui
