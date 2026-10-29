#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "ShapeEditor.h"
#include "Theme.h"

class ShapeModEditor;

// One modulation target row: on/off, parameter, Learn, range, invert, clear.
class TargetRow final : public juce::Component
{
public:
    TargetRow (ShapeModEditor& owner, int index);

    void refresh();
    void setLearning (bool);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    ShapeModEditor& editor;
    const int slotIndex;

    juce::ToggleButton enableToggle;
    juce::TextButton paramButton, learnButton { "Learn" }, invertButton { "Inv" }, clearButton { "x" };
    juce::Slider range { juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox };
};

class ShapeModEditor final : public juce::AudioProcessorEditor,
                             private ShapeModProcessor::HostListener,
                             private juce::ComponentListener,
                             private juce::AudioProcessorParameter::Listener,
                             private juce::AsyncUpdater,
                             private juce::Timer
{
public:
    explicit ShapeModEditor (ShapeModProcessor&);
    ~ShapeModEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    ShapeModProcessor& getProcessor() noexcept { return processor; }

    // Called by TargetRow
    void showParameterMenu (int slot, juce::Component& anchor);
    void toggleLearn (int slot);
    void refreshRows();

private:
    // HostListener
    void hostedPluginAboutToChange() override;
    void hostedPluginChanged() override;

    // ComponentListener (hosted editor resizing)
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;

    // Hosted parameter listener (for Learn)
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void createHostedEditor();
    void destroyHostedEditor();
    void attachParamListeners();
    void detachParamListeners();
    void updateSize();
    void updateHostedLabel();
    void setStatus (const juce::String&, bool isError = false);

    void showLoadMenu();
    void browseForPlugin();
    void loadPluginFile (const juce::File&);
    void showShapeMenu();
    void saveShapeAs();
    void applyShape (const Shape&, bool pushUndo);
    bool loadShapeFromSource (const juce::String& source, bool pushUndo);
    void stepShape (int direction);
    void updateShapeLabel (bool force = false);
    static juce::Array<juce::File> siblingsOf (const juce::File& shapeFile);
    void pushUndo();
    void undo();

    ShapeModProcessor& processor;
    ShapeModLookAndFeel lnf;

    std::unique_ptr<juce::AudioProcessorEditor> hostedEditor;
    juce::Array<juce::AudioProcessorParameter*> listenedParams;
    bool showHosted = true;

    // Top bar
    juce::TextButton loadButton { "Load plugin" }, showPluginButton { "Hide plugin" };
    juce::Label hostedLabel, statusLabel;
    juce::TextButton shapesButton { "Shapes" }, saveButton { "Save" }, undoButton { "Undo" };
    juce::TextButton prevShapeButton, nextShapeButton;
    juce::String labelSource, labelPosition;
    juce::Label shapeNameLabel;

    // Shape
    ShapeEditor shapeEditor;
    juce::TextButton pointsTool { "Points" }, drawTool { "Draw" };
    juce::ComboBox gridBox;
    juce::ToggleButton snapToggle { "Snap" };
    juce::TextButton invertButton { "Invert" }, reverseButton { "Reverse" }, randomButton { "Random" };

    // Timing and output
    juce::ComboBox rateBox, rateModeBox, triggerBox;
    juce::Label rateLabel { {}, "RATE" }, modeLabel { {}, "FEEL" }, triggerLabel { {}, "SYNC" };
    juce::Slider phaseKnob, depthKnob, smoothKnob;
    juce::Label phaseLabel { {}, "PHASE" }, depthLabel { {}, "DEPTH" }, smoothLabel { {}, "SMOOTH" };
    juce::TextButton modOnButton { "Mod On" };
    juce::Label targetsLabel { {}, "TARGETS" };

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ComboAttachment> rateAttach, rateModeAttach, triggerAttach;
    std::unique_ptr<SliderAttachment> phaseAttach, depthAttach, smoothAttach;
    std::unique_ptr<ButtonAttachment> modOnAttach;

    juce::OwnedArray<TargetRow> rows;

    std::vector<Shape> undoStack;
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::array<int, ShapeModProcessor::numSlots> lastSlotParams {};
    int learnSlot = -1;
    std::atomic<int> pendingLearnParam { -1 };
    juce::Rectangle<int> panelBounds;

    static constexpr int panelHeight = 462;
    static constexpr int minWidth = 980;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeModEditor)
};
