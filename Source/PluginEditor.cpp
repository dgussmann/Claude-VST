#include "PluginEditor.h"
#include "ShapeLibrary.h"

namespace
{
    const int gridChoices[] = { 2, 3, 4, 6, 8, 12, 16, 32 };

    void styleSectionLabel (juce::Label& l)
    {
        l.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, Theme::textDim);
        l.setJustificationType (juce::Justification::centredLeft);
    }

    bool onMessageThread()
    {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        return mm != nullptr && mm->isThisTheMessageThread();
    }
}

//==============================================================================
TargetRow::TargetRow (ShapeModEditor& owner, int index)
    : editor (owner), slotIndex (index)
{
    auto& slot = editor.getProcessor().getSlot (slotIndex);

    enableToggle.setTooltip ("Turn this target on or off");
    enableToggle.onClick = [this, &slot] { slot.enabled = enableToggle.getToggleState(); repaint(); };
    addAndMakeVisible (enableToggle);

    paramButton.setTooltip ("Choose which knob of the hosted plugin this target moves");
    paramButton.onClick = [this] { editor.showParameterMenu (slotIndex, paramButton); };
    addAndMakeVisible (paramButton);

    learnButton.setTooltip ("Click, then move a knob in the plugin to assign it");
    learnButton.onClick = [this] { editor.toggleLearn (slotIndex); };
    addAndMakeVisible (learnButton);

    range.setRange (0.0, 1.0, 0.001);
    range.setPopupDisplayEnabled (true, true, nullptr);
    range.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    range.setTooltip ("The part of the knob's travel the shape sweeps across");
    range.onValueChange = [this, &slot]
    {
        slot.rangeMin = (float) range.getMinValue();
        slot.rangeMax = (float) range.getMaxValue();
    };
    addAndMakeVisible (range);

    invertButton.setClickingTogglesState (true);
    invertButton.setTooltip ("Flip the shape for this target");
    invertButton.onClick = [this, &slot] { slot.inverted = invertButton.getToggleState(); };
    addAndMakeVisible (invertButton);

    clearButton.setTooltip ("Remove this target");
    clearButton.onClick = [this]
    {
        editor.getProcessor().clearSlot (slotIndex);
        editor.refreshRows();
    };
    addAndMakeVisible (clearButton);

    refresh();
}

void TargetRow::refresh()
{
    auto& proc = editor.getProcessor();
    auto& slot = proc.getSlot (slotIndex);
    auto* hosted = proc.getHostedPlugin();
    const auto idx = slot.paramIndex.load();

    juce::String text = "Assign...";
    if (hosted != nullptr && juce::isPositiveAndBelow (idx, hosted->getParameters().size()))
        text = hosted->getParameters()[idx]->getName (64);

    paramButton.setButtonText (text);
    paramButton.setColour (juce::TextButton::textColourOffId, idx >= 0 ? Theme::text : Theme::textDim);
    enableToggle.setToggleState (slot.enabled.load(), juce::dontSendNotification);
    invertButton.setToggleState (slot.inverted.load(), juce::dontSendNotification);
    range.setMinAndMaxValues (slot.rangeMin.load(), slot.rangeMax.load(), juce::dontSendNotification);

    const bool hasPlugin = hosted != nullptr;
    const bool assigned = hasPlugin && idx >= 0;
    paramButton.setEnabled (hasPlugin);
    learnButton.setEnabled (hasPlugin);
    range.setEnabled (assigned);
    invertButton.setEnabled (assigned);
    clearButton.setEnabled (assigned);
    enableToggle.setEnabled (assigned);
    repaint();
}

void TargetRow::setLearning (bool learning)
{
    learnButton.setToggleState (learning, juce::dontSendNotification);
    learnButton.setButtonText (learning ? "..." : "Learn");
}

void TargetRow::resized()
{
    auto r = getLocalBounds().reduced (2, 1);
    enableToggle.setBounds (r.removeFromLeft (24));
    r.removeFromLeft (2);
    paramButton.setBounds (r.removeFromLeft (150));
    r.removeFromLeft (4);
    learnButton.setBounds (r.removeFromLeft (48));
    clearButton.setBounds (r.removeFromRight (24));
    r.removeFromRight (4);
    invertButton.setBounds (r.removeFromRight (36));
    r.removeFromRight (4);
    range.setBounds (r);
}

