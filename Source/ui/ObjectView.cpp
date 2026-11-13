#include "ObjectView.h"

namespace ui
{
namespace
{
constexpr float kPi = 3.14159265f;
constexpr float kMembraneAngle = -0.6f; // strike direction from the head centre

// Glass bowl half-width; t = 0 at the bowl bottom, 1 at the rim.
float glassProfile (float t) { return std::pow (juce::jlimit (0.0f, 1.0f, t), 0.55f); }

// Bell half-width; u = 0 at the crown, 1 at the lip.
float bellProfile (float u, float shape) { return 0.30f + 0.12f * u + 0.5f * std::pow (u, 5.0f) * (0.8f + 0.4f * shape); }

float glassStrikeTop (float y0, float y1)    { return y0 + (y1 - y0) * 0.04f; }
float glassStrikeBottom (float y0, float y1) { return y1 - (y1 - y0) * 0.12f; }
} // namespace

ObjectView::ObjectView()
{
    theme = getThemes().front();
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void ObjectView::setTheme (const Theme& t)
{
    theme = t;
    repaint();
}

void ObjectView::update (mp::Model newModel, float newShape, float newPosition, float newEnergy,
                         mp::ExciterType newExciter, const juce::String& newBodyName)
{
    const bool changed = newModel != model || newShape != shape || newPosition != position
                      || newExciter != exciter || newBodyName != bodyName;
    const bool animating = newEnergy > 0.002f || energy > 0.002f;

    model = newModel;
    shape = newShape;
    position = newPosition;
    energy = newEnergy;
    exciter = newExciter;
    bodyName = newBodyName;
    phase += 1.9f;

    if (changed || animating)
        repaint();
}

//==============================================================================
ObjectView::Geometry ObjectView::geometry() const
{
    Geometry geo;
    geo.area = getLocalBounds().toFloat().reduced (24.0f).withTrimmedTop (20.0f).withTrimmedBottom (16.0f);
    geo.centre = geo.area.getCentre();
    const float w = geo.area.getWidth(), h = geo.area.getHeight();

    switch (model)
    {
        case mp::Model::Membrane:
            geo.radius = std::min (w, h) * 0.44f;
            break;

        case mp::Model::Wood:
        {
            const float bw = w * 0.92f;
            const float bh = std::min (h * 0.26f, bw * 0.2f);
            geo.rect = juce::Rectangle<float> (bw, bh).withCentre (geo.centre);
            break;
        }

        case mp::Model::Metal:
        {
            const float a = (float) mp::plateAspect (shape);
            float pw = w * 0.88f, ph = pw / a;
            if (ph > h * 0.88f) { ph = h * 0.88f; pw = ph * a; }
            geo.rect = juce::Rectangle<float> (pw, ph).withCentre (geo.centre);
            break;
        }

        case mp::Model::Glass:
            geo.y0 = geo.area.getY() + h * 0.04f;
            geo.y1 = geo.area.getY() + h * 0.60f;
            geo.halfWidth = std::min (w * 0.34f, h * 0.32f);
            break;

        case mp::Model::Bell:
            geo.y0 = geo.area.getY() + h * 0.12f;
            geo.y1 = geo.area.getY() + h * 0.86f;
            geo.halfWidth = std::min (w * 0.42f, h * 0.40f);
            break;

        case mp::Model::String:
        case mp::Model::Wire:
            geo.x0 = geo.area.getX() + w * 0.04f;
            geo.x1 = geo.area.getRight() - w * 0.04f;
            break;

        case mp::Model::Tube:
        {
            const float tw = w * 0.9f;
            const float th = std::min (h * 0.24f, tw * 0.16f);
            geo.rect = juce::Rectangle<float> (tw, th).withCentre (geo.centre);
            break;
        }
    }
    return geo;
}

juce::Point<float> ObjectView::strikePointFor (float pos) const
{
    const auto geo = geometry();
    switch (model)
    {
        case mp::Model::Membrane:
        {
            const float r = geo.radius * (float) mp::membraneRadius (pos);
            return geo.centre + juce::Point<float> (std::cos (kMembraneAngle), std::sin (kMembraneAngle)) * r;
        }
        case mp::Model::Wood:
            return { geo.rect.getX() + geo.rect.getWidth() * (float) mp::barX (pos), geo.rect.getCentreY() };

        case mp::Model::Metal:
            return { geo.rect.getX() + geo.rect.getWidth() * (float) mp::plateX (pos),
                     geo.rect.getY() + geo.rect.getHeight() * (float) mp::plateY (pos) };

        case mp::Model::Glass:
        {
            const float y = juce::jmap (pos, glassStrikeBottom (geo.y0, geo.y1), glassStrikeTop (geo.y0, geo.y1));
            const float t = (geo.y1 - y) / (geo.y1 - geo.y0);
            return { geo.centre.x + geo.halfWidth * glassProfile (t), y };
        }
        case mp::Model::Bell:
        {
            const float u = 0.35f + 0.6f * pos;
            return { geo.centre.x + geo.halfWidth * bellProfile (u, shape), juce::jmap (u, geo.y0, geo.y1) };
        }
        case mp::Model::String:
            return { juce::jmap ((float) mp::stringX (pos), geo.x0, geo.x1), geo.centre.y };

        case mp::Model::Wire:
            return { juce::jmap ((float) mp::wireX (pos), geo.x0, geo.x1), geo.centre.y };

        case mp::Model::Tube:
            return { geo.rect.getX() + geo.rect.getWidth() * (float) mp::tubeX (pos), geo.rect.getY() };
    }
    return geo.centre;
}

float ObjectView::positionFor (juce::Point<float> pt) const
{
    const auto geo = geometry();
    float p = 0.0f;
    switch (model)
    {
        case mp::Model::Membrane:
            p = (pt.getDistanceFrom (geo.centre) / geo.radius - 0.02f) / 0.9f;
            break;

        case mp::Model::Wood:
            p = std::abs (0.5f - (pt.x - geo.rect.getX()) / geo.rect.getWidth()) / 0.48f;
            break;

        case mp::Model::Metal:
        {
            const float xn = (pt.x - geo.rect.getX()) / geo.rect.getWidth();
            const float yn = (pt.y - geo.rect.getY()) / geo.rect.getHeight();
            p = (std::abs (0.5f - xn) * 0.42f + std::abs (0.5f - yn) * 0.31f) / (0.42f * 0.42f + 0.31f * 0.31f);
            break;
        }
        case mp::Model::Glass:
        {
            const float bottom = glassStrikeBottom (geo.y0, geo.y1), top = glassStrikeTop (geo.y0, geo.y1);
            p = (pt.y - bottom) / (top - bottom);
            break;
        }
        case mp::Model::Bell:
            p = ((pt.y - geo.y0) / (geo.y1 - geo.y0) - 0.35f) / 0.6f;
            break;

        case mp::Model::String:
        case mp::Model::Wire:
            p = std::abs (0.5f - (pt.x - geo.x0) / (geo.x1 - geo.x0)) / 0.48f;
            break;

        case mp::Model::Tube:
            p = std::abs (0.5f - (pt.x - geo.rect.getX()) / geo.rect.getWidth()) / 0.46f;
            break;
    }
    return juce::jlimit (0.0f, 1.0f, p);
}

//==============================================================================
void ObjectView::mouseDown (const juce::MouseEvent& e)
{
    if (onStrike != nullptr)
        onStrike (positionFor (e.position));
}

void ObjectView::mouseMove (const juce::MouseEvent& e)
{
    hoverPosition = positionFor (e.position);
    repaint();
}

void ObjectView::mouseExit (const juce::MouseEvent&)
{
    hoverPosition = -1.0f;
    repaint();
}

//==============================================================================
void ObjectView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (theme.panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (theme.panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    auto text = bounds.reduced (16.0f, 10.0f);
    g.setColour (theme.textDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
    g.drawText (juce::String (mp::getModelInfo (model).name).toUpperCase(), text.removeFromTop (16.0f),
                juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("click to strike", bounds.reduced (16.0f, 10.0f).removeFromTop (16.0f), juce::Justification::centredRight);

    const float shown = hoverPosition >= 0.0f ? hoverPosition : position;
    g.drawText ("Position " + juce::String (juce::roundToInt (shown * 100.0f)) + "%",
                text.removeFromBottom (14.0f), juce::Justification::centredLeft);

    const auto geo = geometry();
    {
        // A little shake while the object rings.
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::translation (0.0f, 2.5f * energy * std::sin (phase)));

        switch (model)
        {
            case mp::Model::Membrane: drawMembrane (g, geo); break;
            case mp::Model::Wood:     drawWood (g, geo);     break;
            case mp::Model::Metal:    drawMetal (g, geo);    break;
            case mp::Model::Glass:    drawGlass (g, geo);    break;
            case mp::Model::Bell:     drawBell (g, geo);     break;
            case mp::Model::String:   drawString (g, geo, false); break;
            case mp::Model::Wire:     drawString (g, geo, true);  break;
            case mp::Model::Tube:     drawTube (g, geo);          break;
        }
    }

    if (hoverPosition >= 0.0f)
        drawMarker (g, strikePointFor (hoverPosition), 0.45f, false);
    drawExciter (g, strikePointFor (position));
    drawMarker (g, strikePointFor (position), 1.0f, true);

    // Exciter name by the strike point's corner, coupled body in the other corner
    auto footer = bounds.reduced (16.0f, 10.0f).removeFromBottom (14.0f);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    if (bodyName.isNotEmpty())
    {
        g.setColour (theme.accent2.withAlpha (0.9f));
        g.drawText ("+ " + bodyName + " body", footer, juce::Justification::centredRight);
    }
    g.setColour (theme.accent.withAlpha (0.9f));
    g.drawText (juce::String (mp::getExciterInfo (exciter).name).toUpperCase(),
                bounds.reduced (16.0f, 10.0f).removeFromTop (34.0f).removeFromBottom (14.0f), juce::Justification::centredLeft);
}

void ObjectView::drawExciter (juce::Graphics& g, juce::Point<float> p)
{
    const auto col = theme.accent.withAlpha (0.85f);
    g.setColour (col);
    switch (exciter)
    {
        case mp::ExciterType::Mallet:
        {
            // Mallet shaft and head, resting above the strike point
            const auto head = p + juce::Point<float> (14.0f, -16.0f);
            g.drawLine ({ head, head + juce::Point<float> (26.0f, -22.0f) }, 2.5f);
            g.drawEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (head), 2.0f);
            break;
        }
        case mp::ExciterType::Bow:
        {
            // Bow hair across the contact point, wobbling while it plays
            const float wobble = 3.0f * energy * std::sin (phase * 0.7f);
            g.drawLine (p.x - 46.0f, p.y - 10.0f + wobble, p.x + 46.0f, p.y + 10.0f + wobble, 2.0f);
            g.setColour (col.withAlpha (0.35f));
            g.drawLine (p.x - 46.0f, p.y - 16.0f + wobble, p.x + 46.0f, p.y + 4.0f + wobble, 4.0f);
            break;
        }
        case mp::ExciterType::Scrape:
        {
            juce::Path zig;
            zig.startNewSubPath (p.x - 30.0f, p.y - 12.0f);
            for (int i = 1; i <= 8; ++i)
                zig.lineTo (p.x - 30.0f + 7.5f * (float) i, p.y - 12.0f + ((i & 1) ? -6.0f : 0.0f));
            g.strokePath (zig, juce::PathStrokeType (1.6f));
            break;
        }
        case mp::ExciterType::Particles:
        {
            juce::Random rng (1234 + (int) (phase * 0.3f) * (energy > 0.01f ? 1 : 0));
            for (int i = 0; i < 14; ++i)
            {
                const float a = rng.nextFloat() * 6.2832f, r = 8.0f + rng.nextFloat() * 22.0f;
                g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (p + juce::Point<float> (std::cos (a), std::sin (a) - 0.6f) * r));
            }
            break;
        }
        case mp::ExciterType::Feedback:
        {
            juce::Path loop;
            loop.addCentredArc (p.x, p.y, 20.0f, 20.0f, 0.0f, 0.5f, 5.6f, true);
            g.strokePath (loop, juce::PathStrokeType (1.8f));
            const auto tip = p.getPointOnCircumference (20.0f, 5.6f);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (tip));
            break;
        }
    }
}

void ObjectView::drawTube (juce::Graphics& g, const Geometry& geo)
{
    const auto r = geo.rect;
    const float stopped = 1.0f - shape; // how closed the left end is

    g.setGradientFill (juce::ColourGradient (theme.accent2.withAlpha (0.30f + 0.2f * energy), r.getX(), r.getY(),
                                             theme.accent2.withAlpha (0.06f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, r.getHeight() * 0.18f);

    // Air column standing wave
    juce::Path wave;
    const float amp = r.getHeight() * 0.3f * (0.2f + energy);
    for (int i = 0; i <= 60; ++i)
    {
        const float u = (float) i / 60.0f;
        const float y = r.getCentreY() + amp * std::cos (kPi * u * (stopped > 0.5f ? 0.5f : 1.0f)) * std::sin (phase);
        if (i == 0) wave.startNewSubPath (r.getX() + u * r.getWidth(), y);
        else        wave.lineTo (r.getX() + u * r.getWidth(), y);
    }
    g.setColour (theme.textDim.withAlpha (0.35f));
    g.strokePath (wave, juce::PathStrokeType (1.2f));

    g.setColour (theme.accent2.withAlpha (0.85f));
    g.drawLine (r.getX(), r.getY(), r.getRight(), r.getY(), 2.0f + 1.5f * energy);
    g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getBottom(), 2.0f + 1.5f * energy);

    // End cap: solid when stopped, open rim when open
    g.setColour (theme.accent2.withAlpha (0.25f + 0.65f * stopped));
    g.fillRect (juce::Rectangle<float> (3.0f + 6.0f * stopped, r.getHeight()).withPosition (r.getX() - 3.0f, r.getY()));
    g.setColour (theme.accent2.withAlpha (0.7f));
    g.drawEllipse (juce::Rectangle<float> (10.0f, r.getHeight()).withCentre ({ r.getRight(), r.getCentreY() }), 1.5f);
}

void ObjectView::drawMarker (juce::Graphics& g, juce::Point<float> p, float alpha, bool solid)
{
    if (solid)
    {
        const float glow = 10.0f + 18.0f * energy;
        g.setColour (theme.accent.withAlpha (0.15f + 0.35f * energy));
        g.fillEllipse (juce::Rectangle<float> (glow * 2.0f, glow * 2.0f).withCentre (p));
        g.setColour (theme.accent);
        g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (p));
        g.setColour (theme.background);
        g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (p), 1.5f);
    }
    else
    {
        g.setColour (theme.accent.withAlpha (alpha));
        g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (p), 1.5f);
    }
}

