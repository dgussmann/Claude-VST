#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace wavesieve;
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;
using PID = juce::ParameterID;

juce::AudioProcessorValueTreeState::ParameterLayout WavesieveProcessor::createLayout()
{
    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    auto hz  = [] (float v, int) { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, 2) + " kHz"; };
    auto db  = [] (float v, int) { return juce::String (v, 1) + " dB"; };

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<APC> (PID ("mode", 1), "Mode", juce::StringArray { "Filter", "Shaper" }, 0));
    p.push_back (std::make_unique<APF> (PID ("frame", 1), "Frame", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    juce::NormalisableRange<float> cutoffRange (20.0f, 20000.0f);
    cutoffRange.setSkewForCentre (632.0f);
    p.push_back (std::make_unique<APF> (PID ("cutoff", 1), "Cutoff", cutoffRange, 1000.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<APF> (PID ("resonance", 1), "Resonance", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<APC> (PID ("phaseMode", 1), "Phase Mode", juce::StringArray { "Minimum", "Linear", "Original", "Raw" }, 0));

    p.push_back (std::make_unique<APF> (PID ("drive", 1), "Drive", juce::NormalisableRange<float> (-12.0f, 36.0f), 0.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<APF> (PID ("bias", 1), "Bias", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<APF> (PID ("phase", 1), "Phase", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 360.0f)) + " deg"; })));
    p.push_back (std::make_unique<APF> (PID ("smooth", 1), "Smooth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<APC> (PID ("overflow", 1), "Overflow", juce::StringArray { "Repeat", "Hold", "Mirror" }, 0));
    p.push_back (std::make_unique<APB> (PID ("dc", 1), "DC Filter", true));

    p.push_back (std::make_unique<APF> (PID ("mix", 1), "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<APF> (PID ("output", 1), "Output", juce::NormalisableRange<float> (-36.0f, 24.0f), 0.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    return { p.begin(), p.end() };
}

WavesieveProcessor::WavesieveProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "WAVESIEVE", createLayout())
{
    pMode = apvts.getRawParameterValue ("mode");
    pFrame = apvts.getRawParameterValue ("frame");
    pCutoff = apvts.getRawParameterValue ("cutoff");
    pReso = apvts.getRawParameterValue ("resonance");
    pPhaseMode = apvts.getRawParameterValue ("phaseMode");
    pDrive = apvts.getRawParameterValue ("drive");
    pBias = apvts.getRawParameterValue ("bias");
    pPhase = apvts.getRawParameterValue ("phase");
    pSmooth = apvts.getRawParameterValue ("smooth");
    pOverflow = apvts.getRawParameterValue ("overflow");
    pDc = apvts.getRawParameterValue ("dc");
    pMix = apvts.getRawParameterValue ("mix");
    pOutput = apvts.getRawParameterValue ("output");
    install (makeInitTable());
}

WavesieveProcessor::~WavesieveProcessor()
{
    loader.removeAllJobs (true, 10000);
}

bool WavesieveProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

void WavesieveProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    currentRate = sampleRate;
    currentBlock = blockSize;
    const int ch = getTotalNumOutputChannels();
    filter.prepare (sampleRate, ch);
    shaper.prepare (sampleRate, ch, blockSize);
    dryBuffer.setSize (ch, juce::jmax (blockSize, 8192));
    delayLine.setSize (ch, filter.kernelLength() + 1024);
    delayLine.clear();
    delayPos = 0;
    lastEngine = -1;
    smoothedFrame = -1.0f;
    mixSmoothed.reset (sampleRate, 0.02);
    gainSmoothed.reset (sampleRate, 0.02);
    mixSmoothed.setCurrentAndTargetValue (pMix->load());
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));

    const int mode = juce::roundToInt (pMode->load());
    const int latency = mode == 0 ? filter.latencySamples ((PhaseMode) juce::roundToInt (pPhaseMode->load())) : shaper.latencySamples();
    lastLatency = latency;
    setLatencySamples (latency);
}

void WavesieveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), delayLine.getNumChannels());
    if (numSamples == 0 || numCh == 0) return;
    if (numSamples > dryBuffer.getNumSamples())
    {
        // A host may exceed its announced block size: split rather than allocate here.
        int done = 0;
        juce::MidiBuffer none;
        while (done < numSamples)
        {
            const int n = juce::jmin (dryBuffer.getNumSamples(), numSamples - done);
            juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), done, n);
            processBlock (part, none);
            done += n;
        }
        return;
    }

    // A single NaN or Inf would otherwise stay in the oversampling filters' state for good.
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (d[i]))
                d[i] = 0.0f;
    }

    const auto* table = audioTable.load (std::memory_order_acquire);
    const int mode = juce::roundToInt (pMode->load());
    const auto phaseMode = (PhaseMode) juce::roundToInt (pPhaseMode->load());

    // Glide the frame so automation steps and mouse drags morph rather than jump.
    const float frameTarget = pFrame->load();
    const float glide = 1.0f - std::exp (-(float) numSamples / (0.01f * (float) currentRate));
    smoothedFrame = smoothedFrame < 0.0f ? frameTarget : smoothedFrame + glide * (frameTarget - smoothedFrame);
    if (std::abs (smoothedFrame - frameTarget) < 1.0e-4f) smoothedFrame = frameTarget;

    const int latency = mode == 0 ? filter.latencySamples (phaseMode) : shaper.latencySamples();
    if (mode != lastEngine)
    {
        if (mode == 0) filter.reset(); else shaper.reset();
        fadeInRemaining = lastEngine < 0 ? 0 : 512;
        lastEngine = mode;
    }
    if (latency != lastLatency.load())
    {
        lastLatency = latency;
        setLatencySamples (latency);
    }

    // The dry signal travels through a delay matching the wet path, so Mix stays phase-aligned.
    const int delaySize = delayLine.getNumSamples();
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* in = buffer.getReadPointer (ch);
        auto* line = delayLine.getWritePointer (ch);
        auto* dry = dryBuffer.getWritePointer (ch);
        int w = delayPos;
        for (int i = 0; i < numSamples; ++i)
        {
            line[w] = in[i];
            int r = w - latency;
            if (r < 0) r += delaySize;
            dry[i] = line[r];
            if (++w == delaySize) w = 0;
        }
    }
    delayPos = (delayPos + numSamples) % delaySize;

    if (mode == 0)
    {
        filter.setParameters (table, smoothedFrame, pCutoff->load(), pReso->load(), phaseMode);
        filter.process (buffer);
    }
    else
    {
        shaper.setParameters (table, smoothedFrame, pPhase->load(), pSmooth->load(),
                              juce::Decibels::decibelsToGain (pDrive->load(), -120.0f), pBias->load(),
                              (Overflow) juce::roundToInt (pOverflow->load()), pDc->load() > 0.5f);
        shaper.process (buffer);
    }

    mixSmoothed.setTargetValue (pMix->load());
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load(), -120.0f));
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mixSmoothed.getNextValue();
        const float g = gainSmoothed.getNextValue();
        float fade = 1.0f;
        if (fadeInRemaining > 0)
        {
            fade = 1.0f - (float) fadeInRemaining / 512.0f;
            --fadeInRemaining;
        }
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* wet = buffer.getWritePointer (ch);
            const float dry = dryBuffer.getSample (ch, i);
            float v = (dry + m * (wet[i] - dry)) * g * fade;
            if (! std::isfinite (v)) v = 0.0f;
            wet[i] = juce::jlimit (-16.0f, 16.0f, v);  // a safety net, not a musical limiter
        }
    }
}

