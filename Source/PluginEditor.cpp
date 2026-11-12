#include "PluginEditor.h"

namespace
{
constexpr int kEditorWidth = 940;
constexpr int kEditorHeight = 680;
constexpr int kUiNote = 60;          // C3: plays at exactly the Pitch knob
constexpr float kUiVelocity = 0.85f;
} // namespace

MaterialPercussionEditor::MaterialPercussionEditor (MaterialPercussionProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    // Model selector (segmented buttons)
    for (int i = 0; i < mp::kNumModels; ++i)
    {
        auto* b = modelButtons.add (new juce::TextButton (mp::getModelInfo ((mp::Model) i).name));
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < mp::kNumModels - 1) edges |= juce::Button::ConnectedOnRight;
        b->setConnectedEdges (edges);
        b->onClick = [this, i] { selectModel (i); };
        addAndMakeVisible (b);
    }

    // Theme picker
    const auto& themes = ui::getThemes();
    for (int i = 0; i < (int) themes.size(); ++i)
        themeBox.addItem (themes[(size_t) i].name, i + 1);
    themeBox.onChange = [this] { applyTheme (themeBox.getSelectedItemIndex()); };
    addAndMakeVisible (themeBox);

    // Visualisation
    objectView.onStrike = [this] (float position) { strikeAt (position); };
    addAndMakeVisible (objectView);
    addAndMakeVisible (modeDisplay);

    // Object controls
    addKnob (objectKnobs, ids::pitch, "Pitch", "Frequency of the lowest mode (MIDI notes transpose from C3)");
    addKnob (objectKnobs, ids::decay, "Decay", "How long the object rings");
    addKnob (objectKnobs, ids::damping, "Damping", "How much faster high modes die than low ones");
    shapeKnob = addKnob (objectKnobs, ids::shape, "Shape", "");
    addKnob (objectKnobs, ids::position, "Position", "Where it is hit: centre (dull) to edge (bright, more modes)");

    // Strike controls
    addKnob (strikeKnobs, ids::hardness, "Hardness", "Mallet: soft felt to hard stick - sets which modes get excited");
    addKnob (strikeKnobs, ids::noise, "Noise", "Adds a noisy scrape or brush to the strike");
    addKnob (strikeKnobs, ids::drop, "Pitch Drop", "Pitch jumps up on impact and settles, like a tight drum head");
    addKnob (strikeKnobs, ids::width, "Width", "Spreads the upper modes across the stereo field");
    addKnob (strikeKnobs, ids::gain, "Output", "Output level");

    keyTrackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids::keyTrack, keyTrackButton);
    addAndMakeVisible (keyTrackButton);

    const int savedTheme = (int) proc.apvts.state.getProperty (ids::themeProperty, 0);
    const int themeIndex = juce::jlimit (0, (int) themes.size() - 1, savedTheme);
    themeBox.setSelectedItemIndex (themeIndex, juce::dontSendNotification);
    applyTheme (themeIndex);

    setSize (kEditorWidth, kEditorHeight);
    timerCallback();
    startTimerHz (30);
}

MaterialPercussionEditor::~MaterialPercussionEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

ui::Knob* MaterialPercussionEditor::addKnob (juce::OwnedArray<ui::Knob>& row, const char* paramId, const char* name, const char* hint)
{
    auto* k = row.add (new ui::Knob (proc.apvts, paramId, name, hint));
    addAndMakeVisible (k);
    return k;
}

//==============================================================================
void MaterialPercussionEditor::applyTheme (int index)
{
    const auto& themes = ui::getThemes();
    index = juce::jlimit (0, (int) themes.size() - 1, index);
    const auto& t = themes[(size_t) index];

    lnf.setTheme (t);
    objectView.setTheme (t);
    modeDisplay.setTheme (t);
    proc.apvts.state.setProperty (ids::themeProperty, index, nullptr);

    sendLookAndFeelChange();
    repaint();
}

void MaterialPercussionEditor::selectModel (int index)
{
    if (auto* param = proc.apvts.getParameter (ids::model))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
    }
}

void MaterialPercussionEditor::strikeAt (float position)
{
    if (auto* param = proc.apvts.getParameter (ids::position))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (position));
        param->endChangeGesture();
    }
    proc.keyboardState.noteOn (1, kUiNote, kUiVelocity);
    proc.keyboardState.noteOff (1, kUiNote, 0.0f);
}