void TargetRow::paint (juce::Graphics& g)
{
    auto& slot = editor.getProcessor().getSlot (slotIndex);
    const bool active = slot.paramIndex.load() >= 0 && slot.enabled.load();
    g.setColour (Theme::panelRaised.withAlpha (active ? 0.9f : 0.45f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    if (active)
    {
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (getLocalBounds().toFloat().removeFromLeft (3.0f), 1.5f);
    }
}

//==============================================================================
ShapeModEditor::ShapeModEditor (ShapeModProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (false);

    // Top bar
    loadButton.onClick = [this] { showLoadMenu(); };
    loadButton.setTooltip ("Choose a VST3 synth or effect to modulate");
    addAndMakeVisible (loadButton);

    showPluginButton.onClick = [this]
    {
        showHosted = ! showHosted;
        showPluginButton.setButtonText (showHosted ? "Hide plugin" : "Show plugin");
        if (hostedEditor != nullptr) hostedEditor->setVisible (showHosted);
        updateSize();
    };
    addAndMakeVisible (showPluginButton);

    hostedLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (hostedLabel);
    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (statusLabel);

    shapeNameLabel.setJustificationType (juce::Justification::centredRight);
    shapeNameLabel.setColour (juce::Label::textColourId, Theme::accent);
    addAndMakeVisible (shapeNameLabel);

    shapesButton.onClick = [this] { showShapeMenu(); };
    shapesButton.setTooltip ("Load a factory or saved shape");
    saveButton.onClick = [this] { saveShapeAs(); };
    saveButton.setTooltip ("Save this shape to your library");
    undoButton.onClick = [this] { undo(); };
    addAndMakeVisible (shapesButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (undoButton);

    // Shape canvas
    shapeEditor.getPhase = [this] { return processor.getDisplayPhase(); };
    shapeEditor.getValue = [this] { return processor.getDisplayValue(); };
    shapeEditor.onEditStart = [this] { pushUndo(); };
    shapeEditor.onChange = [this] (const Shape& s) { processor.setShape (s); };
    shapeEditor.setShape (processor.getShape());
    addAndMakeVisible (shapeEditor);

    pointsTool.setRadioGroupId (1);
    drawTool.setRadioGroupId (1);
    pointsTool.setClickingTogglesState (true);
    drawTool.setClickingTogglesState (true);
    pointsTool.setToggleState (true, juce::dontSendNotification);
    pointsTool.onClick = [this] { shapeEditor.setTool (ShapeEditor::Tool::points); };
    drawTool.onClick = [this] { shapeEditor.setTool (ShapeEditor::Tool::draw); };
    drawTool.setTooltip ("Paint steps on the grid");
    addAndMakeVisible (pointsTool);
    addAndMakeVisible (drawTool);

    for (int i = 0; i < (int) std::size (gridChoices); ++i)
        gridBox.addItem ("Grid " + juce::String (gridChoices[i]), i + 1);
    gridBox.setSelectedId (5, juce::dontSendNotification);
    gridBox.onChange = [this] { shapeEditor.setGridDivisions (gridChoices[gridBox.getSelectedId() - 1]); };
    gridBox.setTooltip ("Grid divisions per cycle (also the number of steps in Draw mode)");
    addAndMakeVisible (gridBox);

    snapToggle.setToggleState (true, juce::dontSendNotification);
    snapToggle.onClick = [this] { shapeEditor.setSnap (snapToggle.getToggleState()); };
    addAndMakeVisible (snapToggle);

    invertButton.onClick = [this] { auto s = shapeEditor.getShape(); s.invert(); applyShape (s, true); };
    reverseButton.onClick = [this] { auto s = shapeEditor.getShape(); s.reverse(); applyShape (s, true); };
    randomButton.onClick = [this]
    {
        juce::Random rng;
        const bool smooth = juce::ModifierKeys::currentModifiers.isShiftDown();
        applyShape (Shape::random (shapeEditor.getGridDivisions(), smooth, rng), true);
    };
    randomButton.setTooltip ("Random steps (Shift-click for a smooth random shape)");
    addAndMakeVisible (invertButton);
    addAndMakeVisible (reverseButton);
    addAndMakeVisible (randomButton);

    // Timing
    rateBox.addItemList (ShapeModProcessor::rateNames(), 1);
    rateModeBox.addItemList (ShapeModProcessor::rateModeNames(), 1);
    triggerBox.addItemList (ShapeModProcessor::triggerNames(), 1);
    rateBox.setTooltip ("How long one pass of the shape lasts");
    rateModeBox.setTooltip ("Straight, dotted (x1.5 longer) or triplet (x2/3)");
    triggerBox.setTooltip ("Song position: locked to the arrangement. Note retrigger: restarts on each MIDI note "
                           "(Instrument version). Free run: ignores the song position.");
    rateAttach     = std::make_unique<ComboAttachment> (processor.apvts, "rate", rateBox);
    rateModeAttach = std::make_unique<ComboAttachment> (processor.apvts, "rateMode", rateModeBox);
    triggerAttach  = std::make_unique<ComboAttachment> (processor.apvts, "trigger", triggerBox);

    for (auto* l : { &rateLabel, &modeLabel, &triggerLabel, &phaseLabel, &depthLabel, &smoothLabel, &targetsLabel })
    {
        styleSectionLabel (*l);
        addAndMakeVisible (*l);
    }
    for (auto* l : { &phaseLabel, &depthLabel, &smoothLabel })
        l->setJustificationType (juce::Justification::centred);

    for (auto* c : { &rateBox, &rateModeBox, &triggerBox })
        addAndMakeVisible (*c);

    for (auto* k : { &phaseKnob, &depthKnob, &smoothKnob })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        addAndMakeVisible (*k);
        k->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        k->setColour (juce::Slider::textBoxTextColourId, Theme::text);
        k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    }
    phaseAttach  = std::make_unique<SliderAttachment> (processor.apvts, "phase", phaseKnob);
    depthAttach  = std::make_unique<SliderAttachment> (processor.apvts, "depth", depthKnob);
    smoothAttach = std::make_unique<SliderAttachment> (processor.apvts, "smooth", smoothKnob);

    modOnButton.setClickingTogglesState (true);
    modOnButton.setTooltip ("Pause or resume all modulation");
    modOnAttach = std::make_unique<ButtonAttachment> (processor.apvts, "modOn", modOnButton);
    addAndMakeVisible (modOnButton);

    for (int i = 0; i < ShapeModProcessor::numSlots; ++i)
        addAndMakeVisible (rows.add (new TargetRow (*this, i)));

    processor.addHostListener (this);
    createHostedEditor();
    updateHostedLabel();
    shapeNameLabel.setText (processor.getShape().name, juce::dontSendNotification);

    if (processor.getLastLoadError().isNotEmpty())
        setStatus (processor.getLastLoadError(), true);
    else if (processor.getHostedPlugin() == nullptr)
        setStatus ("Click \"Load plugin\" to choose a synth or effect, then assign targets.");

    updateSize();
    startTimerHz (10);
}

ShapeModEditor::~ShapeModEditor()
{
    processor.removeHostListener (this);
    cancelPendingUpdate();
    destroyHostedEditor();
    setLookAndFeel (nullptr);
}

//==============================================================================
void ShapeModEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    if (! panelBounds.isEmpty())
    {
        g.setColour (Theme::background);
        g.fillRect (panelBounds);
        g.setColour (Theme::accent.withAlpha (0.8f));
        g.fillRect (panelBounds.getX(), panelBounds.getY(), panelBounds.getWidth(), 2);
    }
}

void ShapeModEditor::resized()
{
    auto r = getLocalBounds();

    if (hostedEditor != nullptr && showHosted)
    {
        auto top = r.removeFromTop (hostedEditor->getHeight());
        hostedEditor->setTopLeftPosition (top.getX() + (top.getWidth() - hostedEditor->getWidth()) / 2, top.getY());
    }

    panelBounds = r;
    r = r.reduced (12);
    r.removeFromTop (2);

    // Top bar
    auto bar = r.removeFromTop (32);
    loadButton.setBounds (bar.removeFromLeft (110).reduced (0, 2));
    bar.removeFromLeft (6);
    showPluginButton.setBounds (bar.removeFromLeft (96).reduced (0, 2));
    bar.removeFromLeft (10);
    undoButton.setBounds (bar.removeFromRight (60).reduced (0, 2));
    bar.removeFromRight (6);
    saveButton.setBounds (bar.removeFromRight (60).reduced (0, 2));
    bar.removeFromRight (6);
    shapesButton.setBounds (bar.removeFromRight (80).reduced (0, 2));
    bar.removeFromRight (6);
    shapeNameLabel.setBounds (bar.removeFromRight (170));
    hostedLabel.setBounds (bar);

    statusLabel.setBounds (r.removeFromBottom (18));
    r.removeFromTop (10);

    // Right column
    auto right = r.removeFromRight (430);
    r.removeFromRight (14);

    auto combos = right.removeFromTop (42);
    const auto comboW = (combos.getWidth() - 16) / 3;
    auto placeCombo = [&] (juce::Label& label, juce::ComboBox& box)
    {
        auto c = combos.removeFromLeft (comboW);
        label.setBounds (c.removeFromTop (14));
        box.setBounds (c.removeFromTop (26));
        combos.removeFromLeft (8);
    };
    placeCombo (rateLabel, rateBox);
    placeCombo (modeLabel, rateModeBox);
    placeCombo (triggerLabel, triggerBox);

    right.removeFromTop (8);
    auto knobs = right.removeFromTop (82);
    modOnButton.setBounds (knobs.removeFromRight (84).withSizeKeepingCentre (84, 30));
    const auto knobW = knobs.getWidth() / 3;
    auto placeKnob = [&] (juce::Label& label, juce::Slider& knob)
    {
        auto k = knobs.removeFromLeft (knobW);
        label.setBounds (k.removeFromTop (14));
        knob.setBounds (k);
    };
    placeKnob (phaseLabel, phaseKnob);
    placeKnob (depthLabel, depthKnob);
    placeKnob (smoothLabel, smoothKnob);

    right.removeFromTop (6);
    targetsLabel.setBounds (right.removeFromTop (16));
    for (auto* row : rows)
    {
        row->setBounds (right.removeFromTop (25));
        right.removeFromTop (1);
    }

    // Left: canvas and tools
    auto tools = r.removeFromBottom (28);
    r.removeFromBottom (8);
    shapeEditor.setBounds (r);

    pointsTool.setBounds (tools.removeFromLeft (62));
    tools.removeFromLeft (2);
    drawTool.setBounds (tools.removeFromLeft (54));
    tools.removeFromLeft (10);
    gridBox.setBounds (tools.removeFromLeft (84));
    tools.removeFromLeft (6);
    snapToggle.setBounds (tools.removeFromLeft (64));
    randomButton.setBounds (tools.removeFromRight (70));
    tools.removeFromRight (4);
    reverseButton.setBounds (tools.removeFromRight (70));
    tools.removeFromRight (4);
    invertButton.setBounds (tools.removeFromRight (62));
}

void ShapeModEditor::updateSize()
{
    int hw = 0, hh = 0;
    if (hostedEditor != nullptr && showHosted)
    {
        hw = hostedEditor->getWidth();
        hh = hostedEditor->getHeight();
    }

    const auto w = juce::jmax (minWidth, hw);
    const auto h = hh + panelHeight;

    if (w != getWidth() || h != getHeight())
        setSize (w, h);
    else
        resized();
}

void ShapeModEditor::setStatus (const juce::String& text, bool isError)
{
    statusLabel.setColour (juce::Label::textColourId, isError ? Theme::danger : Theme::textDim);
    statusLabel.setText (text, juce::dontSendNotification);
}

void ShapeModEditor::updateHostedLabel()
{
    const auto name = processor.getHostedName();
    hostedLabel.setText (name.isEmpty() ? juce::String ("No plugin loaded") : name, juce::dontSendNotification);
    hostedLabel.setColour (juce::Label::textColourId, name.isEmpty() ? Theme::textDim : Theme::text);
    showPluginButton.setEnabled (name.isNotEmpty());
}

//==============================================================================
void ShapeModEditor::createHostedEditor()
{
    destroyHostedEditor();

    auto* plugin = processor.getHostedPlugin();
    if (plugin == nullptr)
        return;

    if (plugin->hasEditor())
        hostedEditor.reset (plugin->createEditorAndMakeActive());

    if (hostedEditor == nullptr)
    {
        hostedEditor = std::make_unique<juce::GenericAudioProcessorEditor> (*plugin);
        hostedEditor->setSize (juce::jmax (minWidth, hostedEditor->getWidth()), juce::jmin (420, hostedEditor->getHeight()));
    }

    addChildComponent (hostedEditor.get());
    hostedEditor->setVisible (showHosted);
    hostedEditor->addComponentListener (this);
    attachParamListeners();
}

void ShapeModEditor::destroyHostedEditor()
{
    detachParamListeners();

    if (hostedEditor != nullptr)
    {
        hostedEditor->removeComponentListener (this);
        removeChildComponent (hostedEditor.get());
        hostedEditor.reset();
    }
}

void ShapeModEditor::attachParamListeners()
{
    detachParamListeners();
    if (auto* plugin = processor.getHostedPlugin())
        for (auto* param : plugin->getParameters())
        {
            param->addListener (this);
            listenedParams.add (param);
        }
}

void ShapeModEditor::detachParamListeners()
{
    for (auto* param : listenedParams)
        param->removeListener (this);
    listenedParams.clear();
}

void ShapeModEditor::hostedPluginAboutToChange()
{
    learnSlot = -1;
    destroyHostedEditor();
}

void ShapeModEditor::hostedPluginChanged()
{
    if (hostedEditor == nullptr || listenedParams.isEmpty())
        createHostedEditor();

    updateHostedLabel();
    refreshRows();
    setStatus (processor.getHostedPlugin() != nullptr
                   ? juce::String ("Loaded. Click Learn on a target, then move a knob in the plugin.")
                   : juce::String ("No plugin loaded."));
    updateSize();
}

void ShapeModEditor::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    if (wasResized && &c == hostedEditor.get())
        updateSize();
}