void ObjectView::drawMembrane (juce::Graphics& g, const Geometry& geo)
{
    const auto c = geo.centre;
    const float r = geo.radius;
    auto circle = [c] (float radius) { return juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c); };

    // Kettle shell shows through as the head gets "tuned" towards a timpani.
    if (shape > 0.01f)
    {
        g.setColour (theme.textDim.withAlpha (0.35f * shape));
        g.drawEllipse (circle (r * 1.07f), 2.0f + 5.0f * shape);
    }

    g.setColour (theme.accent2.withAlpha (0.14f + 0.2f * energy));
    g.fillEllipse (circle (r));

    g.setColour (theme.textDim.withAlpha (0.18f));
    for (float k : { 0.33f, 0.66f })
        g.drawEllipse (circle (r * k), 1.0f);

    g.setColour (theme.accent2.withAlpha (0.85f));
    g.drawEllipse (circle (r), 2.5f + 2.0f * energy);

    g.setColour (theme.textDim.withAlpha (0.6f));
    for (int i = 0; i < 8; ++i)
    {
        const auto lug = c.getPointOnCircumference (r + 7.0f, (float) i * kPi / 4.0f);
        g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, 6.0f).withCentre (lug), 1.5f);
    }
}

void ObjectView::drawWood (juce::Graphics& g, const Geometry& geo)
{
    const auto r = geo.rect;
    const float undercut = r.getHeight() * 0.55f * shape;

    juce::Path bar;
    bar.startNewSubPath (r.getX(), r.getY());
    bar.lineTo (r.getRight(), r.getY());
    bar.lineTo (r.getRight(), r.getBottom());
    bar.lineTo (r.getX() + r.getWidth() * 0.78f, r.getBottom());
    bar.quadraticTo (r.getCentreX(), r.getBottom() - undercut * 2.0f, r.getX() + r.getWidth() * 0.22f, r.getBottom());
    bar.lineTo (r.getX(), r.getBottom());
    bar.closeSubPath();

    g.setColour (theme.accent2.withAlpha (0.18f + 0.2f * energy));
    g.fillPath (bar);

    // Grain
    g.setColour (theme.textDim.withAlpha (0.22f));
    for (int i = 1; i <= 3; ++i)
    {
        const float y = r.getY() + r.getHeight() * 0.22f * (float) i;
        g.drawLine (r.getX() + 8.0f * (float) i, y, r.getRight() - 14.0f * (float) i, y + 1.5f, 1.0f);
    }

    g.setColour (theme.accent2.withAlpha (0.85f));
    g.strokePath (bar, juce::PathStrokeType (2.0f + 1.5f * energy));

    // Support cords sit at the first mode's nodes.
    g.setColour (theme.textDim.withAlpha (0.7f));
    for (float xn : { 0.224f, 0.776f })
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getX() + r.getWidth() * xn, r.getCentreY() }));
}