juce::String MaterialPercussionEditor::currentHint() const
{
    const auto& info = mp::getModelInfo (displayParams.model);

    for (auto* row : { &objectKnobs, &strikeKnobs })
        for (auto* k : *row)
            if (k->isHovered())
                return k->getLabel() + ":  " + (k == shapeKnob ? juce::String (info.shapeHint) : k->hint);

    for (int i = 0; i < modelButtons.size(); ++i)
        if (modelButtons[i]->isMouseOver())
            return juce::String (mp::getModelInfo ((mp::Model) i).name) + ":  " + mp::getModelInfo ((mp::Model) i).blurb;

    if (keyTrackButton.isMouseOver())
        return "Key track:  MIDI notes change the pitch (off = every note plays the same pitch)";

    return juce::String (info.blurb);
}

//==============================================================================
void MaterialPercussionEditor::timerCallback()
{
    const auto params = proc.getModeParams();
    if (! haveDisplayModes || ! (params == displayParams))
    {
        mp::computeModes (params, displayModes);
        displayParams = params;
        haveDisplayModes = true;
    }

    float energy = 0.0f;
    for (size_t i = 0; i < shownLevels.size(); ++i)
    {
        const float live = proc.modeLevels[i].exchange (0.0f, std::memory_order_relaxed);
        shownLevels[i] = std::max (live, shownLevels[i] * 0.82f);
        if (shownLevels[i] < 1.0e-4f)
            shownLevels[i] = 0.0f;
        energy = std::max (energy, shownLevels[i]);
    }

    const float pitch = proc.apvts.getRawParameterValue (ids::pitch)->load();
    const float hardness = proc.apvts.getRawParameterValue (ids::hardness)->load();

    modeDisplay.setHint (currentHint());
    modeDisplay.update (displayModes, pitch, hardness, shownLevels.data());
    objectView.update (params.model, params.shape, params.position, juce::jlimit (0.0f, 1.0f, energy * 2.0f));

    const int modelIndex = (int) params.model;
    for (int i = 0; i < modelButtons.size(); ++i)
        modelButtons[i]->setToggleState (i == modelIndex, juce::dontSendNotification);

    shapeKnob->setLabel (mp::getModelInfo (params.model).shapeName);
}

//==============================================================================
void MaterialPercussionEditor::paint (juce::Graphics& g)
{
    const auto& t = lnf.getTheme();
    g.fillAll (t.background);

    // Title
    auto title = headerArea.withWidth (240).toFloat();
    g.setFont (juce::Font (juce::FontOptions (20.0f)).boldened());
    g.setColour (t.accent);
    g.drawText ("MATERIAL", title, juce::Justification::centredLeft);
    const float offset = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "MATERIAL ") ;
    g.setColour (t.text);
    g.drawText ("PERCUSSION", title.withTrimmedLeft (offset), juce::Justification::centredLeft);

    // Section headers
    auto drawSection = [&] (juce::Rectangle<int> area, const juce::String& name)
    {
        g.setColour (t.textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
        g.drawText (name, area, juce::Justification::centredLeft);
        g.setColour (t.panelEdge);
        g.drawHorizontalLine (area.getCentreY(), (float) area.getX() + 70.0f, (float) area.getRight() - 120.0f);
    };
    drawSection (objectHeader, "OBJECT");
    drawSection (strikeHeader, "STRIKE");
}

void MaterialPercussionEditor::resized()
{
    auto b = getLocalBounds().reduced (16);

    headerArea = b.removeFromTop (40);
    auto header = headerArea;
    themeBox.setBounds (header.removeFromRight (130).withSizeKeepingCentre (130, 28));
    header.removeFromLeft (240);
    auto buttons = header.withSizeKeepingCentre (juce::jmin (header.getWidth() - 20, mp::kNumModels * 86), 30);
    const int buttonWidth = buttons.getWidth() / mp::kNumModels;
    for (auto* button : modelButtons)
        button->setBounds (buttons.removeFromLeft (buttonWidth));

    b.removeFromTop (10);
    auto views = b.removeFromTop (300);
    objectView.setBounds (views.removeFromLeft (320));
    views.removeFromLeft (10);
    modeDisplay.setBounds (views);

    auto layoutRow = [] (juce::Rectangle<int> area, juce::OwnedArray<ui::Knob>& row)
    {
        const int w = area.getWidth() / row.size();
        for (auto* k : row)
            k->setBounds (area.removeFromLeft (w).reduced (10, 0));
    };

    b.removeFromTop (10);
    objectHeader = b.removeFromTop (22);
    keyTrackButton.setBounds (objectHeader.withLeft (objectHeader.getRight() - 110));
    layoutRow (b.removeFromTop (118), objectKnobs);

    b.removeFromTop (6);
    strikeHeader = b.removeFromTop (22);
    layoutRow (b.removeFromTop (118), strikeKnobs);
}
