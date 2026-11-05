// Headless checks for Wavesieve. Usage: WavesieveTest [--samples DIR] [--vst3 PATH] [--snapshot PREFIX]
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/TableLibrary.h"
#include <cstdio>
#include <map>
#include <random>

using namespace wavesieve;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; printf ("  FAIL: " __VA_ARGS__); printf ("\n"); } } while (0)

static void setP (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s, const char* id, float value)
{
    juce::ignoreUnused (p);
    auto* param = s.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

static double rms (const float* d, int n) { double s = 0; for (int i = 0; i < n; ++i) s += d[i] * d[i]; return std::sqrt (s / juce::jmax (1, n)); }

// Measures the plugin's gain at one frequency once any latency and transients are over.
static double gainAt (WavesieveProcessor& proc, double hz, double fs)
{
    proc.prepareToPlay (fs, 512);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    double phase = 0, in = 0, out = 0;
    const int skip = 40, keep = 40;
    for (int b = 0; b < skip + keep; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float v = (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * hz / fs;
            buf.setSample (0, i, v); buf.setSample (1, i, v);
            if (b >= skip) in += (double) v * v;
        }
        proc.processBlock (buf, midi);
        if (b >= skip) for (int i = 0; i < 512; ++i) out += (double) buf.getSample (0, i) * buf.getSample (0, i);
    }
    return std::sqrt (out / in);
}

static void testLoading (const juce::File& dir)
{
    printf ("[loading] %s\n", dir.getFullPathName().toRawUTF8());
    std::map<juce::String, std::pair<int,int>> counts;
    juce::StringArray failed;
    double slowest = 0;
    for (const auto& it : juce::RangedDirectoryIterator (dir, true, "*.wav;*.flac;*.wt;*.vitaltable;*.vital"))
    {
        juce::String error;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto t = loadWaveTable (it.getFile(), error);
        slowest = juce::jmax (slowest, juce::Time::getMillisecondCounterHiRes() - start);
        auto& c = counts[it.getFile().getFileExtension().toLowerCase()];
        ++c.first;
        if (t == nullptr) { ++c.second; failed.add (it.getFile().getFileName() + ": " + error); continue; }
        CHECK (t->numFrames >= 1 && t->numFrames <= kMaxFrames, "%s frames=%d", it.getFile().getFileName().toRawUTF8(), t->numFrames);
        float peak = 0; bool finite = true;
        for (auto v : t->samples) { peak = juce::jmax (peak, std::abs (v)); finite = finite && std::isfinite (v); }
        CHECK (finite && std::abs (peak - 1.0f) < 1.0e-3f, "%s peak=%f", it.getFile().getFileName().toRawUTF8(), peak);
    }
    for (auto& [ext, c] : counts) printf ("  %-12s %5d loaded, %d failed\n", ext.toRawUTF8(), c.first - c.second, c.second);
    printf ("  slowest load: %.0f ms\n", slowest);
    for (int i = 0; i < juce::jmin (15, failed.size()); ++i) printf ("  - %s\n", failed[i].toRawUTF8());
    failures += failed.size();

    // A table written as a transfer curve must pass quiet signal almost unchanged at its first frame.
    for (const auto& it : juce::RangedDirectoryIterator (dir, true, "Simple Sym.flac"))
    {
        WavesieveProcessor p;
        CHECK (p.loadTableNow (it.getFile()), "load curve table");
        setP (p, p.apvts, "mode", 1);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer m;
        double in = 0, out = 0, phase = 0;
        for (int b = 0; b < 60; ++b)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.1f * (float) std::sin (phase); phase += 0.05; buf.setSample (0, i, v); buf.setSample (1, i, v); if (b > 20) in += (double) v * v; }
            p.processBlock (buf, m);
            if (b > 20) for (int i = 0; i < 512; ++i) out += (double) buf.getSample (0, i) * buf.getSample (0, i);
        }
        const double ratio = std::sqrt (out / in);
        printf ("  curve table small-signal gain %.3f\n", ratio);
        CHECK (ratio > 0.5 && ratio < 2.0, "curve mapping gain %f", ratio);
    }

    const auto entries = TableLibrary::scan ({ { dir, "Test", true } });
    printf ("  library scan lists %d tables\n", (int) entries.size());
}

