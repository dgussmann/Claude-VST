#pragma once
#include "PluginProcessor.h"
#include "TableLibrary.h"

namespace wavesieve::ui
{
class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& title);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::String title;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

class TableView : public juce::Component
{
public:
    explicit TableView (WavesieveProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    WavesieveProcessor& proc;
    float dragStart = 0;
};

class GraphView : public juce::Component
{
public:
    explicit GraphView (WavesieveProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    void paintFilter (juce::Graphics&, const WaveTable&, juce::Rectangle<float>);
    void paintShaper (juce::Graphics&, const WaveTable&, juce::Rectangle<float>);
    WavesieveProcessor& proc;
};
} // namespace wavesieve::ui

class WavesieveEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::ListBoxModel,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    explicit WavesieveEditor (WavesieveProcessor&);
    ~WavesieveEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    /** Test hook: waits for the scan and returns how many tables the browser lists. */
    int getNumListed() const noexcept { return (int) filtered.size(); }

private:
    int getNumRows() override { return (int) filtered.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuildSources(); applyFilter(); }
    void timerCallback() override;
    void rebuildSources();
    void applyFilter();
    void step (int delta);
    void syncMode();

    WavesieveProcessor& proc;
    wavesieve::ui::Look look;
    juce::SharedResourcePointer<wavesieve::TableLibrary> library;
    std::vector<wavesieve::TableEntry> all;
    std::vector<int> filtered;
    bool suppressSelection = false;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton filterButton { "Filter" }, shaperButton { "Shaper" };
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::Label tableName;
    juce::TextEditor search;
    juce::ComboBox sourceBox;
    juce::ListBox list { "Tables", this };
    juce::TextButton addFolderButton { "Add folder" }, openButton { "Open file" }, rescanButton { "Rescan" };
    juce::Label status;

    wavesieve::ui::TableView tableView;
    wavesieve::ui::GraphView graphView;

    wavesieve::ui::Knob frameKnob, cutoffKnob, resoKnob, driveKnob, biasKnob, phaseKnob, smoothKnob, mixKnob, outputKnob;
    juce::ComboBox phaseModeBox, overflowBox;
    juce::Label phaseModeLabel, overflowLabel;
    juce::ToggleButton dcButton { "DC filter" };
    juce::AudioProcessorValueTreeState::ComboBoxAttachment phaseModeAttachment, overflowAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment dcAttachment;

    const wavesieve::WaveTable* shownTable = nullptr;
    float shownFrame = -1, shownCutoff = -1, shownReso = 9, shownPhase = -1, shownSmooth = -1, shownDrive = 99, shownBias = 9;
    int shownMode = -1, shownOverflow = -1;
    juce::String shownError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavesieveEditor)
};
