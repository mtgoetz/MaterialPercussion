#pragma once

#include "Theme.h"
#include "dsp/ModeModels.h"

#include <functional>

namespace ui
{
// Draws the struck object for the current model (drum head, bar, plate, glass, bell,
// string) with its strike point. Clicking the object moves the strike position there
// and plays a hit.
class ObjectView : public juce::Component
{
public:
    ObjectView();

    std::function<void (float position)> onStrike;

    void setTheme (const Theme& t);
    void update (mp::Model model, float shape, float position, float energy);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct Geometry
    {
        juce::Rectangle<float> area;
        juce::Point<float> centre;
        float radius = 0.0f;           // membrane
        juce::Rectangle<float> rect;   // bar, plate
        float x0 = 0.0f, x1 = 0.0f;    // string ends
        float y0 = 0.0f, y1 = 0.0f;    // glass rim/bottom, bell crown/lip
        float halfWidth = 0.0f;        // glass, bell
    };

    Geometry geometry() const;
    juce::Point<float> strikePointFor (float position) const;
    float positionFor (juce::Point<float> point) const;

    void drawMembrane (juce::Graphics&, const Geometry&);
    void drawWood (juce::Graphics&, const Geometry&);
    void drawMetal (juce::Graphics&, const Geometry&);
    void drawGlass (juce::Graphics&, const Geometry&);
    void drawBell (juce::Graphics&, const Geometry&);
    void drawString (juce::Graphics&, const Geometry&);
    void drawMarker (juce::Graphics&, juce::Point<float>, float alpha, bool solid);

    Theme theme;
    mp::Model model = mp::Model::Membrane;
    float shape = 0.0f, position = 0.3f, energy = 0.0f, phase = 0.0f;
    float hoverPosition = -1.0f;
};
} // namespace ui
