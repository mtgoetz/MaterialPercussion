#pragma once

#include "Theme.h"
#include "dsp/Capture.h"
#include "dsp/VoiceSettings.h"

#include <functional>
#include <memory>

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
    std::function<void()> onLoadRequest; // clicked the empty Captured drop zone

    void setTheme (const Theme& t);
    void update (mp::Model model, float shape, float position, float energy,
                 mp::ExciterType exciter, const juce::String& bodyName);

    // What the Captured model shows: the recording's envelope, or the drop zone.
    void setCapture (const std::shared_ptr<const mp::CaptureData>& data, const juce::String& message, bool busy);
    void setDropHighlight (bool on);

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
    void drawString (juce::Graphics&, const Geometry&, bool wire);
    void drawTube (juce::Graphics&, const Geometry&);
    void drawCaptured (juce::Graphics&, const Geometry&);
    void drawMarker (juce::Graphics&, juce::Point<float>, float alpha, bool solid);
    void drawExciter (juce::Graphics&, juce::Point<float>);

    Theme theme;
    mp::Model model = mp::Model::Membrane;
    mp::ExciterType exciter = mp::ExciterType::Mallet;
    juce::String bodyName;
    float shape = 0.0f, position = 0.3f, energy = 0.0f, phase = 0.0f;
    float hoverPosition = -1.0f;

    // Captured model
    int captureId = -1;
    std::vector<float> overview;
    juce::String captureName, captureMessage;
    bool captureBusy = false, dropHighlight = false;
};
} // namespace ui
