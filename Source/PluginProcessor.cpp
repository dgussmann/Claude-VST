#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr double rateBars[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
    constexpr double modeMultipliers[] = { 1.0, 1.5, 2.0 / 3.0 };

    double frac (double x) noexcept { return x - std::floor (x); }

    bool isMessageThread()
    {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        return mm != nullptr && mm->isThisTheMessageThread();
    }
}

//==============================================================================
ShapeModProcessor::BusesProperties ShapeModProcessor::makeBuses()
{
   #if SHAPEMOD_IS_INSTRUMENT
    return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
   #else
    return BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
   #endif
}

juce::AudioProcessorValueTreeState::ParameterLayout ShapeModProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 }, "Rate", rateNames(), 3));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rateMode", 1 }, "Rate Mode", rateModeNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "trigger", 1 }, "Trigger", triggerNames(), 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "phase", 1 }, "Phase",
                                                       NormalisableRange<float> (0.0f, 360.0f, 1.0f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("deg")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "depth", 1 }, "Depth",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "smooth", 1 }, "Smooth",
                                                       NormalisableRange<float> (0.0f, 200.0f, 0.1f, 0.4f), 5.0f,
                                                       AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "modOn", 1 }, "Modulation On", true));
    return layout;
}

ShapeModProcessor::ShapeModProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
   #if JUCE_PLUGINHOST_VST3
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #endif

    rateParam     = apvts.getRawParameterValue ("rate");
    rateModeParam = apvts.getRawParameterValue ("rateMode");
    triggerParam  = apvts.getRawParameterValue ("trigger");
    phaseParam    = apvts.getRawParameterValue ("phase");
    depthParam    = apvts.getRawParameterValue ("depth");
    smoothParam   = apvts.getRawParameterValue ("smooth");
    modOnParam    = apvts.getRawParameterValue ("modOn");

    shape = Shape::factory ("Sine");
    chunkMidi.ensureSize (4096);
    startTimerHz (4);
}

ShapeModProcessor::~ShapeModProcessor()
{
    *alive = false;
    stopTimer();

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (hostedLock);
        old = std::move (hosted);
    }
    if (old != nullptr)
        old->releaseResources();
}

//==============================================================================
void ShapeModProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock sl (hostedLock);
    currentSampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    currentBlockSize = juce::jmax (samplesPerBlock, modChunk);
    firstBlock = true;

    if (hosted != nullptr)
        prepareHosted (*hosted);
}

void ShapeModProcessor::releaseResources()
{
    const juce::ScopedLock sl (hostedLock);
    if (hosted != nullptr)
        hosted->releaseResources();
}

bool ShapeModProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

   #if SHAPEMOD_IS_INSTRUMENT
    return true;
   #else
    return layouts.getMainInputChannelSet() == out;
   #endif
}

double ShapeModProcessor::getCycleLengthInQuarterNotes (double quarterNotesPerBar) const
{
    const auto rateIndex = juce::jlimit (0, 5, (int) rateParam->load());
    const auto modeIndex = juce::jlimit (0, 2, (int) rateModeParam->load());
    return rateBars[rateIndex] * quarterNotesPerBar * modeMultipliers[modeIndex];
}

void ShapeModProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // Transport
    double bpm = 120.0, ppq = 0.0, qnPerBar = 4.0;
    bool playing = false, hasPpq = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm(); b.hasValue() && *b > 0.0) bpm = *b;
            if (auto p = pos->getPpqPosition(); p.hasValue()) { ppq = *p; hasPpq = true; }
            if (auto ts = pos->getTimeSignature(); ts.hasValue() && ts->numerator > 0 && ts->denominator > 0)
                qnPerBar = ts->numerator * 4.0 / ts->denominator;
            playing = pos->getIsPlaying();
        }
    }

    const auto cycleQN = juce::jmax (1.0e-3, getCycleLengthInQuarterNotes (qnPerBar));
    const auto trigger = (Trigger) juce::jlimit (0, 2, (int) triggerParam->load());
    const auto phaseOffset = phaseParam->load() / 360.0;
    const auto depth = juce::jlimit (0.0f, 1.0f, depthParam->load() / 100.0f);
    const auto smoothMs = smoothParam->load();
    const auto modOn = modOnParam->load() > 0.5f;
    const auto songSync = trigger == Trigger::songPosition && playing && hasPpq;
    const auto qnPerSample = bpm / 60.0 / currentSampleRate;

    const juce::ScopedLock sl (hostedLock);
    auto* h = hosted.get();

    if (h != nullptr)
        h->setPlayHead (getPlayHead());

    for (int start = 0; start < numSamples; start += modChunk)
    {
        const auto len = juce::jmin (modChunk, numSamples - start);

        // Note retrigger
        if (trigger == Trigger::noteRetrigger)
        {
            for (const auto meta : midi)
            {
                if (meta.samplePosition >= start && meta.samplePosition < start + len
                     && meta.getMessage().isNoteOn())
                {
                    freePhase = 0.0;
                    break;
                }
            }
        }

        double raw;
        if (songSync)
        {
            raw = (ppq + start * qnPerSample) / cycleQN;
            freePhase = frac (raw);
        }
        else
        {
            raw = freePhase;
        }

        const auto phase = (float) frac (raw + phaseOffset);
        freePhase = frac (freePhase + len * qnPerSample / cycleQN);

        {
            const juce::SpinLock::ScopedTryLockType tl (shapeLock);
            if (tl.isLocked())
                lastShapeValue = shape.evaluate (phase);
        }

        if (firstBlock || smoothMs <= 0.01f)
        {
            smoothedValue = lastShapeValue;
            firstBlock = false;
        }
        else
        {
            const auto coeff = (float) std::exp (-len / (currentSampleRate * smoothMs * 0.001));
            smoothedValue = lastShapeValue + coeff * (smoothedValue - lastShapeValue);
        }

        displayPhase.store (phase);
        displayValue.store (smoothedValue);

        if (h != nullptr)
        {
            if (modOn)
            {
                const auto& params = h->getParameters();
                const auto m = smoothedValue * depth;

                for (auto& slot : slots)
                {
                    const auto idx = slot.paramIndex.load();
                    if (idx < 0 || idx >= params.size() || ! slot.enabled.load())
                        continue;

                    const auto lo = slot.rangeMin.load();
                    const auto hi = slot.rangeMax.load();
                    const auto v = juce::jlimit (0.0f, 1.0f, slot.inverted.load() ? hi - (hi - lo) * m
                                                                                   : lo + (hi - lo) * m);
                    if (std::abs (v - slot.lastSent) > 1.0e-5f)
                    {
                        params[idx]->setValue (v);
                        slot.lastSent = v;
                    }
                }
            }

            processHostedChunk (buffer, midi, start, len);
        }
    }

   #if SHAPEMOD_IS_INSTRUMENT
    if (h == nullptr)
        buffer.clear();
   #endif

    midi.clear();
}

void ShapeModProcessor::processHostedChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int start, int len)
{
    auto& h = *hosted;
    const auto numCh = hostBuffer.getNumChannels();
    const auto hostIn = h.getMainBusNumInputChannels();
    const auto hostOut = h.getMainBusNumOutputChannels();
    const auto ourIn = getTotalNumInputChannels();
    const auto ourOut = getTotalNumOutputChannels();

    for (int ch = 0; ch < numCh; ++ch)
        hostBuffer.clear (ch, 0, len);

    if (hostIn > 0 && ourIn > 0)
    {
        if (hostIn == 1 && ourIn >= 2)
        {
            hostBuffer.addFrom  (0, 0, buffer, 0, start, len, 0.5f);
            hostBuffer.addFrom  (0, 0, buffer, 1, start, len, 0.5f);
        }
        else
        {
            for (int ch = 0; ch < juce::jmin (hostIn, ourIn); ++ch)
                hostBuffer.copyFrom (ch, 0, buffer, ch, start, len);

            if (hostIn >= 2 && ourIn == 1)
                hostBuffer.copyFrom (1, 0, buffer, 0, start, len);
        }
    }

    chunkMidi.clear();
    for (const auto meta : midi)
        if (meta.samplePosition >= start && meta.samplePosition < start + len)
            chunkMidi.addEvent (meta.data, meta.numBytes, meta.samplePosition - start);

    juce::AudioBuffer<float> view (hostBuffer.getArrayOfWritePointers(), numCh, len);

    {
        const juce::ScopedLock cl (h.getCallbackLock());
        if (h.isSuspended())
            view.clear();
        else
            h.processBlock (view, chunkMidi);
    }

    for (int ch = 0; ch < ourOut; ++ch)
    {
        if (hostOut <= 0)
            buffer.clear (ch, start, len);
        else
            buffer.copyFrom (ch, start, view, juce::jmin (ch, hostOut - 1), 0, len);
    }
}