static void testFormats()
{
    printf ("[formats]\n");
    const auto dir = juce::File::createTempFile ("wv"); dir.deleteFile(); dir.createDirectory();

    // .wt, float32, 2 frames of 256 samples
    {
        const auto f = dir.getChildFile ("a.wt");
        juce::FileOutputStream o (f);
        o.write ("vawt", 4); o.writeInt (256); o.writeShort (2); o.writeShort (0);
        for (int fr = 0; fr < 2; ++fr) for (int i = 0; i < 256; ++i) o.writeFloat ((float) std::sin (juce::MathConstants<double>::twoPi * (fr + 1) * i / 256.0));
        o.flush();
        juce::String e; const auto t = loadWaveTable (f, e);
        CHECK (t != nullptr && t->numFrames == 2, ".wt parse: %s", e.toRawUTF8());
        if (t != nullptr)
        {
            auto m = [&] (int fr, int h) { auto* s = t->spectra (fr); return std::hypot (s[h * 2], s[h * 2 + 1]); };
            CHECK (m (0, 1) > 0.95 && m (1, 2) > 0.95 && m (0, 2) < 0.05, ".wt harmonics wrong: %f %f %f", m (0,1), m (1,2), m (0,2));
        }
    }
    // .flac with no metadata: 3 frames of 2048 samples
    {
        const auto f = dir.getChildFile ("c.flac");
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (new juce::FileOutputStream (f), 44100.0, 1, 24, {}, 0));
            CHECK (w != nullptr, "flac writer");
            juce::AudioBuffer<float> b (1, 2048 * 3);
            for (int fr = 0; fr < 3; ++fr)
                for (int i = 0; i < 2048; ++i)
                    b.setSample (0, fr * 2048 + i, (float) std::sin (juce::MathConstants<double>::twoPi * (fr + 1) * i / 2048.0));
            if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        juce::String e; const auto t = loadWaveTable (f, e);
        CHECK (t != nullptr && t->numFrames == 3, "flac parse: %s", e.toRawUTF8());
        if (t != nullptr) { auto* s2 = t->spectra (2); CHECK (std::hypot (s2[6], s2[7]) > 0.95, "flac harmonic 3 missing"); }
    }
    // .vitaltable with a Wave Source
    {
        juce::MemoryBlock mb (2048 * sizeof (float));
        auto* d = (float*) mb.getData();
        for (int i = 0; i < 2048; ++i) d[i] = (float) std::sin (juce::MathConstants<double>::twoPi * 3 * i / 2048.0);
        const auto b64 = juce::Base64::toBase64 (mb.getData(), mb.getSize());
        const auto f = dir.getChildFile ("b.vitaltable");
        f.replaceWithText ("{\"name\":\"x\",\"groups\":[{\"components\":[{\"type\":\"Wave Source\",\"interpolation_style\":1,\"keyframes\":[{\"position\":0,\"wave_data\":\"" + b64 + "\"},{\"position\":256,\"wave_data\":\"" + b64 + "\"}]}]}]}");
        juce::String e; const auto t = loadWaveTable (f, e);
        CHECK (t != nullptr && t->numFrames == 256, "vitaltable: %s", e.toRawUTF8());
        if (t != nullptr) { auto* s = t->spectra (100); CHECK (std::hypot (s[6], s[7]) > 0.95, "vital harmonic 3 missing"); }
    }
    dir.deleteRecursively();
}

