#include "PluginEditor.h"

namespace
{
constexpr int kEditorWidth = 1080;
constexpr int kEditorHeight = 680;
constexpr int kKnobRowHeight = 112;
constexpr int kUiNote = 60;          // C3: plays at exactly the Pitch knob
constexpr float kUiVelocity = 0.85f;
} // namespace

MaterialPercussionEditor::MaterialPercussionEditor (MaterialPercussionProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    // Model and exciter selectors (segmented buttons)
    juce::StringArray modelNames, exciterNames;
    for (int i = 0; i < mp::kNumModels; ++i)
        modelNames.add (mp::getModelInfo ((mp::Model) i).name);
    for (int i = 0; i < mp::kNumExciters; ++i)
        exciterNames.add (mp::getExciterInfo ((mp::ExciterType) i).name);
    modelButtons = makeSegmented (modelNames, [this] (int i) { setChoice (ids::model, i); });
    exciterButtons = makeSegmented (exciterNames, [this] (int i) { setChoice (ids::exciter, i); });

    // Theme picker
    const auto& themes = ui::getThemes();
    for (int i = 0; i < (int) themes.size(); ++i)
        themeBox.addItem (themes[(size_t) i].name, i + 1);
    themeBox.onChange = [this] { applyTheme (themeBox.getSelectedItemIndex()); };
    addAndMakeVisible (themeBox);

    // Body picker (item ids follow the parameter's choice order)
    bodyBox.addItem ("No body", 1);
    for (int i = 0; i < mp::kNumModels; ++i)
        bodyBox.addItem (juce::String (mp::getModelInfo ((mp::Model) i).name) + " body", i + 2);
    bodyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ids::bodyModel, bodyBox);
    addAndMakeVisible (bodyBox);

    // Visualisation
    objectView.onStrike = [this] (float position) { strikeAt (position); };
    addAndMakeVisible (objectView);
    addAndMakeVisible (modeDisplay);

    // Capture: the Load button sits on top of the object view (shown for the Captured model)
    objectView.onLoadRequest = [this] { chooseCaptureFile(); };
    loadButton.onClick = [this] { chooseCaptureFile(); };
    addChildComponent (loadButton);

    // Object
    addKnob (objectSection, ids::pitch, "Pitch", "Frequency of the lowest mode (MIDI notes transpose from C3)");
    decayKnob = addKnob (objectSection, ids::decay, "Decay", "How long the object rings");
    dampingKnob = addKnob (objectSection, ids::damping, "Damping", "How much faster high modes die than low ones");
    shapeKnob = addKnob (objectSection, ids::shape, "Shape", "");
    positionKnob = addKnob (objectSection, ids::position, "Position", "Where it is hit: centre (dull) to edge (bright, more modes)");

    // Exciter
    hardnessKnob = addKnob (exciterSection, ids::hardness, "Hardness", "");
    colorKnob = addKnob (exciterSection, ids::color, "Colour", "");
    lengthKnob = addKnob (exciterSection, ids::length, "Length", "How long it keeps bowing / scraping / shaking / feeding back (or hold the key)");
    bounceKnob = addKnob (exciterSection, ids::bounce, "Bounce", "Mallet bounces: one clean hit (left) to a dropped stick rattling to rest");
    noiseKnob = addKnob (exciterSection, ids::noise, "Noise", "Adds noise to the excitation: a scrape on the strike, breath on the bow");

    // Nonlinear
    addKnob (nonlinearSection, ids::drop, "Pitch Drop", "Pitch jumps up on impact and settles, like a tight drum head");
    addKnob (nonlinearSection, ids::tension, "Tension", "Loud = stiff: pitch bends down as it decays and energy blooms into high modes (gong crash)");
    addKnob (nonlinearSection, ids::rattle, "Rattle", "A loose part buzzing against the object: snare wires, sizzle rivets, a sitar bridge");
    addKnob (nonlinearSection, ids::gap, "Gap", "How far it must move before it buzzes: small = constant buzz, large = only on hard hits");

    // Body
    addKnob (bodySection, ids::bodyTune, "Tune", "Body pitch relative to the object");
    addKnob (bodySection, ids::bodyDecay, "Decay", "How long the body rings");
    addKnob (bodySection, ids::bodyBlend, "Blend", "What gets hit: the object (left), the body (right), or both - a morph between them");
    addKnob (bodySection, ids::bodyCouple, "Couple", "How tightly the two are joined: sympathetic ringing, beating, energy trading");

    // Output
    addKnob (outputSection, ids::width, "Width", "Spreads the upper modes across the stereo field");
    addKnob (outputSection, ids::gain, "Output", "Output level");

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