void WavesieveProcessor::install (WaveTablePtr t)
{
    const juce::ScopedLock sl (tableLock);
    const auto now = juce::Time::getMillisecondCounter();
    // The audio thread reads a raw pointer once per block, so tables outlive replacement by a few seconds.
    retired.erase (std::remove_if (retired.begin(), retired.end(), [&] (const Retained& r) { return now - r.retiredAt > 3000; }), retired.end());
    if (currentTable != nullptr)
        retired.push_back ({ currentTable, now });
    currentTable = std::move (t);
    audioTable.store (currentTable.get(), std::memory_order_release);
    tableError.clear();
}

bool WavesieveProcessor::loadTableNow (const juce::File& file)
{
    ++loadGeneration;
    juce::String error;
    auto t = loadWaveTable (file, error);
    if (t == nullptr)
    {
        const juce::ScopedLock sl (tableLock);
        tableError = "Could not load " + file.getFileName() + ": " + error;
        return false;
    }
    install (std::move (t));
    return true;
}

void WavesieveProcessor::loadTableAsync (const juce::File& file)
{
    const int generation = ++loadGeneration;
    loader.addJob ([this, file, generation]
    {
        if (generation != loadGeneration.load()) return;
        juce::String error;
        auto t = loadWaveTable (file, error);
        if (generation != loadGeneration.load()) return;
        if (t != nullptr) { install (std::move (t)); return; }
        const juce::ScopedLock sl (tableLock);
        tableError = "Could not load " + file.getFileName() + ": " + error;
    });
}

WaveTablePtr WavesieveProcessor::getCurrentTable() const
{
    const juce::ScopedLock sl (tableLock);
    return currentTable;
}

juce::String WavesieveProcessor::getTableError() const
{
    const juce::ScopedLock sl (tableLock);
    return tableError;
}

void WavesieveProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    const auto t = getCurrentTable();
    state.setProperty ("tablePath", t != nullptr ? t->file.getFullPathName() : juce::String(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void WavesieveProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    const auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);
    const auto path = state.getProperty ("tablePath").toString();
    // Loaded synchronously so an offline render never starts on the wrong table.
    if (path.isNotEmpty())
        loadTableNow (juce::File (path));
}

juce::AudioProcessorEditor* WavesieveProcessor::createEditor()
{
    return new WavesieveEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WavesieveProcessor();
}
