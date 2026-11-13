#include "ModeDisplay.h"

namespace ui
{
namespace
{
constexpr float kMinHz = 20.0f;
constexpr float kMaxHz = 20000.0f;
constexpr float kFloorDb = -60.0f;
} // namespace

ModeDisplay::ModeDisplay()
{
    theme = getThemes().front();
    setInterceptsMouseClicks (false, false);
}

void ModeDisplay::setTheme (const Theme& t)
{
    theme = t;
    repaint();
}

void ModeDisplay::setHint (const juce::String& text)
{
    if (hint != text)
    {
        hint = text;
        repaint();
    }
}

void ModeDisplay::update (const State& s)
{
    modes = *s.modes;
    baseHz = s.baseHz;
    hasBody = s.bodyModes != nullptr;
    if (hasBody)
        bodyModes = *s.bodyModes;
    bodyHz = s.bodyHz;
    exciter = s.exciter;
    hardness = s.hardness;
    color = s.color;
    std::copy (s.liveLevels, s.liveLevels + mp::kMaxModes, levels.begin());
    std::copy (s.bodyLevels, s.bodyLevels + kMaxBodyModes, bodyLevels.begin());
    repaint();
}

float ModeDisplay::exciterResponse (float hz) const
{
    return mp::exciterSpectrum (exciter, hz, hardness, color);
}

void ModeDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (theme.panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (theme.panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    auto area = bounds.reduced (16.0f, 10.0f);

    // Header: title + hint about whatever the user is touching
    auto header = area.removeFromTop (18.0f);
    g.setColour (theme.textDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
    g.drawText ("MODES", header.removeFromLeft (60.0f), juce::Justification::centredLeft);
    g.setColour (theme.text);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawFittedText (hint, header.toNearestInt(), juce::Justification::centredRight, 1);

    // Legend
    auto legend = area.removeFromBottom (14.0f);
    g.setColour (theme.textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (hasBody ? "bar = loudness per hit   dot = ring time   glow = ringing now   diamond = body"
                        : "bar = loudness per hit   dot = ring time   glow = ringing now",
                legend, juce::Justification::centredLeft);
    g.setColour (theme.accent2.withAlpha (0.6f));
    g.drawText (juce::String (mp::getExciterInfo (exciter).name).toLowerCase() + " spectrum", legend, juce::Justification::centredRight);

    const auto plot = area.reduced (4.0f, 8.0f);
    const float logSpan = std::log (kMaxHz / kMinHz);
    auto xFor = [&] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / kMinHz) / logSpan; };
    auto yForDb = [&] (float db) { return juce::jmap (juce::jlimit (kFloorDb, 0.0f, db), kFloorDb, 0.0f, plot.getBottom(), plot.getY()); };

    // Grid
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xFor (hz);
        g.setColour (theme.panelEdge);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (theme.textDim.withAlpha (0.7f));
        const auto label = hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k" : juce::String (juce::roundToInt (hz));
        g.drawText (label, juce::Rectangle<float> (x + 3.0f, plot.getBottom() - 13.0f, 40.0f, 12.0f), juce::Justification::centredLeft);
    }
    for (float db : { -20.0f, -40.0f })
    {
        g.setColour (theme.panelEdge.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (yForDb (db)), plot.getX(), plot.getRight());
    }

    // Exciter spectrum
    juce::Path curve;
    curve.startNewSubPath (plot.getX(), plot.getBottom());
    for (float px = 0.0f; px <= plot.getWidth(); px += 2.0f)
    {
        const float hz = kMinHz * std::exp (logSpan * px / plot.getWidth());
        const float db = juce::Decibels::gainToDecibels (exciterResponse (hz), -80.0f);
        curve.lineTo (plot.getX() + px, yForDb (db));
    }
    curve.lineTo (plot.getRight(), plot.getBottom());
    curve.closeSubPath();
    g.setColour (theme.accent2.withAlpha (0.07f));
    g.fillPath (curve);
    g.setColour (theme.accent2.withAlpha (0.35f));
    g.strokePath (curve, juce::PathStrokeType (1.2f));

    // Modes
    for (int i = 0; i < modes.count; ++i)
    {
        const auto& m = modes.modes[(size_t) i];
        const float hz = baseHz * m.ratio;
        if (hz < kMinHz || hz > kMaxHz || m.amp == 0.0f)
            continue;

        const float x = xFor (hz);
        const float db = juce::Decibels::gainToDecibels (std::abs (m.amp) * exciterResponse (hz), -80.0f);
        const float top = yForDb (db);
        const float ring = juce::jlimit (0.0f, 1.0f, std::log (m.t60 / 0.02f) / std::log (1000.0f)); // 20 ms .. 20 s

        g.setColour (theme.accent.withAlpha (0.15f + 0.35f * ring));
        g.fillRect (juce::Rectangle<float> (x - 1.5f, top, 3.0f, plot.getBottom() - top));

        if (db > kFloorDb)
        {
            const float r = 2.0f + 3.5f * ring;
            g.setColour (theme.accent.withAlpha (0.45f + 0.55f * ring));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ x, top }));
        }

        const float live = levels[(size_t) i];
        if (live > 1.0e-3f)
        {
            const float ly = yForDb (juce::Decibels::gainToDecibels (live));
            g.setColour (theme.accent2.withAlpha (0.22f));
            g.fillRect (juce::Rectangle<float> (x - 4.5f, ly, 9.0f, plot.getBottom() - ly));
            g.setColour (theme.accent2.withAlpha (0.95f));
            g.fillRect (juce::Rectangle<float> (x - 1.5f, ly, 3.0f, plot.getBottom() - ly));
        }
    }

    // Body modes: diamonds (bigger = longer ring), glowing when they ring
    if (hasBody)
    {
        for (int i = 0; i < std::min (bodyModes.count, kMaxBodyModes); ++i)
        {
            const auto& m = bodyModes.modes[(size_t) i];
            const float hz = bodyHz * m.ratio;
            if (hz < kMinHz || hz > kMaxHz || m.amp == 0.0f)
                continue;

            const float x = xFor (hz);
            const float db = juce::Decibels::gainToDecibels (std::abs (m.amp), -80.0f) - 6.0f;
            if (db <= kFloorDb)
                continue;
            const float y = yForDb (db);
            const float ring = juce::jlimit (0.0f, 1.0f, std::log (m.t60 / 0.02f) / std::log (1000.0f));
            const float r = 2.5f + 3.0f * ring;
            const float live = bodyLevels[(size_t) i];

            juce::Path diamond;
            diamond.addQuadrilateral (x, y - r, x + r, y, x, y + r, x - r, y);
            g.setColour (theme.text.withAlpha (0.25f + 0.35f * ring));
            g.strokePath (diamond, juce::PathStrokeType (1.2f));
            g.drawVerticalLine (juce::roundToInt (x), y + r, plot.getBottom());

            if (live > 1.0e-3f)
            {
                const float ly = yForDb (juce::Decibels::gainToDecibels (live));
                g.setColour (theme.text.withAlpha (0.55f));
                g.fillRect (juce::Rectangle<float> (x - 1.0f, ly, 2.0f, plot.getBottom() - ly));
                g.fillPath (diamond);
            }
        }
    }
}
} // namespace ui