void ObjectView::drawMetal (juce::Graphics& g, const Geometry& geo)
{
    const auto r = geo.rect;
    g.setGradientFill (juce::ColourGradient (theme.accent2.withAlpha (0.32f + 0.2f * energy), r.getTopLeft(),
                                             theme.accent2.withAlpha (0.05f), r.getBottomRight(), false));
    g.fillRoundedRectangle (r, 3.0f);

    // Sheen
    g.setColour (theme.text.withAlpha (0.08f));
    g.drawLine (r.getX() + r.getWidth() * 0.15f, r.getBottom() - 6.0f, r.getX() + r.getWidth() * 0.55f, r.getY() + 6.0f, 10.0f);

    g.setColour (theme.accent2.withAlpha (0.85f));
    g.drawRoundedRectangle (r, 3.0f, 2.0f + 1.5f * energy);

    g.setColour (theme.textDim.withAlpha (0.6f));
    for (auto corner : { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() })
    {
        const auto inset = corner + (r.getCentre() - corner) * (10.0f / r.getCentre().getDistanceFrom (corner));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (inset));
    }
}

void ObjectView::drawGlass (juce::Graphics& g, const Geometry& geo)
{
    constexpr int steps = 40;
    const float cx = geo.centre.x;
    auto yAt = [&] (float t) { return juce::jmap (t, geo.y1, geo.y0); };

    juce::Path bowl;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = 1.0f - (float) i / steps;
        const juce::Point<float> p { cx + geo.halfWidth * glassProfile (t), yAt (t) };
        if (i == 0) bowl.startNewSubPath (p);
        else        bowl.lineTo (p);
    }
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        bowl.lineTo (cx - geo.halfWidth * glassProfile (t), yAt (t));
    }

    g.setColour (theme.accent2.withAlpha (0.12f + 0.2f * energy));
    g.fillPath (bowl);

    const float wall = 1.5f + 4.0f * shape;
    g.setColour (theme.accent2.withAlpha (0.85f));
    g.strokePath (bowl, juce::PathStrokeType (wall + 1.5f * energy));
    g.drawEllipse (juce::Rectangle<float> (geo.halfWidth * 2.0f, 10.0f).withCentre ({ cx, geo.y0 }), wall);

    // Stem and foot
    const float footY = geo.area.getBottom() - 6.0f;
    g.drawLine (cx, geo.y1, cx, footY, 2.5f);
    g.drawEllipse (juce::Rectangle<float> (geo.halfWidth * 1.3f, 10.0f).withCentre ({ cx, footY }), 2.0f);
}