// A table with one narrow peak at harmonic 24: the filter must pass the cutoff and reject elsewhere.
static void writeSinglePeakTable (const juce::File& f)
{
    juce::FileOutputStream o (f);
    o.write ("vawt", 4); o.writeInt (2048); o.writeShort (1); o.writeShort (0);
    for (int i = 0; i < 2048; ++i) o.writeFloat ((float) std::sin (juce::MathConstants<double>::twoPi * 24 * i / 2048.0));
    o.flush();
}

static void testFilterResponse()
{
    printf ("[filter response]\n");
    const auto f = juce::File::createTempFile ("peak.wt"); f.deleteFile();
    writeSinglePeakTable (f);
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int mode : { 0, 1, 2 })
        {
            WavesieveProcessor p;
            CHECK (p.loadTableNow (f), "load");
            setP (p, p.apvts, "mode", 0);
            setP (p, p.apvts, "cutoff", 1000.0f);
            setP (p, p.apvts, "phaseMode", (float) mode);
            const double atCut = gainAt (p, 1000.0, fs), off = gainAt (p, 3000.0, fs), low = gainAt (p, 200.0, fs);
            printf ("  fs=%6.0f mode=%d  1k=%.3f 3k=%.5f 200=%.5f\n", fs, mode, atCut, off, low);
            CHECK (atCut > 0.8 && atCut < 1.2, "peak gain %f", atCut);
            CHECK (off < 0.02 && low < 0.02, "rejection %f %f", off, low);
        }
    // Moving cutoff moves the peak
    {
        WavesieveProcessor p; p.loadTableNow (f);
        setP (p, p.apvts, "cutoff", 4000.0f);
        CHECK (gainAt (p, 4000.0, 48000.0) > 0.8, "peak at 4k");
        CHECK (gainAt (p, 1000.0, 48000.0) < 0.02, "1k rejected when cutoff=4k");
    }
    f.deleteFile();
}

// A delayed impulse must match the reported latency, and dry + wet must stay aligned.
static void testLatency()
{
    printf ("[latency]\n");
    for (int mode : { 0, 1, 2 })
    {
        WavesieveProcessor p;
        setP (p, p.apvts, "mode", 0);
        setP (p, p.apvts, "phaseMode", (float) mode);
        setP (p, p.apvts, "mix", 0.0f);  // fully dry through the compensating delay
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer m;
        buf.clear(); p.processBlock (buf, m);  // applies the mode's latency
        const int L = p.getLatencySamples();
        int found = -1, n = 0;
        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            if (b == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); }
            p.processBlock (buf, m);
            for (int i = 0; i < 256; ++i, ++n)
                if (found < 0 && std::abs (buf.getSample (0, i)) > 0.5f) found = n;
        }
        printf ("  phase mode %d: latency %d, dry impulse at %d\n", mode, L, found);
        CHECK (found == L, "dry alignment %d vs %d", found, L);
    }
}