void ShapeModEditor::timerCallback()
{
    // Pick up changes made outside the editor (e.g. Ableton recalling a preset or undo).
    undoButton.setEnabled (! undoStack.empty());

    if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        const auto ps = processor.getShape();
        if (ps != shapeEditor.getShape())
        {
            shapeEditor.setShape (ps);
            shapeNameLabel.setText (ps.name, juce::dontSendNotification);
        }
    }

    bool slotsChanged = false;
    for (int i = 0; i < ShapeModProcessor::numSlots; ++i)
    {
        const auto idx = processor.getSlot (i).paramIndex.load();
        if (idx != lastSlotParams[(size_t) i])
        {
            lastSlotParams[(size_t) i] = idx;
            slotsChanged = true;
        }
    }
    if (slotsChanged)
        refreshRows();
}

//==============================================================================
void ShapeModEditor::parameterValueChanged (int parameterIndex, float)
{
    // Only react to changes made by the user (message thread); our own modulation runs on the audio thread.
    if (learnSlot < 0 || ! onMessageThread() || processor.isParamModulated (parameterIndex))
        return;

    pendingLearnParam = parameterIndex;
    triggerAsyncUpdate();
}

void ShapeModEditor::parameterGestureChanged (int parameterIndex, bool starting)
{
    if (starting)
        parameterValueChanged (parameterIndex, 0.0f);
}

