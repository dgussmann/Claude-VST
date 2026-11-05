#pragma once
#include "WaveTable.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace wavesieve
{
enum class PhaseMode { minimum = 0, linear, original, raw };

/** Harmonic response of a frame position: |H| for harmonics 0..1024, relative to a saw (a saw is flat). Peak is 1 at 0% resonance; positive resonance can exceed it. */
void computeHarmonicResponse (const WaveTable&, float framePos, float resonance, float* mag, float* re, float* im);

/** The last harmonic with real content. */
int findTopHarmonic (const float* mag) noexcept;

/** Reads the curve at a fractional harmonic. Below the fundamental it holds the first value. Above the
    last harmonic, a table with full-range content holds its last value, so it doesn't become a brick wall
    at low cutoffs; a sparse table (a sine, say) has nothing up there and stays closed. */
inline float readResponse (const float* mag, int top, double h) noexcept
{
    constexpr int fullRange = 512;
    if (h > (double) top + 1.0 && top < fullRange) return 0.0f;
    const double c = juce::jlimit (1.0, top >= fullRange ? (double) top : (double) kNumHarmonics - 1.001, h);
    const int i = juce::jmin ((int) c, kNumHarmonics - 2);
    return mag[i] + (float) (c - i) * (mag[i + 1] - mag[i]);
}

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