static void testPartitionAlignment()
{
    printf ("[partition alignment]\n");
    // A table response that is flat must behave as a clean impulse: energy ends up in one spike,
    // proving head and tail partitions join at the correct delay.
    WaveTable flat;
    flat.numFrames = 1;
    flat.samples.assign (kFrameSize, 0.0f);
    flat.samples[0] = 1.0f;
    flat.spectrum.assign (kNumHarmonics * 2, 0.0f);
    for (int h = 1; h < kNumHarmonics; ++h) flat.spectrum[(size_t) h * 2] = 1.0f / (float) h;

    FilterEngine e; e.prepare (48000.0, 1);
    e.setParameters (&flat, 0, 20000.0f, 0, PhaseMode::linear);
    juce::AudioBuffer<float> buf (1, 16384);
    buf.clear(); buf.setSample (0, 0, 1.0f);
    e.process (buf);
    int peakAt = 0; float peak = 0;
    for (int i = 0; i < 16384; ++i) if (std::abs (buf.getSample (0, i)) > peak) { peak = std::abs (buf.getSample (0, i)); peakAt = i; }
    printf ("  linear impulse peak %.3f at %d (expected %d)\n", peak, peakAt, e.latencySamples (PhaseMode::linear));
    CHECK (peakAt == e.latencySamples (PhaseMode::linear), "peak position");

    // Compare partitioned output with a brute-force convolution of a morphless frame.
    WavesieveProcessor p;
    juce::Random rng (3);
    FilterEngine e2; e2.prepare (48000.0, 1);
    const auto init = p.getCurrentTable();
    e2.setParameters (init.get(), 0.4f, 900.0f, 0, PhaseMode::minimum);
    juce::AudioBuffer<float> imp (1, 8192); imp.clear(); imp.setSample (0, 0, 1.0f);
    e2.process (imp);
    FilterEngine e3; e3.prepare (48000.0, 1);
    e3.setParameters (init.get(), 0.4f, 900.0f, 0, PhaseMode::minimum);
    juce::AudioBuffer<float> noise (1, 8192), result (1, 8192);
    for (int i = 0; i < 8192; ++i) noise.setSample (0, i, rng.nextFloat() * 2 - 1);
    result.makeCopyOf (noise);
    // Irregular host blocks must not change the answer.
    int done = 0, sizes[] = { 1, 7, 64, 256, 311, 1024 }, s = 0;
    while (done < 8192)
    {
        const int n = juce::jmin (sizes[s++ % 6], 8192 - done);
        juce::AudioBuffer<float> part (result.getArrayOfWritePointers(), 1, done, n);
        e3.process (part);
        done += n;
    }
    double maxErr = 0;
    for (int n = 0; n < 8192; ++n)
    {
        double sum = 0;
        for (int k = 0; k <= n && k < e2.kernelLength(); ++k) sum += imp.getSample (0, k) * noise.getSample (0, n - k);
        maxErr = juce::jmax (maxErr, std::abs (sum - result.getSample (0, n)));
    }
    printf ("  partitioned vs brute force: max error %.2e\n", maxErr);
    CHECK (maxErr < 1.0e-3, "convolution mismatch %g", maxErr);
}

static void testShaper()
{
    printf ("[shaper]\n");
    WavesieveProcessor p;
    setP (p, p.apvts, "mode", 1);
    setP (p, p.apvts, "dc", 0.0f);
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer m;
    double phase = 0;
    double dryRms = 0, wetRms = 0;
    for (int b = 0; b < 60; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float v = 0.8f * (float) std::sin (phase); phase += 0.05;
            buf.setSample (0, i, v); buf.setSample (1, i, v);
            dryRms += v * v;
        }
        p.processBlock (buf, m);
        wetRms += rms (buf.getReadPointer (0), 512) * rms (buf.getReadPointer (0), 512) * 512;
    }
    CHECK (wetRms > 0.0, "shaper silent");
    printf ("  latency %d, wet/dry rms %.3f\n", p.getLatencySamples(), std::sqrt (wetRms / dryRms));

    // Overflow modes are bounded and distinct at high drive.
    for (int o : { 0, 1, 2 })
    {
        setP (p, p.apvts, "overflow", (float) o);
        setP (p, p.apvts, "drive", 36.0f);
        double peak = 0;
        for (int b = 0; b < 20; ++b)
        {
            for (int i = 0; i < 512; ++i) { const float v = (float) std::sin (phase); phase += 0.05; buf.setSample (0, i, v); buf.setSample (1, i, v); }
            p.processBlock (buf, m);
            peak = juce::jmax (peak, (double) buf.getMagnitude (0, 512));
        }
        printf ("  overflow %d peak %.3f\n", o, peak);
        CHECK (peak < 2.0, "overflow %d peak %f", o, peak);
    }
}