void ShapeModEditor::handleAsyncUpdate()
{
    const auto param = pendingLearnParam.exchange (-1);
    if (learnSlot < 0 || param < 0)
        return;

    const auto slot = learnSlot;
    learnSlot = -1;
    processor.assignSlot (slot, param);
    refreshRows();

    if (auto* plugin = processor.getHostedPlugin())
        setStatus ("Target " + juce::String (slot + 1) + " -> " + plugin->getParameters()[param]->getName (64));
}

void ShapeModEditor::toggleLearn (int slot)
{
    learnSlot = (learnSlot == slot) ? -1 : slot;
    refreshRows();
    if (learnSlot >= 0)
        setStatus ("Move a knob in " + processor.getHostedName() + " to assign target " + juce::String (slot + 1) + "...");
    else
        setStatus ({});
}

void ShapeModEditor::refreshRows()
{
    for (int i = 0; i < rows.size(); ++i)
    {
        rows[i]->refresh();
        rows[i]->setLearning (i == learnSlot);
    }
}

void ShapeModEditor::showParameterMenu (int slot, juce::Component& anchor)
{
    auto* plugin = processor.getHostedPlugin();
    if (plugin == nullptr)
        return;

    const auto& params = plugin->getParameters();
    const auto current = processor.getSlot (slot).paramIndex.load();

    juce::PopupMenu menu;
    menu.addItem (1, "None", true, current < 0);
    menu.addSeparator();

    auto addParam = [&] (juce::PopupMenu& m, int i)
    {
        const auto name = params[i]->getName (64);
        m.addItem (i + 2, name.isEmpty() ? "Param " + juce::String (i + 1) : name, true, i == current);
    };

    constexpr int groupSize = 50;
    if (params.size() <= 60)
    {
        for (int i = 0; i < params.size(); ++i)
            addParam (menu, i);
    }
    else
    {
        for (int startIdx = 0; startIdx < params.size(); startIdx += groupSize)
        {
            const auto endIdx = juce::jmin (startIdx + groupSize, params.size());
            juce::PopupMenu sub;
            for (int i = startIdx; i < endIdx; ++i)
                addParam (sub, i);
            menu.addSubMenu (params[startIdx]->getName (24) + "  ...  " + params[endIdx - 1]->getName (24), sub);
        }
    }

    juce::Component::SafePointer<ShapeModEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                        [safe, slot] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result == 1)
            safe->processor.clearSlot (slot);
        else
            safe->processor.assignSlot (slot, result - 2);
        safe->refreshRows();
    });
}