juce::OwnedArray<juce::TextButton> MaterialPercussionEditor::makeSegmented (const juce::StringArray& names, std::function<void (int)> onPick)
{
    juce::OwnedArray<juce::TextButton> buttons;
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (names[i]));
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < names.size() - 1) edges |= juce::Button::ConnectedOnRight;
        b->setConnectedEdges (edges);
        b->onClick = [onPick, i] { onPick (i); };
        addAndMakeVisible (b);
    }
    return buttons;
}

ui::Knob* MaterialPercussionEditor::addKnob (Section& section, const char* paramId, const char* name, const char* hint)
{
    auto* k = knobs.add (new ui::Knob (proc.apvts, paramId, name, hint));
    section.knobs.push_back (k);
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

void MaterialPercussionEditor::setChoice (const char* paramId, int index)
{
    if (auto* param = proc.apvts.getParameter (paramId))
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
    const auto& model = mp::getModelInfo (displayParams.model);
    const auto exciterType = (mp::ExciterType) juce::roundToInt (proc.apvts.getRawParameterValue (ids::exciter)->load());
    const auto& exciter = mp::getExciterInfo (exciterType);

    const bool captured = displayParams.model == mp::Model::Captured;
    for (auto* k : knobs)
        if (k->isVisible() && k->isHovered())
        {
            if (k == shapeKnob)    return k->getLabel() + ":  " + model.shapeHint;
            if (k == hardnessKnob) return k->getLabel() + ":  " + exciter.amountHint;
            if (k == colorKnob)    return exciter.colorHint;
            if (k == noiseKnob && exciterType == mp::ExciterType::Sample)
                return "Attack:  how loud the recorded attack is, heard on top of the ringing modes";
            if (captured && k == decayKnob)    return "Decay:  scales the recorded ring times (1 s = as recorded)";
            if (captured && k == dampingKnob)  return "Damping:  high modes ring longer (left) or die faster (right) than recorded; 50% = as recorded";
            if (captured && k == positionKnob) return "Position:  darker (left) or brighter (right) than recorded; 50% = as recorded";
            return k->getLabel() + ":  " + k->hint;
        }

    if (loadButton.isVisible() && loadButton.isMouseOver())
        return "Load sound:  analyse a recording of one hit into modes + attack (or drop a file anywhere here)";

    for (int i = 0; i < modelButtons.size(); ++i)
        if (modelButtons[i]->isMouseOver())
            return juce::String (mp::getModelInfo ((mp::Model) i).name) + ":  " + mp::getModelInfo ((mp::Model) i).blurb;

    for (int i = 0; i < exciterButtons.size(); ++i)
        if (exciterButtons[i]->isMouseOver())
            return juce::String (mp::getExciterInfo ((mp::ExciterType) i).name) + ":  " + mp::getExciterInfo ((mp::ExciterType) i).blurb;

    if (bodyBox.isMouseOver (true))
        return "Body:  a second resonator joined to the object - a drum shell, a soundboard, or something impossible";

    if (keyTrackButton.isMouseOver())
        return "Key track:  MIDI notes change the pitch (off = every note plays the same pitch)";

    return juce::String (model.blurb);
}

//==============================================================================
void MaterialPercussionEditor::timerCallback()
{
    displayCapture = proc.getCapture();
    const auto params = proc.getModeParams (displayCapture.get());
    if (! haveDisplayModes || ! (params == displayParams))
    {
        mp::computeModes (params, displayModes);
        displayParams = params;
    }

    mp::ModeParams bodyParams;
    const bool bodyOn = proc.getBodyParams (bodyParams, displayCapture.get());
    if (! haveDisplayModes || bodyOn != displayBodyOn || ! (bodyParams == displayBodyParams))
    {
        mp::computeModes (bodyParams, displayBodyModes);
        displayBodyParams = bodyParams;
        displayBodyOn = bodyOn;
    }
    haveDisplayModes = true;

    float energy = 0.0f;
    for (size_t i = 0; i < shownLevels.size(); ++i)
    {
        const float live = proc.modeLevels[i].exchange (0.0f, std::memory_order_relaxed);
        shownLevels[i] = std::max (live, shownLevels[i] * 0.82f);
        if (shownLevels[i] < 1.0e-4f)
            shownLevels[i] = 0.0f;
        energy = std::max (energy, shownLevels[i]);
    }
    for (size_t i = 0; i < shownBodyLevels.size(); ++i)
    {
        const float live = proc.bodyLevels[i].exchange (0.0f, std::memory_order_relaxed);
        shownBodyLevels[i] = std::max (live, shownBodyLevels[i] * 0.82f);
        if (shownBodyLevels[i] < 1.0e-4f)
            shownBodyLevels[i] = 0.0f;
    }

    const auto settings = proc.readVoiceSettings();
    const auto& exciter = mp::getExciterInfo (settings.exciter);

    ui::ModeDisplay::State state;
    state.modes = &displayModes;
    state.baseHz = settings.pitchHz;
    state.bodyModes = bodyOn ? &displayBodyModes : nullptr;
    state.bodyHz = settings.pitchHz * std::exp2 (settings.bodyTune / 12.0f);
    state.exciter = settings.exciter;
    state.hardness = settings.hardness;
    state.color = settings.color;
    state.liveLevels = shownLevels.data();
    state.bodyLevels = shownBodyLevels.data();
    modeDisplay.setHint (currentHint());
    modeDisplay.update (state);

    objectView.update (params.model, params.shape, params.position, juce::jlimit (0.0f, 1.0f, energy * 2.0f), settings.exciter,
                       bodyOn ? juce::String (mp::getModelInfo (bodyParams.model).name) : juce::String());
    objectView.setCapture (displayCapture, proc.getCaptureMessage(), proc.isCaptureBusy());
    loadButton.setVisible (params.model == mp::Model::Captured);
    noiseKnob->setLabel (settings.exciter == mp::ExciterType::Sample ? "Attack" : "Noise");

    for (int i = 0; i < modelButtons.size(); ++i)
        modelButtons[i]->setToggleState (i == (int) params.model, juce::dontSendNotification);
    for (int i = 0; i < exciterButtons.size(); ++i)
        exciterButtons[i]->setToggleState (i == (int) settings.exciter, juce::dontSendNotification);

    // Knobs whose meaning depends on the model / exciter
    shapeKnob->setLabel (mp::getModelInfo (params.model).shapeName);
    hardnessKnob->setLabel (exciter.amountName);
    lengthKnob->setVisible (exciter.continuous);
    bounceKnob->setVisible (! exciter.continuous);
    colorKnob->setAlpha (settings.exciter == mp::ExciterType::Mallet ? 0.4f : 1.0f);
    for (auto* k : bodySection.knobs)
        k->setAlpha (bodyOn ? 1.0f : 0.4f);
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
    const float offset = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "MATERIAL ");
    g.setColour (t.text);
    g.drawText ("PERCUSSION", title.withTrimmedLeft (offset), juce::Justification::centredLeft);

    // Section headers
    for (auto* s : { &objectSection, &exciterSection, &nonlinearSection, &bodySection, &outputSection })
    {
        g.setColour (t.textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
        g.drawText (s->title, s->header, juce::Justification::centredLeft);
        const float textW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s->title) + 10.0f;
        g.setColour (t.panelEdge);
        g.drawHorizontalLine (s->header.getCentreY(), (float) s->header.getX() + textW,
                              (float) (s->header.getRight() - s->headerReserve - 8));
    }
}