//==============================================================================
void ShapeModProcessor::configureHostedBuses (juce::AudioPluginInstance& p) const
{
    // Ask for plain stereo on the main buses and switch off sidechains/extra outputs.
    auto layout = p.getBusesLayout();
    auto desired = layout;

    for (int i = 0; i < desired.inputBuses.size(); ++i)
        desired.inputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    for (int i = 0; i < desired.outputBuses.size(); ++i)
        desired.outputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();

    if (p.setBusesLayout (desired))
        return;

    // Fall back to stereo main buses while leaving the others as the plugin likes them.
    auto mainsOnly = layout;
    if (! mainsOnly.inputBuses.isEmpty())  mainsOnly.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! mainsOnly.outputBuses.isEmpty()) mainsOnly.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! p.setBusesLayout (mainsOnly))
        p.setBusesLayout (layout);
}

void ShapeModProcessor::prepareHosted (juce::AudioPluginInstance& p)
{
    p.setPlayHead (getPlayHead());
    p.setNonRealtime (isNonRealtime());
    p.prepareToPlay (currentSampleRate, currentBlockSize);

    const auto numCh = juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    hostBuffer.setSize (numCh, currentBlockSize, false, true, false);
}

bool ShapeModProcessor::loadHostedPlugin (const juce::File& bundle, juce::String& error)
{
    juce::OwnedArray<juce::PluginDescription> types;

    for (auto* format : formatManager.getFormats())
        if (format->fileMightContainThisPluginType (bundle.getFullPathName()))
            format->findAllTypesForFile (types, bundle.getFullPathName());

    if (types.isEmpty())
    {
        error = "Couldn't find a VST3 plugin in " + bundle.getFileName();
        lastLoadError = error;
        return false;
    }

    auto* chosen = types.getFirst();
   #if SHAPEMOD_IS_INSTRUMENT
    for (auto* t : types)
        if (t->isInstrument) { chosen = t; break; }
   #endif

    return loadHostedPlugin (*chosen, error);
}

bool ShapeModProcessor::loadHostedPlugin (const juce::PluginDescription& desc, juce::String& error,
                                          const juce::MemoryBlock* stateToRestore, bool keepSlots)
{
    jassert (isMessageThread() || juce::MessageManager::getInstanceWithoutCreating() == nullptr);

    auto instance = formatManager.createPluginInstance (desc, currentSampleRate, currentBlockSize, error);

    if (instance == nullptr)
    {
        if (error.isEmpty())
            error = "Couldn't open " + desc.name;
        lastLoadError = error;
        return false;
    }

    configureHostedBuses (*instance);

    if (stateToRestore != nullptr && stateToRestore->getSize() > 0)
        instance->setStateInformation (stateToRestore->getData(), (int) stateToRestore->getSize());

    hostListeners.call ([] (HostListener& l) { l.hostedPluginAboutToChange(); });

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (hostedLock);
        prepareHosted (*instance);
        old = std::move (hosted);
        hosted = std::move (instance);
        hostedDescription = desc;

        for (auto& s : slots)
        {
            s.lastSent = -1.0f;
            if (! keepSlots)
            {
                s.paramIndex = -1;
                s.paramName = {};
            }
        }
    }

    if (old != nullptr)
    {
        old->releaseResources();
        old.reset();
    }

    lastLoadError = {};
    setLatencySamples (hosted->getLatencySamples());
    hostedTail = hosted->getTailLengthSeconds();
    hostListeners.call ([] (HostListener& l) { l.hostedPluginChanged(); });
    updateHostDisplay();
    return true;
}

void ShapeModProcessor::unloadHostedPlugin()
{
    if (hosted == nullptr)
        return;

    hostListeners.call ([] (HostListener& l) { l.hostedPluginAboutToChange(); });

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (hostedLock);
        old = std::move (hosted);
        hostedDescription = {};
    }

    old->releaseResources();
    old.reset();

    for (int i = 0; i < numSlots; ++i)
        clearSlot (i);

    setLatencySamples (0);
    hostedTail = 0.0;
    hostListeners.call ([] (HostListener& l) { l.hostedPluginChanged(); });
    updateHostDisplay();
}

