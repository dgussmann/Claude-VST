#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "WaveTable.h"
#include "FilterEngine.h"
#include "ShaperEngine.h"

class WavesieveProcessor : public juce::AudioProcessor
{
public:
    WavesieveProcessor();
    ~WavesieveProcessor() override;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Wavesieve"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Loads on a background thread; the newest request wins. */
    void loadTableAsync (const juce::File&);
    bool loadTableNow (const juce::File&);
    wavesieve::WaveTablePtr getCurrentTable() const;
    juce::String getTableError() const;
    int getCurrentLatency() const noexcept { return lastLatency.load(); }
    void waitForLoads() { loader.removeAllJobs (false, 10000); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void install (wavesieve::WaveTablePtr);

    wavesieve::FilterEngine filter;
    wavesieve::ShaperEngine shaper;

    mutable juce::CriticalSection tableLock;
    struct Retained { wavesieve::WaveTablePtr table; juce::uint32 retiredAt; };
    std::vector<Retained> retired;
    wavesieve::WaveTablePtr currentTable;
    juce::String tableError;
    std::atomic<const wavesieve::WaveTable*> audioTable { nullptr };
    std::atomic<int> loadGeneration { 0 };
    juce::ThreadPool loader { 1 };

    juce::AudioBuffer<float> dryBuffer, delayLine;
    int delayPos = 0;
    double currentRate = 44100.0;
    int currentBlock = 512;
    int lastEngine = -1;
    std::atomic<int> lastLatency { 0 };
    int fadeInRemaining = 0;
    float smoothedFrame = -1.0f;
    juce::SmoothedValue<float> mixSmoothed, gainSmoothed;

    std::atomic<float>* pMode, *pFrame, *pCutoff, *pReso, *pPhaseMode, *pDrive, *pBias, *pPhase, *pSmooth, *pOverflow, *pDc, *pMix, *pOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavesieveProcessor)
};