static void testStress()
{
    printf ("[stress: random automation, block sizes, tables]\n");
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> uni (0.0f, 1.0f);
    for (double fs : { 44100.0, 96000.0, 192000.0 })
    {
        WavesieveProcessor p;
        p.prepareToPlay (fs, 1024);
        juce::AudioBuffer<float> buf (2, 4096);
        juce::MidiBuffer m;
        double peak = 0;
        const char* ids[] = { "mode", "frame", "cutoff", "resonance", "phaseMode", "drive", "bias", "phase", "smooth", "overflow", "dc", "mix", "output" };
        const auto start = juce::Time::getMillisecondCounterHiRes();
        int processed = 0;
        for (int b = 0; b < 800; ++b)
        {
            if (b % 7 == 0)
                for (auto* id : ids)
                    if (uni (rng) < 0.3f)
                        p.apvts.getParameter (id)->setValueNotifyingHost (uni (rng));
            const int n = 1 + (int) (uni (rng) * 2047.0f);
            juce::AudioBuffer<float> part (buf.getArrayOfWritePointers(), 2, 0, n);
            for (int i = 0; i < n; ++i) { const float v = uni (rng) * 2 - 1; part.setSample (0, i, v); part.setSample (1, i, -v); }
            if (b == 400) { part.setSample (0, 0, std::numeric_limits<float>::quiet_NaN()); part.setSample (1, 1, std::numeric_limits<float>::infinity()); }
            p.processBlock (part, m);
            processed += n;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float v = part.getSample (c, i);
                    if (! std::isfinite (v)) { CHECK (false, "non-finite at fs=%g block=%d", fs, b); b = 100000; break; }
                    peak = juce::jmax (peak, (double) std::abs (v));
                }
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        printf ("  fs=%6.0f peak=%.2f, %.1f%% of realtime under constant automation\n", fs, peak, 100.0 * (ms / 1000.0) / (processed / fs));
    }
}

static void testNaNRecovery()
{
    printf ("[NaN recovery]\n");
    for (int mode : { 0, 1 })
    {
        double results[2];
        for (int poison : { 0, 1 })
        {
            WavesieveProcessor p;
            setP (p, p.apvts, "mode", (float) mode);
            p.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer m;
            buf.clear();
            if (poison) buf.setSample (0, 5, std::numeric_limits<float>::quiet_NaN());
            p.processBlock (buf, m);
            double last = -1, phase = 0;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < 512; ++i) { const float v = 0.5f * (float) std::sin (phase); phase += 0.1; buf.setSample (0, i, v); buf.setSample (1, i, v); }
                p.processBlock (buf, m);
                last = rms (buf.getReadPointer (0), 512);
            }
            results[poison] = last;
        }
        printf ("  mode %d rms clean %.4f, after NaN %.4f\n", mode, results[0], results[1]);
        CHECK (results[0] > 1.0e-4, "mode %d silent on clean signal", mode);
        CHECK (std::isfinite (results[1]) && std::abs (results[1] - results[0]) < 0.05 * results[0], "mode %d did not recover from NaN input", mode);
    }
}

// Harmonics that follow 1/h with an optional rolloff above harmonic 24, in the layout of a real saw table.
static void writeSawTable (const juce::File& f, bool lowpass)
{
    juce::FileOutputStream o (f);
    o.write ("vawt", 4); o.writeInt (2048); o.writeShort (1); o.writeShort (0);
    for (int i = 0; i < 2048; ++i)
    {
        double v = 0;
        for (int h = 1; h < 1000; ++h)
        {
            const double roll = lowpass && h > 24 ? std::pow (24.0 / h, 2.0) : 1.0;
            v += ((h & 1) != 0 ? 1.0 : -1.0) * roll / h * std::sin (juce::MathConstants<double>::twoPi * h * i / 2048.0);
        }
        o.writeFloat ((float) v);
    }
    o.flush();
}

