#include "PluginEditor.h"
#include <cmath>

using namespace wavesieve;
using namespace wavesieve::ui;

namespace
{
const juce::Colour kBg       (0xff14161a);
const juce::Colour kPanel    (0xff1c1f25);
const juce::Colour kRaised   (0xff272b33);
const juce::Colour kLine     (0xff363b45);
const juce::Colour kText     (0xffe6e8ec);
const juce::Colour kDim      (0xff8d94a1);
const juce::Colour kAccent   (0xfff2a33c);
const juce::Colour kAccent2  (0xff4fc3c7);

juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}
float paramValue (WavesieveProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
}

Look::Look()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, kBg);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::arrowColourId, kDim);
    setColour (juce::PopupMenu::backgroundColourId, kRaised);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour (juce::PopupMenu::highlightedTextColourId, kBg);
    setColour (juce::TextEditor::backgroundColourId, kRaised);
    setColour (juce::TextEditor::textColourId, kText);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, kAccent);
    setColour (juce::TextEditor::highlightColourId, kAccent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, kAccent);
    setColour (juce::ListBox::backgroundColourId, kPanel);
    setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kText);
    setColour (juce::ToggleButton::textColourId, kText);
    setColour (juce::ToggleButton::tickColourId, kAccent);
    setColour (juce::ToggleButton::tickDisabledColourId, kDim);
    setColour (juce::ScrollBar::thumbColourId, kLine);
    setColour (juce::ResizableWindow::backgroundColourId, kBg);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0 && std::abs (s.getMinimum() + s.getMaximum()) < 1.0e-6;
    const float from = bipolar ? (start + end) * 0.5f : start;

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, start, end, true);
    g.setColour (kLine);
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (kAccent);
    g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (kRaised);
    g.fillEllipse (c.x - r + 8.0f, c.y - r + 8.0f, (r - 8.0f) * 2.0f, (r - 8.0f) * 2.0f);
    const juce::Point<float> tip = c.getPointOnCircumference (r - 10.0f, angle);
    const juce::Point<float> inner = c.getPointOnCircumference (r * 0.35f, angle);
    g.setColour (kText);
    g.drawLine ({ inner, tip }, 2.0f);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto area = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? kAccent : kRaised;
    if (over) c = c.brighter (0.08f);
    if (down) c = c.darker (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (area, 4.0f);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    g.setColour (kRaised);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 4.0f);
    juce::Path arrow;
    const float cx = (float) w - 13.0f, cy = (float) h * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (kDim);
    g.fillPath (arrow);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int) { return uiFont (13.0f); }
juce::Font Look::getComboBoxFont (juce::ComboBox&)          { return uiFont (13.0f); }
juce::Font Look::getLabelFont (juce::Label& l)              { return l.getFont(); }

Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& t)
    : title (t), attachment (s, id, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, kText);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 86, 18);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (slider);
}

void Knob::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (18)); }

void Knob::paint (juce::Graphics& g)
{
    g.setColour (kDim);
    g.setFont (uiFont (12.0f));
    g.drawText (title, getLocalBounds().removeFromTop (18), juce::Justification::centred);
}

// ------------------------------------------------------------------------------------------------
void TableView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds, 6.0f);
    const auto t = proc.getCurrentTable();
    if (t == nullptr) return;

    const auto area = bounds.reduced (14.0f);
    const int shown = juce::jmin (t->numFrames, 28);
    const float frame = paramValue (proc, "frame");
    const float depthX = area.getWidth() * 0.22f, depthY = area.getHeight() * 0.42f;
    const float waveW = area.getWidth() - depthX, waveH = area.getHeight() - depthY;

    auto drawFrame = [&] (const float* data, float z, juce::Colour colour, float thickness)
    {
        juce::Path p;
        const float x0 = area.getX() + z * depthX, yMid = area.getBottom() - waveH * 0.5f - z * depthY;
        const int steps = 128;
        for (int i = 0; i <= steps; ++i)
        {
            const float px = x0 + waveW * (float) i / steps;
            const float py = yMid - data[juce::jmin (i * kFrameSize / steps, kFrameSize - 1)] * waveH * 0.46f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (thickness));
    };

    for (int i = shown - 1; i >= 0; --i)
    {
        const float z = shown > 1 ? (float) i / (float) (shown - 1) : 0.0f;
        const int f = shown > 1 ? i * (t->numFrames - 1) / (shown - 1) : 0;
        drawFrame (t->frame (f), z, kDim.withAlpha (0.16f + 0.2f * (1.0f - z)), 1.0f);
    }
    const int cur = juce::roundToInt (frame * (float) (t->numFrames - 1));
    drawFrame (t->frame (cur), frame, kAccent, 1.8f);

    g.setColour (kDim);
    g.setFont (uiFont (12.0f));
    g.drawText ("Frame " + juce::String (cur + 1) + " / " + juce::String (t->numFrames) + "   (drag to scan)",
                getLocalBounds().reduced (12, 8), juce::Justification::topLeft);
}

