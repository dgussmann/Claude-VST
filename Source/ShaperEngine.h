#pragma once
#include "WaveTable.h"
#include <juce_dsp/juce_dsp.h>

namespace wavesieve
{
enum class Overflow { repeat = 0, hold, mirror };

/** Builds the transfer curve for a frame position: rotated by phase, smoothed, kFrameSize points. */
void computeShaperCurve (const WaveTable&, float framePos, float phase01, float smooth01, float* out, float* scratch);

/** Waveshaper whose curve is a table frame. Runs at 4x oversampling to keep aliasing down. */
class ShaperEngine
{
public:
    void prepare (double sampleRate, int numChannels, int maxBlock);
    void reset();
    void setParameters (const WaveTable*, float framePos, float phase01, float smooth01,
                        float driveGain, float bias, Overflow, bool dcFilter) noexcept;
    void process (juce::AudioBuffer<float>& buffer) noexcept;
    int latencySamples() const noexcept;

private:
    void processChunk (juce::dsp::AudioBlock<float> block) noexcept;

    static constexpr int kChunk = 512;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::vector<float> curveA, curveB, scratch;
    float* current = nullptr;
    float* target  = nullptr;
    std::vector<float> dcX, dcY;
    float dcCoeff = 0.999f;
    bool hasCurve = false;

    const WaveTable* table = nullptr;
    float frame = 0, phase = 0, smooth = 0, drive = 1, bias = 0;
    float prevDrive = 1, prevBias = 0;
    Overflow overflow = Overflow::repeat;
    bool dc = true;
    const WaveTable* lTable = nullptr;
    float lFrame = -1, lPhase = -1, lSmooth = -1;
};
} // namespace wavesieve