static void testSawReference()
{
    printf ("[saw is a flat filter; lowpass table filters at the cutoff]\n");
    const auto f = juce::File::createTempFile ("saw.wt"); f.deleteFile();
    writeSawTable (f, false);
    for (int mode : { 0, 1, 2 })
    {
        WavesieveProcessor p; p.loadTableNow (f);
        setP (p, p.apvts, "phaseMode", (float) mode);
        const double a = gainAt (p, 150.0, 48000.0), b = gainAt (p, 1000.0, 48000.0), c = gainAt (p, 6000.0, 48000.0);
        setP (p, p.apvts, "cutoff", 100.0f);
        const double d = gainAt (p, 12000.0, 48000.0);
        printf ("  saw, phase mode %d: 150 Hz %.3f, 1 kHz %.3f, 6 kHz %.3f, 12 kHz at 100 Hz cutoff %.3f\n", mode, a, b, c, d);
        CHECK (a > 0.8 && a < 1.2 && b > 0.8 && b < 1.2 && c > 0.8 && c < 1.2 && d > 0.8 && d < 1.2, "saw not flat in mode %d", mode);
    }
    // In Original mode a saw must also keep the signal's shape: an impulse stays one clean impulse.
    {
        FilterEngine e; e.prepare (48000.0, 1);
        WavesieveProcessor p; p.loadTableNow (f);
        const auto t = p.getCurrentTable();
        e.setParameters (t.get(), 0, 1000.0f, 0, PhaseMode::original);
        juce::AudioBuffer<float> buf (1, 8192); buf.clear(); buf.setSample (0, 0, 1.0f);
        e.process (buf);
        int at = 0; float peak = 0;
        for (int i = 0; i < 8192; ++i) if (std::abs (buf.getSample (0, i)) > peak) { peak = std::abs (buf.getSample (0, i)); at = i; }
        printf ("  saw, original phase: impulse peak %.3f at %d\n", buf.getSample (0, at), at);
        CHECK (at == e.latencySamples (PhaseMode::original) && buf.getSample (0, at) > 0.5f, "original phase not neutral");
    }
    f.deleteFile();
    writeSawTable (f, true);
    {
        WavesieveProcessor p; p.loadTableNow (f);
        const double low = gainAt (p, 200.0, 48000.0), high = gainAt (p, 8000.0, 48000.0);
        printf ("  lowpass table, cutoff 1 kHz: 200 Hz %.3f, 8 kHz %.4f\n", low, high);
        CHECK (low > 0.8 && high < 0.05, "lowpass did not filter");
        // Moving the cutoff on a running instance must move the corner.
        setP (p, p.apvts, "cutoff", 16000.0f);
        CHECK (gainAt (p, 8000.0, 48000.0) > 0.8, "raising the cutoff did not open the filter");
    }
    f.deleteFile();
}

static void testResonance()
{
    printf ("[resonance]\n");
    // Strong peak at harmonic 24 over a quieter bed of harmonics.
    const auto f = juce::File::createTempFile ("res.wt"); f.deleteFile();
    {
        juce::FileOutputStream o (f);
        o.write ("vawt", 4); o.writeInt (2048); o.writeShort (1); o.writeShort (0);
        for (int i = 0; i < 2048; ++i)
        {
            double v = (1.0 / 24.0) * std::sin (juce::MathConstants<double>::twoPi * 24 * i / 2048.0);
            for (int h = 1; h < 200; ++h)
                if (h != 24) v += (0.15 / h) * std::sin (juce::MathConstants<double>::twoPi * h * i / 2048.0 + h * 1.7);
            o.writeFloat ((float) v);
        }
        o.flush();
    }
    double last = 0;
    for (float r : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        WavesieveProcessor p; p.loadTableNow (f);
        setP (p, p.apvts, "resonance", r);
        setP (p, p.apvts, "phaseMode", 1.0f);
        const double peak = gainAt (p, 1000.0, 48000.0), bed = gainAt (p, 1041.7 * 3.2, 48000.0);
        printf ("  resonance %+.1f: peak %.3f, between peaks %.4f\n", r, peak, bed);
        CHECK (peak > last, "peak must rise with resonance: %f after %f", peak, last);
        last = peak;
    }
    f.deleteFile();
}