//==============================================================================
void ShapeModEditor::showLoadMenu()
{
    const auto files = ShapeModProcessor::findInstalledVST3s();

    juce::PopupMenu menu;
    menu.addItem (1, "Browse for a VST3...");
    if (processor.getHostedPlugin() != nullptr)
        menu.addItem (2, "Unload " + processor.getHostedName());
    menu.addSeparator();

    if (files.isEmpty())
    {
        menu.addItem (3, "No VST3 plugins found in the standard folders", false);
    }
    else if (files.size() <= 40)
    {
        for (int i = 0; i < files.size(); ++i)
            menu.addItem (100 + i, files[i].getFileNameWithoutExtension());
    }
    else
    {
        juce::String currentLetter;
        juce::PopupMenu sub;
        for (int i = 0; i < files.size(); ++i)
        {
            const auto name = files[i].getFileNameWithoutExtension();
            const auto letter = name.substring (0, 1).toUpperCase();
            if (letter != currentLetter && sub.getNumItems() > 0)
            {
                menu.addSubMenu (currentLetter, sub);
                sub = {};
            }
            currentLetter = letter;
            sub.addItem (100 + i, name);
        }
        if (sub.getNumItems() > 0)
            menu.addSubMenu (currentLetter, sub);
    }

    juce::Component::SafePointer<ShapeModEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton),
                        [safe, files] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result == 1)       safe->browseForPlugin();
        else if (result == 2)  safe->processor.unloadHostedPlugin();
        else if (result >= 100) safe->loadPluginFile (files[result - 100]);
    });
}