void TableView::mouseDown (const juce::MouseEvent&)
{
    if (auto* p = proc.apvts.getParameter ("frame"))
    {
        p->beginChangeGesture();
        dragStart = p->getValue();
    }
}

void TableView::mouseDrag (const juce::MouseEvent& e)
{
    if (auto* p = proc.apvts.getParameter ("frame"))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStart + ((float) e.getDistanceFromDragStartX() - (float) e.getDistanceFromDragStartY()) / 300.0f));
}

void TableView::mouseUp (const juce::MouseEvent&)
{
    if (auto* p = proc.apvts.getParameter ("frame"))
        p->endChangeGesture();
}

void GraphView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds, 6.0f);
    const auto t = proc.getCurrentTable();
    if (t == nullptr) return;
    const auto area = bounds.reduced (14.0f, 12.0f).withTrimmedBottom (12.0f);
    if (paramValue (proc, "mode") < 0.5f) paintFilter (g, *t, area);
    else                                  paintShaper (g, *t, area);
}

void GraphView::paintFilter (juce::Graphics& g, const WaveTable& t, juce::Rectangle<float> area)
{
    std::vector<float> mag ((size_t) kNumHarmonics);
    computeHarmonicResponse (t, paramValue (proc, "frame"), paramValue (proc, "resonance"), mag.data(), nullptr, nullptr);
    const double cutoff = paramValue (proc, "cutoff");
    const double loHz = 20.0, hiHz = 20000.0;

    auto xFor = [&] (double hz) { return area.getX() + area.getWidth() * (float) (std::log (hz / loHz) / std::log (hiHz / loHz)); };
    g.setFont (uiFont (11.0f));
    for (double hz : { 100.0, 1000.0, 10000.0 })
    {
        g.setColour (kLine);
        g.drawVerticalLine ((int) xFor (hz), area.getY(), area.getBottom());
        g.setColour (kDim);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    (int) xFor (hz) - 20, (int) area.getBottom() + 1, 40, 12, juce::Justification::centred);
    }

    juce::Path p;
    const int steps = juce::jmax (64, (int) area.getWidth() * 2);
    for (int i = 0; i <= steps; ++i)
    {
        const double hz = loHz * std::pow (hiHz / loHz, (double) i / steps);
        const double h = juce::jmax (1.0, harmonicForFrequency (hz, cutoff));
        float m = 0.0f;
        if (h < 1024.0)
        {
            const int a = (int) h;
            m = mag[(size_t) a] + (float) (h - a) * (mag[(size_t) a + 1] - mag[(size_t) a]);
        }
        const float db = juce::jlimit (-72.0f, 0.0f, juce::Decibels::gainToDecibels (m, -72.0f));
        const float x = area.getX() + area.getWidth() * (float) i / steps;
        const float y = area.getY() + area.getHeight() * (-db / 72.0f);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    juce::Path fill (p);
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setColour (kAccent2.withAlpha (0.14f));
    g.fillPath (fill);
    g.setColour (kAccent2);
    g.strokePath (p, juce::PathStrokeType (1.3f));
}