void ObjectView::drawBell (juce::Graphics& g, const Geometry& geo)
{
    constexpr int steps = 40;
    const float cx = geo.centre.x;
    auto yAt = [&] (float u) { return juce::jmap (u, geo.y0, geo.y1); };
    auto hwAt = [&] (float u) { return geo.halfWidth * bellProfile (u, shape); };

    juce::Path bell;
    bell.startNewSubPath (cx - hwAt (0.0f), yAt (0.0f));
    bell.quadraticTo (cx, yAt (0.0f) - geo.halfWidth * 0.45f, cx + hwAt (0.0f), yAt (0.0f));
    for (int i = 1; i <= steps; ++i)
    {
        const float u = (float) i / steps;
        bell.lineTo (cx + hwAt (u), yAt (u));
    }
    bell.quadraticTo (cx, yAt (1.0f) + geo.halfWidth * 0.18f, cx - hwAt (1.0f), yAt (1.0f));
    for (int i = steps - 1; i >= 0; --i)
    {
        const float u = (float) i / steps;
        bell.lineTo (cx - hwAt (u), yAt (u));
    }
    bell.closeSubPath();

    g.setColour (theme.accent2.withAlpha (0.16f + 0.2f * energy));
    g.fillPath (bell);

    // Sound-bow band
    g.setColour (theme.textDim.withAlpha (0.3f));
    g.drawLine (cx - hwAt (0.82f), yAt (0.82f), cx + hwAt (0.82f), yAt (0.82f), 1.5f);

    g.setColour (theme.accent2.withAlpha (0.85f));
    g.strokePath (bell, juce::PathStrokeType (2.0f + 1.5f * energy));
}