juce::String ShapeModProcessor::getHostedName() const
{
    return hosted != nullptr ? hosted->getName() : juce::String();
}

juce::Array<juce::File> ShapeModProcessor::findInstalledVST3s()
{
    juce::Array<juce::File> roots;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

   #if JUCE_MAC
    roots.add (home.getChildFile ("Library/Audio/Plug-Ins/VST3"));
    roots.add (juce::File ("/Library/Audio/Plug-Ins/VST3"));
   #elif JUCE_WINDOWS
    roots.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files/VST3"));
   #else
    roots.add (home.getChildFile (".vst3"));
    roots.add (juce::File ("/usr/lib/vst3"));
    roots.add (juce::File ("/usr/local/lib/vst3"));
   #endif

    juce::Array<juce::File> found;

    std::function<void (const juce::File&, int)> scan = [&] (const juce::File& dir, int depth)
    {
        for (const auto& child : dir.findChildFiles (juce::File::findDirectories | juce::File::findFiles, false))
        {
            if (child.hasFileExtension ("vst3"))
            {
                if (! child.getFileName().startsWithIgnoreCase ("ShapeMod"))
                    found.addIfNotAlreadyThere (child);
            }
            else if (child.isDirectory() && depth < 3)
            {
                scan (child, depth + 1);
            }
        }
    };

    for (const auto& r : roots)
        if (r.isDirectory())
            scan (r, 0);

    std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0;
    });

    return found;
}

//==============================================================================
Shape ShapeModProcessor::getShape() const
{
    const juce::SpinLock::ScopedLockType sl (shapeLock);
    return shape;
}

void ShapeModProcessor::setShape (const Shape& s)
{
    auto copy = s;
    copy.normalise();
    const juce::SpinLock::ScopedLockType sl (shapeLock);
    shape = std::move (copy);
}

void ShapeModProcessor::assignSlot (int slotIndex, int paramIndex)
{
    if (! juce::isPositiveAndBelow (slotIndex, numSlots) || hosted == nullptr)
        return;

    const auto& params = hosted->getParameters();
    if (! juce::isPositiveAndBelow (paramIndex, params.size()))
        return;

    auto& s = slots[(size_t) slotIndex];
    const auto value = params[paramIndex]->getValue();
    const auto lo = juce::jlimit (0.0f, 0.5f, value - 0.25f);

    s.paramIndex = -1;
    s.rangeMin = lo;
    s.rangeMax = lo + 0.5f;
    s.inverted = false;
    s.enabled = true;
    s.paramName = params[paramIndex]->getName (128);
    s.lastSent = -1.0f;
    s.paramIndex = paramIndex;
}

void ShapeModProcessor::clearSlot (int slotIndex)
{
    if (! juce::isPositiveAndBelow (slotIndex, numSlots))
        return;

    auto& s = slots[(size_t) slotIndex];
    s.paramIndex = -1;
    s.paramName = {};
    s.lastSent = -1.0f;
}

bool ShapeModProcessor::isParamModulated (int paramIndex) const noexcept
{
    for (const auto& s : slots)
        if (s.paramIndex.load() == paramIndex)
            return true;
    return false;
}

bool ShapeModProcessor::isUsefulTarget (const juce::AudioProcessorParameter& param)
{
    if (! param.isAutomatable())
        return false;

    const auto name = param.getName (64).trim();
    if (name.isEmpty())
        return false;

    // "MIDI CC 3|74" style controller-emulation parameters
    if (name.startsWith ("MIDI CC ") && name.containsChar ('|')
         && name.substring (8).removeCharacters ("|").containsOnly ("0123456789"))
        return false;

    return true;
}

std::vector<int> ShapeModProcessor::getUsefulTargetIndices() const
{
    std::vector<int> result;
    if (hosted == nullptr)
        return result;

    const auto& params = hosted->getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (isUsefulTarget (*params[i]))
            result.push_back (i);

    // Some plugins mark nothing as automatable; fall back to everything rather than an empty list.
    if (result.empty())
        for (int i = 0; i < params.size(); ++i)
            result.push_back (i);

    return result;
}

void ShapeModProcessor::timerCallback()
{
    if (hosted == nullptr)
        return;

    const auto latency = hosted->getLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);

    hostedTail = hosted->getTailLengthSeconds();
}