void GraphView::paintShaper (juce::Graphics& g, const WaveTable& t, juce::Rectangle<float> area)
{
    std::vector<float> curve ((size_t) kFrameSize), scratch ((size_t) kFrameSize);
    computeShaperCurve (t, paramValue (proc, "frame"), paramValue (proc, "phase"), paramValue (proc, "smooth"), curve.data(), scratch.data());
    g.setColour (kLine);
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
    g.drawVerticalLine ((int) area.getCentreX(), area.getY(), area.getBottom());

    const float drive = juce::Decibels::decibelsToGain (paramValue (proc, "drive"));
    const float bias = paramValue (proc, "bias");
    const auto overflow = (Overflow) juce::roundToInt (paramValue (proc, "overflow"));
    juce::Path p;
    const int steps = juce::jmax (128, (int) area.getWidth() * 2);
    for (int i = 0; i <= steps; ++i)
    {
        const float in = -1.0f + 2.0f * (float) i / steps;
        float u = (in * drive + bias + 1.0f) * 0.5f;
        if (overflow == Overflow::repeat) u -= std::floor (u);
        else if (overflow == Overflow::hold) u = juce::jlimit (0.0f, 1.0f, u);
        else { float m = u * 0.5f; m -= std::floor (m); u = m < 0.5f ? m * 2.0f : 2.0f - m * 2.0f; }
        const float out = curve[(size_t) juce::jlimit (0, kFrameSize - 1, (int) (u * (kFrameSize - 1)))];
        const float x = area.getX() + area.getWidth() * (float) i / steps;
        const float y = area.getCentreY() - out * area.getHeight() * 0.48f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (kAccent2);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

// ------------------------------------------------------------------------------------------------
WavesieveEditor::WavesieveEditor (WavesieveProcessor& p)
    : AudioProcessorEditor (p), proc (p), tableView (p), graphView (p),
      frameKnob (p.apvts, "frame", "Frame"), cutoffKnob (p.apvts, "cutoff", "Cutoff"), resoKnob (p.apvts, "resonance", "Resonance"),
      driveKnob (p.apvts, "drive", "Drive"), biasKnob (p.apvts, "bias", "Bias"), phaseKnob (p.apvts, "phase", "Phase"),
      smoothKnob (p.apvts, "smooth", "Smooth"), mixKnob (p.apvts, "mix", "Mix"), outputKnob (p.apvts, "output", "Output"),
      phaseModeAttachment ((phaseModeBox.addItemList ({ "Minimum", "Linear", "Original", "Raw" }, 1), p.apvts), "phaseMode", phaseModeBox),
      overflowAttachment ((overflowBox.addItemList ({ "Repeat", "Hold", "Mirror" }, 1), p.apvts), "overflow", overflowBox),
      dcAttachment (p.apvts, "dc", dcButton)
{
    setLookAndFeel (&look);
    setWantsKeyboardFocus (true);

    for (auto* b : { &filterButton, &shaperButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    auto setMode = [this] (int m)
    {
        if (auto* param = proc.apvts.getParameter ("mode"))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) m));
    };
    filterButton.onClick = [setMode] { setMode (0); };
    shaperButton.onClick = [setMode] { setMode (1); };

    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    tableName.setFont (uiFont (15.0f, true));
    tableName.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (tableName);

    search.setTextToShowWhenEmpty ("Search tables", kDim);
    search.setFont (uiFont (13.0f));
    search.setIndents (8, 5);
    search.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (search);

    sourceBox.onChange = [this] { applyFilter(); };
    addAndMakeVisible (sourceBox);

    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, kPanel);
    addAndMakeVisible (list);

    addFolderButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a folder of wavetables");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& c)
                              {
                                  if (c.getResult().isDirectory())
                                      library->addFolder (c.getResult());
                              });
    };
    openButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a wavetable", juce::File(), "*.wav;*.wt;*.vitaltable;*.vital");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& c)
                              {
                                  if (c.getResult().existsAsFile())
                                      proc.loadTableAsync (c.getResult());
                              });
    };
    rescanButton.onClick = [this] { library->rescan(); status.setText ("Scanning...", juce::dontSendNotification); };
    addAndMakeVisible (addFolderButton);
    addAndMakeVisible (openButton);
    addAndMakeVisible (rescanButton);

    status.setFont (uiFont (12.0f));
    status.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (status);

    addAndMakeVisible (tableView);
    addAndMakeVisible (graphView);
    for (auto* k : { &frameKnob, &cutoffKnob, &resoKnob, &driveKnob, &biasKnob, &phaseKnob, &smoothKnob, &mixKnob, &outputKnob })
        addChildComponent (k);
    addChildComponent (phaseModeBox);
    addChildComponent (overflowBox);
    addChildComponent (dcButton);
    for (auto* l : { &phaseModeLabel, &overflowLabel })
    {
        l->setFont (uiFont (12.0f));
        l->setColour (juce::Label::textColourId, kDim);
        l->setJustificationType (juce::Justification::centredLeft);
        addChildComponent (l);
    }
    phaseModeLabel.setText ("Phase mode", juce::dontSendNotification);
    overflowLabel.setText ("Overflow", juce::dontSendNotification);

    library->addChangeListener (this);
    setSize (1060, 640);
    rebuildSources();
    applyFilter();
    syncMode();
    startTimerHz (30);
}