void ShapeModEditor::browseForPlugin()
{
   #if JUCE_MAC
    auto start = juce::File ("/Library/Audio/Plug-Ins/VST3");
   #else
    auto start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
   #endif

    fileChooser = std::make_unique<juce::FileChooser> ("Choose a VST3 plugin", start, "*.vst3");
    juce::Component::SafePointer<ShapeModEditor> safe (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr)
            return;
        const auto file = fc.getResult();
        if (file != juce::File())
            safe->loadPluginFile (file);
    });
}

void ShapeModEditor::loadPluginFile (const juce::File& file)
{
    setStatus ("Loading " + file.getFileNameWithoutExtension() + "...");
    juce::String error;
    if (! processor.loadHostedPlugin (file, error))
        setStatus (error, true);
}

//==============================================================================
void ShapeModEditor::showShapeMenu()
{
    const auto factoryNames = Shape::factoryNames();
    const auto userFiles = ShapeLibrary::list();

    juce::PopupMenu factory;
    for (int i = 0; i < factoryNames.size(); ++i)
        factory.addItem (100 + i, factoryNames[i]);

    juce::PopupMenu menu;
    menu.addSectionHeader ("My shapes");
    if (userFiles.isEmpty())
        menu.addItem (3, "(none saved yet)", false);
    for (int i = 0; i < userFiles.size(); ++i)
        menu.addItem (1000 + i, userFiles[i].getFileNameWithoutExtension());

    menu.addSeparator();
    menu.addSubMenu ("Factory shapes", factory);
    menu.addSeparator();
    menu.addItem (1, "Save shape as...");

    if (! userFiles.isEmpty())
    {
        juce::PopupMenu del;
        for (int i = 0; i < userFiles.size(); ++i)
            del.addItem (2000 + i, userFiles[i].getFileNameWithoutExtension());
        menu.addSubMenu ("Delete saved shape", del);
    }
    menu.addItem (2, "Show shapes folder");

    juce::Component::SafePointer<ShapeModEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&shapesButton),
                        [safe, factoryNames, userFiles] (int r)
    {
        if (safe == nullptr || r == 0)
            return;

        if (r == 1) { safe->saveShapeAs(); return; }
        if (r == 2) { ShapeLibrary::folder().revealToUser(); return; }

        if (r >= 100 && r < 100 + factoryNames.size())
        {
            safe->applyShape (Shape::factory (factoryNames[r - 100]), true);
            return;
        }

        if (r >= 1000 && r < 1000 + userFiles.size())
        {
            Shape s;
            if (ShapeLibrary::load (userFiles[r - 1000], s))
                safe->applyShape (s, true);
            else
                safe->setStatus ("Couldn't read " + userFiles[r - 1000].getFileName(), true);
            return;
        }

        if (r >= 2000 && r < 2000 + userFiles.size())
        {
            const auto file = userFiles[r - 2000];
            file.moveToTrash();
            safe->setStatus ("Moved \"" + file.getFileNameWithoutExtension() + "\" to the Trash");
        }
    });
}

