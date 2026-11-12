#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace ui
{
struct Theme
{
    juce::String name;
    juce::Colour background, panel, panelEdge, text, textDim, accent, accent2, track;
};

inline const std::vector<Theme>& getThemes()
{
    using C = juce::Colour;
    static const std::vector<Theme> themes {
        //  name     background      panel           edge            text            textDim         accent          accent2         track
        { "Ember", C (0xff16110f), C (0xff221a17), C (0xff3a2c26), C (0xfff3e9e2), C (0xffa08f85), C (0xffff7a3d), C (0xffffd166), C (0xff3b2d27) },
        { "Ocean", C (0xff0b1220), C (0xff121b2e), C (0xff22304d), C (0xffe6eefc), C (0xff8a9bbd), C (0xff3ec6ff), C (0xff7cf5d6), C (0xff22304d) },
        { "Moss",  C (0xff10140f), C (0xff182016), C (0xff2b3827), C (0xffe9f1e4), C (0xff93a58b), C (0xff9bd65b), C (0xffe5d36a), C (0xff2b3827) },
        { "Paper", C (0xffefe9df), C (0xfff8f4ed), C (0xffd6cdbf), C (0xff2a2420), C (0xff7b6f64), C (0xffd1495b), C (0xff2e86ab), C (0xffddd3c4) },
        { "Neon",  C (0xff0d0a14), C (0xff171126), C (0xff2e2347), C (0xfff0eaff), C (0xff9a8fbf), C (0xffff3ea5), C (0xff3effd8), C (0xff2e2347) },
    };
    return themes;
}

//==============================================================================
class MPLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MPLookAndFeel() { setTheme (getThemes().front()); }

    const Theme& getTheme() const noexcept { return theme; }

    void setTheme (const Theme& t)
    {
        theme = t;
        setColour (juce::ResizableWindow::backgroundColourId, t.background);
        setColour (juce::Label::textColourId, t.text);
        setColour (juce::Slider::textBoxTextColourId, t.textDim);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, t.accent.withAlpha (0.4f));
        setColour (juce::Slider::rotarySliderFillColourId, t.accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, t.track);
        setColour (juce::TextButton::buttonColourId, t.panel);
        setColour (juce::TextButton::buttonOnColourId, t.accent);
        setColour (juce::TextButton::textColourOffId, t.textDim);
        setColour (juce::TextButton::textColourOnId, t.background);
        setColour (juce::ComboBox::backgroundColourId, t.panel);
        setColour (juce::ComboBox::textColourId, t.text);
        setColour (juce::ComboBox::outlineColourId, t.panelEdge);
        setColour (juce::ComboBox::arrowColourId, t.textDim);
        setColour (juce::PopupMenu::backgroundColourId, t.panel);
        setColour (juce::PopupMenu::textColourId, t.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent);
        setColour (juce::PopupMenu::highlightedTextColourId, t.background);
        setColour (juce::ToggleButton::textColourId, t.textDim);
        setColour (juce::ToggleButton::tickColourId, t.accent);
        setColour (juce::ToggleButton::tickDisabledColourId, t.textDim);
        setColour (juce::TextEditor::backgroundColourId, t.panel);
        setColour (juce::TextEditor::textColourId, t.text);
        setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.4f));
        setColour (juce::TextEditor::outlineColourId, t.panelEdge);
        setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
        setColour (juce::CaretComponent::caretColourId, t.accent);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float lineW = juce::jmax (3.0f, radius * 0.13f);
        const float arcR = radius - lineW * 0.5f;
        const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (theme.track);
        g.strokePath (track, stroke);

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (theme.accent);
        g.strokePath (value, stroke);

        const float bodyR = arcR - lineW * 1.4f;
        g.setGradientFill (juce::ColourGradient (theme.panelEdge.brighter (0.25f), centre.x, centre.y - bodyR,
                                                 theme.panel, centre.x, centre.y + bodyR, false));
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre));
        g.setColour (theme.panelEdge);
        g.drawEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre), 1.0f);

        const auto p1 = centre.getPointOnCircumference (bodyR * 0.3f, angle);
        const auto p2 = centre.getPointOnCircumference (bodyR * 0.85f, angle);
        g.setColour (theme.text);
        g.drawLine ({ p1, p2 }, 2.5f);
    }

private:
    Theme theme;
};
} // namespace ui