WavesieveEditor::~WavesieveEditor()
{
    library->removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void WavesieveEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (uiFont (20.0f, true));
    g.drawText ("Wavesieve", 16, 10, 120, 32, juce::Justification::centredLeft);
}

void WavesieveEditor::resized()
{
    auto area = getLocalBounds().reduced (14, 10);

    auto top = area.removeFromTop (34);
    top.removeFromLeft (126);
    filterButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (4);
    shaperButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (22);
    prevButton.setBounds (top.removeFromLeft (30));
    top.removeFromLeft (4);
    nextButton.setBounds (top.removeFromLeft (30));
    top.removeFromLeft (10);
    tableName.setBounds (top);
    area.removeFromTop (10);

    auto left = area.removeFromLeft (290);
    search.setBounds (left.removeFromTop (28));
    left.removeFromTop (6);
    sourceBox.setBounds (left.removeFromTop (28));
    left.removeFromTop (6);
    status.setBounds (left.removeFromBottom (20));
    left.removeFromBottom (2);
    auto buttons = left.removeFromBottom (28);
    const int w = (buttons.getWidth() - 8) / 3;
    addFolderButton.setBounds (buttons.removeFromLeft (w));
    buttons.removeFromLeft (4);
    openButton.setBounds (buttons.removeFromLeft (w));
    buttons.removeFromLeft (4);
    rescanButton.setBounds (buttons);
    left.removeFromBottom (6);
    list.setBounds (left);
    area.removeFromLeft (12);

    auto controls = area.removeFromBottom (132);
    area.removeFromBottom (10);
    tableView.setBounds (area.removeFromLeft (area.getWidth() / 2 - 4));
    area.removeFromLeft (8);
    graphView.setBounds (area);

    auto side = controls.removeFromRight (120);
    phaseModeLabel.setBounds (side.withHeight (18));
    overflowLabel.setBounds (side.withHeight (18));
    side.removeFromTop (20);
    phaseModeBox.setBounds (side.withHeight (28));
    overflowBox.setBounds (side.withHeight (28));
    dcButton.setBounds (side.withTrimmedTop (36).withHeight (26));

    const bool isFilter = paramValue (proc, "mode") < 0.5f;
    std::vector<Knob*> shown;
    if (isFilter) shown = { &frameKnob, &cutoffKnob, &resoKnob, &mixKnob, &outputKnob };
    else          shown = { &frameKnob, &driveKnob, &biasKnob, &phaseKnob, &smoothKnob, &mixKnob, &outputKnob };
    const int kw = controls.getWidth() / (int) shown.size();
    for (auto* k : shown)
        k->setBounds (controls.removeFromLeft (kw));
}

void WavesieveEditor::syncMode()
{
    const bool isFilter = paramValue (proc, "mode") < 0.5f;
    filterButton.setToggleState (isFilter, juce::dontSendNotification);
    shaperButton.setToggleState (! isFilter, juce::dontSendNotification);
    cutoffKnob.setVisible (isFilter);
    resoKnob.setVisible (isFilter);
    phaseModeBox.setVisible (isFilter);
    phaseModeLabel.setVisible (isFilter);
    frameKnob.setVisible (true);
    mixKnob.setVisible (true);
    outputKnob.setVisible (true);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &driveKnob, &biasKnob, &phaseKnob, &smoothKnob, &overflowBox, &overflowLabel, &dcButton })
        c->setVisible (! isFilter);
    resized();
}

void WavesieveEditor::rebuildSources()
{
    all = library->getEntries();
    const auto previous = sourceBox.getText();
    juce::StringArray sources;
    for (const auto& e : all) sources.addIfNotAlreadyThere (e.source);
    sourceBox.clear (juce::dontSendNotification);
    sourceBox.addItem ("All sources", 1);
    sourceBox.addItemList (sources, 2);
    const int idx = sources.indexOf (previous);
    sourceBox.setSelectedId (idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);
}