void ShapeModEditor::saveShapeAs()
{
    auto* window = new juce::AlertWindow ("Save shape", "Name this shape:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", shapeEditor.getShape().name);
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ShapeModEditor> safe (this);
    juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, safeWindow] (int result)
    {
        if (safe == nullptr || safeWindow == nullptr || result != 1)
            return;

        auto name = safeWindow->getTextEditorContents ("name").trim();
        if (name.isEmpty())
            name = "Untitled";

        auto shape = safe->shapeEditor.getShape();
        shape.name = name;

        if (ShapeLibrary::save (shape, name))
        {
            safe->applyShape (shape, false);
            safe->setStatus ("Saved \"" + name + "\"");
        }
        else
        {
            safe->setStatus ("Couldn't save the shape", true);
        }
    }), true);
}

void ShapeModEditor::applyShape (const Shape& s, bool undoable)
{
    if (undoable)
        pushUndo();

    shapeEditor.setShape (s);
    processor.setShape (shapeEditor.getShape());
    shapeNameLabel.setText (s.name, juce::dontSendNotification);
}

void ShapeModEditor::pushUndo()
{
    undoStack.push_back (shapeEditor.getShape());
    if (undoStack.size() > 64)
        undoStack.erase (undoStack.begin());
    undoButton.setEnabled (true);
}

void ShapeModEditor::undo()
{
    if (undoStack.empty())
        return;

    auto s = undoStack.back();
    undoStack.pop_back();
    applyShape (s, false);
}
