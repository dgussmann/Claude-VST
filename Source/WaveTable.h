#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace wavesieve
{
constexpr int kFrameSize    = 2048;
constexpr int kNumHarmonics = kFrameSize / 2 + 1;
constexpr int kMaxFrames    = 256;

/** An immutable, fully decoded wavetable: frames in the time domain plus each frame's spectrum. */
struct WaveTable
{
    juce::String name;
    juce::File   file;
    int          numFrames = 0;
    std::vector<float> samples;   // numFrames * kFrameSize, peak-normalised
    std::vector<float> spectrum;  // numFrames * kNumHarmonics * 2 (re, im)

    const float* frame (int i) const noexcept    { return samples.data() + (size_t) i * kFrameSize; }
    const float* spectra (int i) const noexcept  { return spectrum.data() + (size_t) i * kNumHarmonics * 2; }
};

using WaveTablePtr = std::shared_ptr<const WaveTable>;

/** Loads Serum/Vital/generic .wav, Kilohearts .flac, Surge .wt, .vitaltable and .vital (first non-Init table). */
WaveTablePtr loadWaveTable (const juce::File& file, juce::String& error);

/** Built-in table used until a file is chosen: a formant sweep, so the plugin is never silent. */
WaveTablePtr makeInitTable();

/** Reads a .wav header and returns the cycle length declared in clm/srge/uhWT chunks, or 0 if none. */
int readWavFrameSizeHint (const juce::File& file);

bool isWaveTableExtension (const juce::File& file);
} // namespace wavesieve
