#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include "Shape.h"

#ifndef SHAPEMOD_IS_INSTRUMENT
 #define SHAPEMOD_IS_INSTRUMENT 0
#endif

// ShapeMod: hosts one other VST3 plugin and moves up to 8 of its knobs with a drawn,
// tempo-synced shape.
class ShapeModProcessor final : public juce::AudioProcessor,
                                private juce::Timer
{
public:
    static constexpr int numSlots = 8;
    static constexpr int modChunk = 64;   // modulation update interval in samples

    // One modulation target: a parameter of the hosted plugin and the range the shape sweeps.
    struct Slot
    {
        std::atomic<int>   paramIndex { -1 };
        std::atomic<float> rangeMin { 0.0f };
        std::atomic<float> rangeMax { 1.0f };
        std::atomic<bool>  enabled { true };
        std::atomic<bool>  inverted { false };
        juce::String       paramName;          // message thread only (for state restore)
        float              lastSent = -1.0f;   // audio thread only
    };

    // Notified on the message thread around hosted-plugin changes.
    struct HostListener
    {
        virtual ~HostListener() = default;
        virtual void hostedPluginAboutToChange() = 0;
        virtual void hostedPluginChanged() = 0;
    };

    enum class Trigger { songPosition = 0, noteRetrigger, freeRun };

    ShapeModProcessor();
    ~ShapeModProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return SHAPEMOD_IS_INSTRUMENT ? "ShapeMod Instrument" : "ShapeMod FX"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return hostedTail.load(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Hosted plugin (message thread only)
    bool loadHostedPlugin (const juce::File& vst3Bundle, juce::String& error);
    bool loadHostedPlugin (const juce::PluginDescription&, juce::String& error,
                           const juce::MemoryBlock* stateToRestore = nullptr, bool keepSlots = false);
    void unloadHostedPlugin();
    juce::AudioPluginInstance* getHostedPlugin() const noexcept { return hosted.get(); }
    juce::String getHostedName() const;
    juce::String getLastLoadError() const { return lastLoadError; }

    void addHostListener (HostListener* l)    { hostListeners.add (l); }
    void removeHostListener (HostListener* l) { hostListeners.remove (l); }

    // Finds installed VST3 bundles in the standard folders.
    static juce::Array<juce::File> findInstalledVST3s();

    //==========================================================================
    // Shape (message thread writes, audio thread reads)
    Shape getShape() const;
    void setShape (const Shape&);

    // Where the current shape was loaded from: "file:<path>", "factory:<name>" or empty
    // (message thread only). Lets the editor step to the next shape in the same folder.
    juce::String getShapeSource() const { return shapeSource; }
    void setShapeSource (const juce::String& s) { shapeSource = s; }

    // Modulation targets
    Slot& getSlot (int i) noexcept { return slots[(size_t) i]; }
    void assignSlot (int slot, int paramIndex);     // captures a sensible range around the knob's value
    void clearSlot (int slot);
    bool isParamModulated (int paramIndex) const noexcept;

    // True for parameters worth offering as targets. Hides the ~2000 hidden "MIDI CC x|y"
    // parameters that JUCE-built plugins add, and anything the plugin marks as not automatable.
    static bool isUsefulTarget (const juce::AudioProcessorParameter&);
    std::vector<int> getUsefulTargetIndices() const;

    // Live display values for the editor
    float getDisplayPhase() const noexcept { return displayPhase.load(); }
    float getDisplayValue() const noexcept { return displayValue.load(); }

    // Cycle length in quarter notes for the current settings and time signature.
    double getCycleLengthInQuarterNotes (double quarterNotesPerBar) const;

    static juce::StringArray rateNames()     { return { "1/8 bar", "1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars" }; }
    static juce::StringArray rateModeNames() { return { "Straight", "Dotted", "Triplet" }; }
    static juce::StringArray triggerNames()  { return { "Song position", "Note retrigger", "Free run" }; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();
    void timerCallback() override;
    void configureHostedBuses (juce::AudioPluginInstance&) const;
    void prepareHosted (juce::AudioPluginInstance&);
    void restoreHostedFromXml (const juce::XmlElement&);
    void processHostedChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int start, int len);

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> hosted;
    juce::PluginDescription hostedDescription;
    juce::CriticalSection hostedLock;
    juce::ListenerList<HostListener> hostListeners;
    juce::String lastLoadError;
    std::atomic<double> hostedTail { 0.0 };

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    juce::AudioBuffer<float> hostBuffer;
    juce::MidiBuffer chunkMidi;

    Shape shape;
    juce::SpinLock shapeLock;
    juce::String shapeSource;

    std::array<Slot, numSlots> slots;

    // Modulation state (audio thread)
    double freePhase = 0.0;
    float smoothedValue = 0.0f;
    float lastShapeValue = 0.0f;
    bool firstBlock = true;

    std::atomic<float> displayPhase { 0.0f }, displayValue { 0.0f };

    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* rateModeParam = nullptr;
    std::atomic<float>* triggerParam = nullptr;
    std::atomic<float>* phaseParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* smoothParam = nullptr;
    std::atomic<float>* modOnParam = nullptr;

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeModProcessor)
};
