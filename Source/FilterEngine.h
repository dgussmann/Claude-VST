#pragma once
#include "WaveTable.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace wavesieve
{
enum class PhaseMode { minimum = 0, linear, original, raw };

/** Harmonic response of a frame position: |H| for harmonics 0..1024, peak 1, with resonance applied. */
void computeHarmonicResponse (const WaveTable&, float framePos, float resonance, float* mag, float* re, float* im);

/** Maps an audio frequency onto the table. Cutoff is the frequency of harmonic 24. */
inline double harmonicForFrequency (double hz, double cutoff) noexcept  { return 24.0 * hz / cutoff; }

/** Turns a table frame into a filter: a zero-latency partitioned convolver whose kernel morphs with
    the frame and cutoff. Kernels crossfade block by block, so sweeps are free of clicks. */
class FilterEngine
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    void setParameters (const WaveTable* table, float framePos, float cutoffHz, float resonance, PhaseMode mode) noexcept;
    void process (juce::AudioBuffer<float>& buffer) noexcept;
    int latencySamples (PhaseMode) const noexcept;
    int kernelLength() const noexcept { return kernelLen; }

private:
    static constexpr int B = 256;  // partition length
    static constexpr int kOrderB = 9;

    struct Kernel { std::vector<float> headReversed, partitions; };
    struct Channel
    {
        std::vector<float> history, fdl, tail;
        int writePos = 0, fdlPos = 0;
    };

    void design (Kernel&) noexcept;
    void blockBoundary (int numChannels) noexcept;
    void convolve (const Kernel&, Channel&, float* out) noexcept;

    double sr = 44100.0;
    int kernelLen = 4096, designOrder = 14, designSize = 16384, numPartitions = 15;
    std::unique_ptr<juce::dsp::FFT> blockFFT, designFFT;
    Kernel kernels[2];
    int active = 0;
    bool crossfading = false, designed = false;
    std::vector<Channel> channels;
    std::vector<float> spec, spec2, mag, re, im, kernelTime, ifftA, ifftB;
    int pos = 0;

    const WaveTable* table = nullptr;
    float framePos = 0, cutoff = 1000, reso = 0;
    PhaseMode mode = PhaseMode::minimum;
    const WaveTable* dTable = nullptr;
    float dFrame = -1, dCutoff = -1, dReso = -2;
    PhaseMode dMode = PhaseMode::minimum;
};
} // namespace wavesieve
