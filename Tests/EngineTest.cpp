// Headless end-to-end test: shape maths, tempo sync, hosting a real VST3 and moving its knob,
// audio pass-through, and state save/restore.
// Usage: ShapeModTest "/path/to/ShapeMod FX.vst3"

#include "../Source/PluginProcessor.h"
#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        if (! ok) ++failures;
    }

    bool near (double a, double b, double tol = 1.0e-3) { return std::abs (a - b) <= tol; }

    struct MockPlayHead final : juce::AudioPlayHead
    {
        double ppq = 0.0, bpm = 120.0;
        bool playing = true;
        int num = 4, den = 4;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (playing);
            info.setTimeSignature (TimeSignature { num, den });
            return info;
        }
    };

    void setParam (ShapeModProcessor& p, const char* id, float plainValue)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    int findParam (juce::AudioPluginInstance& plugin, const juce::String& name)
    {
        const auto& params = plugin.getParameters();
        for (int i = 0; i < params.size(); ++i)
            if (params[i]->getName (64) == name)
                return i;
        return -1;
    }

    double expectedSaw (double ppqAtChunk, double cycleQN)
    {
        const auto raw = ppqAtChunk / cycleQN;
        return raw - std::floor (raw);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "Shape maths" << std::endl;
    {
        auto saw = Shape::factory ("Saw Up");
        check (near (saw.evaluate (0.25f), 0.25), "saw up at 1/4 = 0.25");
        auto sq = Shape::factory ("Square");
        check (near (sq.evaluate (0.49f), 1.0) && near (sq.evaluate (0.5f), 0.0), "square jumps at the half");
        auto sine = Shape::factory ("Sine");
        check (near (sine.evaluate (0.25f), 1.0, 0.01) && near (sine.evaluate (0.75f), 0.0, 0.01), "sine peaks and troughs");
        auto expRise = Shape::factory ("Exp Rise");
        check (expRise.evaluate (0.5f) < 0.3f, "curved segment bends");
        auto round = Shape::fromJSON (expRise.toJSON());
        check (round == expRise && round.name == "Exp Rise", "JSON round trip");
        auto rev = saw; rev.reverse();
        check (near (rev.evaluate (0.25f), 0.75), "reverse");
        auto inv = saw; inv.invert();
        check (near (inv.evaluate (0.25f), 0.75), "invert");
        auto steps = Shape::fromSteps ({ 0.1f, 0.9f }, "S");
        check (near (steps.evaluate (0.2f), 0.1) && near (steps.evaluate (0.7f), 0.9), "steps");
    }

    std::cout << "Rate lengths (4/4)" << std::endl;
    {
        ShapeModProcessor p;
        const double expected[] = { 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        bool allOk = true;
        for (int i = 0; i < 6; ++i)
        {
            setParam (p, "rate", (float) i);
            allOk = allOk && near (p.getCycleLengthInQuarterNotes (4.0), expected[i]);
        }
        check (allOk, "1/8 bar .. 4 bars = 0.5 .. 16 quarter notes");
        setParam (p, "rate", 3.0f);
        setParam (p, "rateMode", 2.0f);
        check (near (p.getCycleLengthInQuarterNotes (4.0), 4.0 * 2.0 / 3.0), "triplet 1 bar");
        setParam (p, "rateMode", 1.0f);
        check (near (p.getCycleLengthInQuarterNotes (3.0), 4.5), "dotted 1 bar in 3/4");
    }

    if (argc < 2)
    {
        std::cout << "No plugin path given; skipping hosting tests" << std::endl;
        return failures == 0 ? 0 : 1;
    }

    const juce::File pluginFile { juce::String (argv[1]) };
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    std::cout << "Hosting " << pluginFile.getFileName() << std::endl;

    MockPlayHead playHead;
    ShapeModProcessor proc;
    proc.setPlayHead (&playHead);
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    setParam (proc, "smooth", 0.0f);
    proc.setShape (Shape::factory ("Saw Up"));

    juce::String error;
    const bool loaded = proc.loadHostedPlugin (pluginFile, error);
    check (loaded, "load hosted VST3" + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));
    if (! loaded)
        return 1;

    auto* hosted = proc.getHostedPlugin();
    const auto depthIndex = findParam (*hosted, "Depth");
    check (depthIndex >= 0, "found hosted parameter \"Depth\"");

    proc.assignSlot (0, depthIndex);
    proc.getSlot (0).rangeMin = 0.0f;
    proc.getSlot (0).rangeMax = 1.0f;

    // Run 3 bars at 120 bpm and check the hosted knob follows the saw (1 bar = 4 quarter notes).
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    const auto qnPerSample = playHead.bpm / 60.0 / sr;
    double maxError = 0.0;
    int checks = 0;

    for (int b = 0; b < (int) (3 * 4 / (block * qnPerSample)); ++b)
    {
        playHead.ppq = b * block * qnPerSample;
        buffer.clear();
        proc.processBlock (buffer, midi);

        const auto lastChunkStart = block - ShapeModProcessor::modChunk;
        const auto expected = expectedSaw (playHead.ppq + lastChunkStart * qnPerSample, 4.0);
        const auto actual = hosted->getParameters()[depthIndex]->getValue();
        // The hosted "Depth" parameter is stepped in 0.1% increments, so allow a small error.
        if (expected > 0.01 && expected < 0.99)
        {
            maxError = juce::jmax (maxError, std::abs (actual - expected));
            ++checks;
        }
    }
    check (checks > 100 && maxError < 0.002, "hosted knob follows the shape in sync (max error "
                                              + juce::String (maxError, 5) + " over " + juce::String (checks) + " blocks)");

    // Depth and invert
    setParam (proc, "depth", 50.0f);
    proc.getSlot (0).inverted = true;
    playHead.ppq = 1.0;   // quarter of a bar -> saw = 0.25 (+ last chunk offset)
    buffer.clear();
    proc.processBlock (buffer, midi);
    {
        const auto shapeVal = expectedSaw (1.0 + (block - ShapeModProcessor::modChunk) * qnPerSample, 4.0);
        const auto expected = 1.0 - shapeVal * 0.5;
        check (near (hosted->getParameters()[depthIndex]->getValue(), expected, 0.002), "depth 50% + invert");
    }
    setParam (proc, "depth", 100.0f);
    proc.getSlot (0).inverted = false;

    // Audio passes through the hosted plugin (ShapeMod FX with nothing loaded passes audio straight through).
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                buffer.setSample (ch, i, (float) std::sin (i * 0.05 + ch));
        juce::AudioBuffer<float> original (buffer);
        proc.processBlock (buffer, midi);
        float diff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                diff = juce::jmax (diff, std::abs (buffer.getSample (ch, i) - original.getSample (ch, i)));
        check (diff < 1.0e-6f, "audio passes through the hosted plugin");
    }

    // Phase offset of 90 degrees shifts the saw by a quarter.
    setParam (proc, "phase", 90.0f);
    playHead.ppq = 0.0;
    proc.processBlock (buffer, midi);
    {
        const auto expected = expectedSaw ((block - ShapeModProcessor::modChunk) * qnPerSample, 4.0) + 0.25;
        check (near (hosted->getParameters()[depthIndex]->getValue(), expected, 0.002), "phase offset 90 degrees");
    }

    // Save and restore into a fresh instance.
    juce::MemoryBlock state;
    proc.getStateInformation (state);

    ShapeModProcessor restored;
    restored.setPlayHead (&playHead);
    restored.setPlayConfigDetails (2, 2, sr, block);
    restored.prepareToPlay (sr, block);
    restored.setStateInformation (state.getData(), (int) state.getSize());

    check (restored.getHostedPlugin() != nullptr, "state restore reloads the hosted plugin");
    check (restored.getSlot (0).paramIndex.load() == depthIndex, "state restore keeps the target");
    check (restored.getShape() == Shape::factory ("Saw Up"), "state restore keeps the shape");
    check (near (restored.apvts.getRawParameterValue ("phase")->load(), 90.0), "state restore keeps parameters");

    // Free-run mode keeps moving with the transport stopped.
    setParam (proc, "phase", 0.0f);
    setParam (proc, "trigger", 2.0f);
    playHead.playing = false;
    proc.processBlock (buffer, midi);
    const auto v1 = hosted->getParameters()[depthIndex]->getValue();
    proc.processBlock (buffer, midi);
    const auto v2 = hosted->getParameters()[depthIndex]->getValue();
    check (std::abs (v2 - v1) > 0.001f, "free run moves while the transport is stopped");

    proc.releaseResources();
    restored.releaseResources();

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " TEST(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