void ObjectView::drawString (juce::Graphics& g, const Geometry& geo, bool wire)
{
    if (wire)
    {
        // Tuning-peg end and the buzzing bridge of a waveguide wire
        g.setColour (theme.textDim.withAlpha (0.4f));
        g.fillRoundedRectangle (juce::Rectangle<float> (14.0f, 30.0f).withCentre ({ geo.x0 - 4.0f, geo.centre.y }), 3.0f);
    }

    constexpr int steps = 80;
    const float y = geo.centre.y;
    const float amp = energy * geo.area.getHeight() * 0.12f * std::sin (phase);

    g.setColour (theme.textDim.withAlpha (0.6f));
    for (float x : { geo.x0, geo.x1 })
    {
        juce::Path bridge;
        bridge.addTriangle (x - 8.0f, y + 16.0f, x + 8.0f, y + 16.0f, x, y);
        g.fillPath (bridge);
    }

    juce::Path str;
    for (int i = 0; i <= steps; ++i)
    {
        const float u = (float) i / steps;
        const float offset = amp * std::sin (kPi * u) + 0.4f * amp * std::sin (2.0f * kPi * u) * std::cos (phase * 1.7f);
        const juce::Point<float> p { juce::jmap (u, geo.x0, geo.x1), y + offset };
        if (i == 0) str.startNewSubPath (p);
        else        str.lineTo (p);
    }
    g.setColour (wire ? theme.text.withAlpha (0.85f) : theme.accent2.withAlpha (0.9f));
    const float thickness = wire ? 1.0f + 2.5f * shape : 1.5f + 4.0f * shape;
    g.strokePath (str, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace ui