//==============================================================================
void ShapeModProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("ShapeModState");
    xml.setAttribute ("version", 1);

    if (auto params = apvts.copyState().createXml())
        xml.addChildElement (params.release());

    xml.createNewChildElement ("Shape")->setAttribute ("json", getShape().toJSON());

    auto* slotsEl = xml.createNewChildElement ("Slots");
    for (int i = 0; i < numSlots; ++i)
    {
        const auto& s = slots[(size_t) i];
        auto* e = slotsEl->createNewChildElement ("Slot");
        e->setAttribute ("index", i);
        e->setAttribute ("param", s.paramIndex.load());
        e->setAttribute ("name", s.paramName);
        e->setAttribute ("min", (double) s.rangeMin.load());
        e->setAttribute ("max", (double) s.rangeMax.load());
        e->setAttribute ("enabled", s.enabled.load());
        e->setAttribute ("inverted", s.inverted.load());
    }

    const juce::ScopedLock sl (hostedLock);
    if (hosted != nullptr)
    {
        auto* hostedEl = xml.createNewChildElement ("Hosted");
        hostedEl->addChildElement (hostedDescription.createXml().release());

        juce::MemoryBlock state;
        hosted->getStateInformation (state);
        hostedEl->setAttribute ("state", state.toBase64Encoding());
    }

    copyXmlToBinary (xml, destData);
}

void ShapeModProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("ShapeModState"))
        return;

    if (auto* params = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    if (auto* shapeEl = xml->getChildByName ("Shape"))
        setShape (Shape::fromJSON (shapeEl->getStringAttribute ("json")));

    if (auto* slotsEl = xml->getChildByName ("Slots"))
    {
        for (auto* e : slotsEl->getChildIterator())
        {
            const auto i = e->getIntAttribute ("index", -1);
            if (! juce::isPositiveAndBelow (i, numSlots))
                continue;

            auto& s = slots[(size_t) i];
            s.rangeMin = (float) e->getDoubleAttribute ("min", 0.0);
            s.rangeMax = (float) e->getDoubleAttribute ("max", 1.0);
            s.enabled = e->getBoolAttribute ("enabled", true);
            s.inverted = e->getBoolAttribute ("inverted", false);
            s.paramName = e->getStringAttribute ("name");
            s.lastSent = -1.0f;
            s.paramIndex = e->getIntAttribute ("param", -1);
        }
    }

    if (auto* hostedEl = xml->getChildByName ("Hosted"))
    {
        auto copy = std::make_shared<juce::XmlElement> (*hostedEl);

        if (isMessageThread() || juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        {
            restoreHostedFromXml (*copy);
        }
        else
        {
            std::weak_ptr<bool> weakAlive = alive;
            juce::MessageManager::callAsync ([this, copy, weakAlive]
            {
                if (auto a = weakAlive.lock(); a != nullptr && *a)
                    restoreHostedFromXml (*copy);
            });
        }
    }
}

void ShapeModProcessor::restoreHostedFromXml (const juce::XmlElement& hostedEl)
{
    juce::PluginDescription desc;
    auto* descEl = hostedEl.getChildByName ("PLUGIN");
    if (descEl == nullptr || ! desc.loadFromXml (*descEl))
        return;

    juce::MemoryBlock state;
    state.fromBase64Encoding (hostedEl.getStringAttribute ("state"));

    juce::String error;
    if (! loadHostedPlugin (desc, error, &state, true))
        return;

    // Re-resolve each target by name in case the plugin's parameter order changed.
    const auto& params = hosted->getParameters();
    for (auto& s : slots)
    {
        const auto idx = s.paramIndex.load();
        if (idx < 0 || s.paramName.isEmpty())
            continue;

        if (juce::isPositiveAndBelow (idx, params.size()) && params[idx]->getName (128) == s.paramName)
            continue;

        int found = -1;
        for (int p = 0; p < params.size(); ++p)
            if (params[p]->getName (128) == s.paramName) { found = p; break; }

        s.paramIndex = found;
    }

    hostListeners.call ([] (HostListener& l) { l.hostedPluginChanged(); });
}

//==============================================================================
juce::AudioProcessorEditor* ShapeModProcessor::createEditor()
{
    return new ShapeModEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ShapeModProcessor();
}