void WavesieveEditor::applyFilter()
{
    filtered.clear();
    const auto query = search.getText().trim();
    juce::StringArray terms;
    terms.addTokens (query, " ", "");
    terms.removeEmptyStrings();
    const auto source = sourceBox.getSelectedId() > 1 ? sourceBox.getText() : juce::String();

    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto& e = all[(size_t) i];
        if (source.isNotEmpty() && e.source != source) continue;
        bool match = true;
        for (const auto& term : terms)
            if (! e.name.containsIgnoreCase (term) && ! e.folder.containsIgnoreCase (term)) { match = false; break; }
        if (match) filtered.push_back (i);
    }

    juce::ScopedValueSetter<bool> guard (suppressSelection, true);
    list.updateContent();
    list.deselectAllRows();
    if (const auto t = proc.getCurrentTable())
        for (int r = 0; r < (int) filtered.size(); ++r)
            if (all[(size_t) filtered[(size_t) r]].file == t->file)
            {
                list.selectRow (r);
                break;
            }

    status.setText (library->isScanning() ? "Scanning your wavetables..."
                                          : juce::String ((int) filtered.size()) + " of " + juce::String ((int) all.size()) + " tables",
                    juce::dontSendNotification);
    list.repaint();
}

void WavesieveEditor::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) filtered.size())) return;
    const auto& e = all[(size_t) filtered[(size_t) row]];
    if (selected)
    {
        g.setColour (kAccent.withAlpha (0.18f));
        g.fillRect (0, 0, w, h);
        g.setColour (kAccent);
        g.fillRect (0, 0, 3, h);
    }
    g.setColour (kText);
    g.setFont (uiFont (13.0f));
    g.drawText (e.name, 10, 3, w - 16, 16, juce::Justification::centredLeft, true);
    g.setColour (kDim);
    g.setFont (uiFont (11.0f));
    g.drawText (e.folder.isNotEmpty() ? e.source + " / " + e.folder : e.source, 10, 18, w - 16, 13, juce::Justification::centredLeft, true);
}

void WavesieveEditor::selectedRowsChanged (int row)
{
    if (suppressSelection || ! juce::isPositiveAndBelow (row, (int) filtered.size())) return;
    proc.loadTableAsync (all[(size_t) filtered[(size_t) row]].file);
}

void WavesieveEditor::step (int delta)
{
    if (filtered.empty()) return;
    const int cur = list.getSelectedRow();
    const int n = (int) filtered.size();
    const int next = cur < 0 ? (delta > 0 ? 0 : n - 1) : (cur + delta + n) % n;
    list.selectRow (next);  // selectedRowsChanged loads it
}

bool WavesieveEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isWaveTableExtension (juce::File (f))) return true;
    return false;
}

void WavesieveEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (isWaveTableExtension (juce::File (f)))
        {
            proc.loadTableAsync (juce::File (f));
            return;
        }
}

void WavesieveEditor::timerCallback()
{
    const int mode = juce::roundToInt (paramValue (proc, "mode"));
    if (mode != shownMode)
    {
        shownMode = mode;
        syncMode();
        graphView.repaint();
    }

    const auto t = proc.getCurrentTable();
    if (t.get() != shownTable)
    {
        shownTable = t.get();
        tableName.setText (t != nullptr ? t->name : juce::String(), juce::dontSendNotification);
        tableView.repaint();
        graphView.repaint();
        if (t != nullptr && list.getSelectedRow() >= 0
            && juce::isPositiveAndBelow (list.getSelectedRow(), (int) filtered.size())
            && all[(size_t) filtered[(size_t) list.getSelectedRow()]].file != t->file)
            applyFilter();
    }

    const float frame = paramValue (proc, "frame"), cutoff = paramValue (proc, "cutoff"), reso = paramValue (proc, "resonance"),
                phase = paramValue (proc, "phase"), smooth = paramValue (proc, "smooth"), drive = paramValue (proc, "drive"), bias = paramValue (proc, "bias");
    const int overflow = juce::roundToInt (paramValue (proc, "overflow"));
    if (frame != shownFrame) tableView.repaint();
    if (frame != shownFrame || cutoff != shownCutoff || reso != shownReso || phase != shownPhase || smooth != shownSmooth
        || drive != shownDrive || bias != shownBias || overflow != shownOverflow)
        graphView.repaint();
    shownFrame = frame; shownCutoff = cutoff; shownReso = reso; shownPhase = phase; shownSmooth = smooth;
    shownDrive = drive; shownBias = bias; shownOverflow = overflow;

    const auto error = proc.getTableError();
    if (error != shownError)
    {
        shownError = error;
        if (error.isNotEmpty())
            status.setText (error, juce::dontSendNotification);
        else
            applyFilter();
    }
}