static void testState (const juce::File& samples)
{
    printf ("[state]\n");
    juce::File table;
    if (samples.isDirectory())
        for (const auto& it : juce::RangedDirectoryIterator (samples, true, "*.vitaltable")) { table = it.getFile(); break; }
    if (table == juce::File()) { table = juce::File::createTempFile ("s.wt"); table.deleteFile(); writeSinglePeakTable (table); }

    WavesieveProcessor a;
    CHECK (a.loadTableNow (table), "load");
    setP (a, a.apvts, "cutoff", 2345.0f);
    setP (a, a.apvts, "mode", 1);
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    WavesieveProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (b.getCurrentTable()->file == table, "table not restored");
    CHECK (std::abs (b.apvts.getRawParameterValue ("cutoff")->load() - 2345.0f) < 1.0f, "cutoff not restored");
    CHECK (juce::roundToInt (b.apvts.getRawParameterValue ("mode")->load()) == 1, "mode not restored");
}

static void testVst3 (const juce::String& path)
{
    printf ("[hosting built VST3] %s\n", path.toRawUTF8());
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, path);
    CHECK (found.size() == 1, "found %d plugins", found.size());
    if (found.isEmpty()) return;
    juce::String error;
    auto inst = format.createInstanceFromDescription (*found[0], 48000.0, 512, error);
    CHECK (inst != nullptr, "instantiate: %s", error.toRawUTF8());
    if (inst == nullptr) return;
    inst->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer m;
    double out = 0;
    for (int b = 0; b < 100; ++b)
    {
        for (int i = 0; i < 512; ++i) { const float v = (float) std::sin (0.05 * (b * 512 + i)); buf.setSample (0, i, v); buf.setSample (1, i, v); }
        inst->processBlock (buf, m);
        out += rms (buf.getReadPointer (0), 512);
    }
    printf ("  params=%d, avg rms %.4f\n", (int) inst->getParameters().size(), out / 100);
    CHECK (out > 0.0, "hosted plugin silent");
    std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
    CHECK (ed != nullptr, "no editor");
    ed.reset();
    inst->releaseResources();
}

static void snapshot (const juce::String& prefix, const juce::File& samples)
{
    printf ("[snapshot]\n");
    WavesieveProcessor p;
    if (samples.isDirectory())
        for (const auto& it : juce::RangedDirectoryIterator (samples, true, "Monster 4.vitaltable")) { p.loadTableNow (it.getFile()); break; }
    setP (p, p.apvts, "frame", 0.45f);
    auto shoot = [&] (const juce::String& name)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (900);
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        const juce::File out (prefix + name + ".png");
        out.deleteFile();
        juce::FileOutputStream o (out);
        juce::PNGImageFormat().writeImageToStream (img, o);
        printf ("  wrote %s\n", out.getFullPathName().toRawUTF8());
    };
    shoot ("filter");
    setP (p, p.apvts, "mode", 1);
    setP (p, p.apvts, "drive", 6.0f);
    shoot ("shaper");
}