void MaterialPercussionEditor::layoutSection (Section& section, juce::Rectangle<int> area)
{
    section.header = area.removeFromTop (24).withTrimmedRight (12);
    auto row = area.removeFromTop (kKnobRowHeight).withTrimmedRight (12);

    // Length and Bounce share a slot: only one of them is visible at a time.
    std::vector<ui::Knob*> slots;
    for (auto* k : section.knobs)
        if (k != bounceKnob)
            slots.push_back (k);

    const int w = row.getWidth() / (int) slots.size();
    for (auto* k : slots)
    {
        const auto cell = row.removeFromLeft (w).reduced (6, 0);
        k->setBounds (cell);
        if (k == lengthKnob)
            bounceKnob->setBounds (cell);
    }
}

void MaterialPercussionEditor::resized()
{
    auto b = getLocalBounds().reduced (16);

    // Header: title, model buttons, theme
    headerArea = b.removeFromTop (40);
    auto header = headerArea;
    themeBox.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 28));
    header.removeFromLeft (240);
    auto buttons = header.withSizeKeepingCentre (juce::jmin (header.getWidth() - 20, mp::kNumModels * 80), 30);
    const int buttonWidth = buttons.getWidth() / mp::kNumModels;
    for (auto* button : modelButtons)
        button->setBounds (buttons.removeFromLeft (buttonWidth));

    b.removeFromTop (10);
    auto views = b.removeFromTop (290);
    objectView.setBounds (views.removeFromLeft (340));
    loadButton.setBounds (objectView.getRight() - 12 - 104, objectView.getY() + 7, 104, 22);
    views.removeFromLeft (10);
    modeDisplay.setBounds (views);
    b.removeFromTop (12);

    // Row 1: object (5 knobs) | exciter (4 knobs + selector)
    auto row1 = b.removeFromTop (24 + kKnobRowHeight);
    const int unit1 = row1.getWidth() / 9;
    layoutSection (objectSection, row1.removeFromLeft (unit1 * 5));
    layoutSection (exciterSection, row1);

    objectSection.headerReserve = 100;
    keyTrackButton.setBounds (objectSection.header.withLeft (objectSection.header.getRight() - 100));

    exciterSection.headerReserve = juce::jmin (exciterSection.header.getWidth() - 80, 5 * 70);
    auto exButtons = exciterSection.header.withLeft (exciterSection.header.getRight() - exciterSection.headerReserve).reduced (0, 1);
    const int exWidth = exButtons.getWidth() / mp::kNumExciters;
    for (auto* button : exciterButtons)
        button->setBounds (exButtons.removeFromLeft (exWidth));

    b.removeFromTop (8);

    // Row 2: nonlinear (4) | body (4 + picker) | output (2)
    auto row2 = b.removeFromTop (24 + kKnobRowHeight);
    const int unit2 = row2.getWidth() / 10;
    layoutSection (nonlinearSection, row2.removeFromLeft (unit2 * 4));
    layoutSection (bodySection, row2.removeFromLeft (unit2 * 4));
    layoutSection (outputSection, row2);

    bodySection.headerReserve = 150;
    bodyBox.setBounds (bodySection.header.withLeft (bodySection.header.getRight() - 150).reduced (0, 1));
}

//==============================================================================
void MaterialPercussionEditor::chooseCaptureFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Capture a sound: pick a recording of a single hit",
                                                       juce::File(), MaterialPercussionProcessor::getCaptureWildcard());
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  if (file.existsAsFile())
                                  {
                                      setChoice (ids::model, (int) mp::Model::Captured);
                                      proc.loadCaptureFile (file);
                                  }
                              });
}

bool MaterialPercussionEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension (MaterialPercussionProcessor::getCaptureWildcard().removeCharacters ("*")))
            return true;
    return false;
}

void MaterialPercussionEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    objectView.setDropHighlight (true);
}

void MaterialPercussionEditor::fileDragExit (const juce::StringArray&)
{
    objectView.setDropHighlight (false);
}

void MaterialPercussionEditor::filesDropped (const juce::StringArray& files, int, int)
{
    objectView.setDropHighlight (false);
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.hasFileExtension (MaterialPercussionProcessor::getCaptureWildcard().removeCharacters ("*")))
        {
            setChoice (ids::model, (int) mp::Model::Captured);
            proc.loadCaptureFile (file);
            return;
        }
    }
}
