#include "ShaperEngine.h"
#include <cmath>

namespace wavesieve
{
void computeShaperCurve (const WaveTable& t, float framePos, float phase01, float smooth01, float* out, float* scratch)
{
    const float p = juce::jlimit (0.0f, 1.0f, framePos) * (float) (t.numFrames - 1);
    const int a = (int) p, b = juce::jmin (a + 1, t.numFrames - 1);
    const float x = p - (float) a;
    const float* fa = t.frame (a);
    const float* fb = t.frame (b);

    const float shift = (phase01 - std::floor (phase01)) * (float) kFrameSize;
    const int si = (int) shift;
    const float sf = shift - (float) si;
    for (int i = 0; i < kFrameSize; ++i)
    {
        const int i0 = (i + si) % kFrameSize, i1 = (i0 + 1) % kFrameSize;
        const float va = fa[i0] + sf * (fa[i1] - fa[i0]);
        const float vb = fb[i0] + sf * (fb[i1] - fb[i0]);
        out[i] = va + x * (vb - va);
    }

    // Smooth = two passes of a circular moving average, the width growing with the square of the knob.
    const int radius = juce::roundToInt (smooth01 * smooth01 * 256.0f);
    if (radius > 0)
    {
        const float norm = 1.0f / (float) (radius * 2 + 1);
        for (int pass = 0; pass < 2; ++pass)
        {
            std::copy (out, out + kFrameSize, scratch);
            float sum = 0.0f;
            for (int i = -radius; i <= radius; ++i)
                sum += scratch[(i + kFrameSize) % kFrameSize];
            for (int i = 0; i < kFrameSize; ++i)
            {
                out[i] = sum * norm;
                sum += scratch[(i + radius + 1) % kFrameSize] - scratch[(i - radius + kFrameSize) % kFrameSize];
            }
        }
    }
}

void ShaperEngine::prepare (double sampleRate, int numChannels, int)
{
    os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) numChannels, 2,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) kChunk);
    curveA.assign ((size_t) kFrameSize, 0.0f);
    curveB.assign ((size_t) kFrameSize, 0.0f);
    scratch.assign ((size_t) kFrameSize, 0.0f);
    current = curveA.data();
    target = curveB.data();
    dcX.assign ((size_t) numChannels, 0.0f);
    dcY.assign ((size_t) numChannels, 0.0f);
    dcCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * 8.0 / sampleRate);
    reset();
}

void ShaperEngine::reset()
{
    if (os != nullptr) os->reset();
    std::fill (dcX.begin(), dcX.end(), 0.0f);
    std::fill (dcY.begin(), dcY.end(), 0.0f);
    hasCurve = false;
    lTable = nullptr;
}

void ShaperEngine::setParameters (const WaveTable* t, float f, float ph, float s, float d, float b, Overflow o, bool dcOn) noexcept
{
    table = t; frame = f; phase = ph; smooth = s; drive = d; bias = b; overflow = o; dc = dcOn;
}

int ShaperEngine::latencySamples() const noexcept
{
    return os != nullptr ? juce::roundToInt (os->getLatencyInSamples()) : 0;
}

void ShaperEngine::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (table == nullptr || os == nullptr) return;
    juce::dsp::AudioBlock<float> whole (buffer);
    const auto total = (size_t) buffer.getNumSamples();
    for (size_t done = 0; done < total; done += kChunk)
        processChunk (whole.getSubBlock (done, juce::jmin ((size_t) kChunk, total - done)));
}

void ShaperEngine::processChunk (juce::dsp::AudioBlock<float> block) noexcept
{
    const bool changed = table != lTable || frame != lFrame || phase != lPhase || smooth != lSmooth;
    if (changed)
    {
        computeShaperCurve (*table, frame, phase, smooth, target, scratch.data());
        lTable = table; lFrame = frame; lPhase = phase; lSmooth = smooth;
        if (! hasCurve)
        {
            std::copy (target, target + kFrameSize, current);
            hasCurve = true;
        }
    }

    auto up = os->processSamplesUp (juce::dsp::AudioBlock<const float> (block));
    const int n = (int) up.getNumSamples();
    const float scale = (float) (kFrameSize - 1);
    const float inv = 1.0f / (float) n;

    for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
    {
        auto* d = up.getChannelPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float r = ((float) i + 1.0f) * inv;
            const float g = prevDrive + r * (drive - prevDrive);
            const float bv = prevBias + r * (bias - prevBias);
            float u = (d[i] * g + bv + 1.0f) * 0.5f;
            if (! (std::abs (u) < 1.0e6f)) u = 0.5f;  // guards NaN/Inf input

            switch (overflow)
            {
                case Overflow::repeat: u -= std::floor (u); break;
                case Overflow::hold:   u = juce::jlimit (0.0f, 1.0f, u); break;
                case Overflow::mirror:
                {
                    float m = u * 0.5f;
                    m -= std::floor (m);
                    u = m < 0.5f ? m * 2.0f : 2.0f - m * 2.0f;
                    break;
                }
            }
            const float q = u * scale;
            const int a = juce::jlimit (0, kFrameSize - 2, (int) q);
            const float f = q - (float) a;
            float y = current[a] + f * (current[a + 1] - current[a]);
            if (changed)
            {
                const float t = target[a] + f * (target[a + 1] - target[a]);
                y += r * (t - y);
            }
            d[i] = y;
        }
    }
    prevDrive = drive;
    prevBias = bias;
    if (changed) std::swap (current, target);

    os->processSamplesDown (block);

    if (dc)
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* d = block.getChannelPointer (ch);
            float xp = dcX[ch], yp = dcY[ch];
            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                const float x = d[i];
                yp = x - xp + dcCoeff * yp;
                xp = x;
                d[i] = yp;
            }
            dcX[ch] = xp; dcY[ch] = yp;
        }
}
} // namespace wavesieve