// Plays noise through a running instance and prints octave-band gains while settings change live.
static void audit (const juce::File& dir)
{
    juce::Array<juce::File> files;
    for (const auto& it : juce::RangedDirectoryIterator (dir, true, "*.wav;*.flac;*.vitaltable"))
        files.add (it.getFile());
    files.sort();
    WavesieveProcessor p;
    const double fs = 48000.0;
    p.prepareToPlay (fs, 512);
    juce::Random rng (11);
    juce::dsp::FFT fft (14);
    const int N = 16384;
    juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann);
    juce::MidiBuffer m;

    auto measure = [&] (const juce::String& label)
    {
        juce::AudioBuffer<float> buf (2, 512);
        std::vector<float> in, out;
        for (int b = 0; b < 60; ++b)
        {
            std::vector<float> dry (512);
            for (int i = 0; i < 512; ++i) { dry[(size_t) i] = 0.25f * (rng.nextFloat() * 2 - 1); buf.setSample (0, i, dry[(size_t) i]); buf.setSample (1, i, dry[(size_t) i]); }
            p.processBlock (buf, m);
            if (b >= 20) { in.insert (in.end(), dry.begin(), dry.end()); out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512); }
        }
        const double edges[] = { 40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 20480 };
        double bandIn[9] = {}, bandOut[9] = {};
        for (int seg = 0; seg + N <= (int) in.size(); seg += N / 2)
        {
            std::vector<float> a ((size_t) N * 2, 0.0f), c ((size_t) N * 2, 0.0f);
            std::copy_n (in.begin() + seg, N, a.begin()); std::copy_n (out.begin() + seg, N, c.begin());
            win.multiplyWithWindowingTable (a.data(), (size_t) N); win.multiplyWithWindowingTable (c.data(), (size_t) N);
            fft.performFrequencyOnlyForwardTransform (a.data(), true); fft.performFrequencyOnlyForwardTransform (c.data(), true);
            for (int k = 1; k < N / 2; ++k)
            {
                const double hz = k * fs / N;
                for (int e = 0; e < 9; ++e)
                    if (hz >= edges[e] && hz < edges[e + 1]) { bandIn[e] += a[(size_t) k] * a[(size_t) k]; bandOut[e] += c[(size_t) k] * c[(size_t) k]; }
            }
        }
        juce::String row;
        for (int e = 0; e < 9; ++e)
            row << juce::String (juce::roundToInt (10.0 * std::log10 ((bandOut[e] + 1.0e-12) / (bandIn[e] + 1.0e-12)))).paddedLeft (' ', 5);
        printf ("  %-30s%s\n", label.toRawUTF8(), row.toRawUTF8());
    };

    printf ("band gain dB by octave:          60  120  240  480   1k   2k   4k   8k  15k\n");
    const int count = files.size();
    for (int i = 0; i < 8 && count > 0; ++i)
    {
        const auto f = files[i * count / 8];
        p.loadTableAsync (f);
        for (int w = 0; w < 200 && p.getCurrentTable()->file != f; ++w) juce::Thread::sleep (10);
        CHECK (p.getCurrentTable()->file == f, "async load never arrived: %s %s", f.getFileName().toRawUTF8(), p.getTableError().toRawUTF8());
        printf ("%s (%d frames)\n", f.getFileName().toRawUTF8(), p.getCurrentTable()->numFrames);
        for (float cut : { 200.0f, 1000.0f })
            for (float fr : { 0.0f, 0.5f, 1.0f })
            {
                setP (p, p.apvts, "cutoff", cut); setP (p, p.apvts, "frame", fr);
                measure ("cut " + juce::String ((int) cut) + " frame " + juce::String (fr, 1));
            }
    }
}

class TestApp : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "WavesieveTest"; }
    const juce::String getApplicationVersion() override { return "1"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}
    void shutdown() override {}
    void initialise (const juce::String&) override
    {
        const auto args = getCommandLineParameterArray();
        juce::File samples, auditDir; juce::String vst3, snap;
        for (int i = 0; i + 1 < args.size(); i += 2)
        {
            if (args[i] == "--samples") samples = juce::File (args[i + 1]);
            if (args[i] == "--vst3") vst3 = args[i + 1];
            if (args[i] == "--snapshot") snap = args[i + 1];
            if (args[i] == "--audit") auditDir = juce::File (args[i + 1]);
        }
        if (auditDir.isDirectory()) audit (auditDir);
        else if (snap.isNotEmpty()) snapshot (snap, samples);
        else
        {
            testFormats();
            testFilterResponse();
            testLatency();
            testPartitionAlignment();
            testShaper();
            testNaNRecovery();
            testSawReference();
            testResonance();
            testStress();
            testState (samples);
            if (samples.isDirectory()) testLoading (samples);
            if (vst3.isNotEmpty()) testVst3 (vst3);
        }
        printf ("%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
        fflush (stdout);
        setApplicationReturnValue (failures == 0 ? 0 : 1);
        quit();
    }
};
START_JUCE_APPLICATION (TestApp)
