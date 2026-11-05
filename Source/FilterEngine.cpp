#include "FilterEngine.h"
#include <cmath>

namespace wavesieve
{
void computeHarmonicResponse (const WaveTable& t, float framePos, float resonance, float* mag, float* re, float* im)
{
    const float p = juce::jlimit (0.0f, 1.0f, framePos) * (float) (t.numFrames - 1);
    const int a = (int) p, b = juce::jmin (a + 1, t.numFrames - 1);
    const float x = p - (float) a;
    const float* sa = t.spectra (a);
    const float* sb = t.spectra (b);

    float peak = 1.0e-9f;
    for (int h = 0; h < kNumHarmonics; ++h)
    {
        const float ar = sa[h * 2], ai = sa[h * 2 + 1], br = sb[h * 2], bi = sb[h * 2 + 1];
        const float ma = std::sqrt (ar * ar + ai * ai), mb = std::sqrt (br * br + bi * bi);
        mag[h] = ma + x * (mb - ma);
        if (re != nullptr) { re[h] = ar + x * (br - ar); im[h] = ai + x * (bi - ai); }
        if (h > 0) peak = std::max (peak, mag[h]);
    }
    // -100%..+100% resonance raises the normalised response to a power of 1/4..4.
    const float exponent = std::pow (4.0f, juce::jlimit (-1.0f, 1.0f, resonance));
    for (int h = 0; h < kNumHarmonics; ++h)
        mag[h] = std::pow (juce::jmin (1.0f, mag[h] / peak), exponent);
}

void FilterEngine::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    int scale = 1;
    while (sr / scale > 64000.0) scale *= 2;
    kernelLen = 4096 * scale;
    designSize = kernelLen * 4;
    designOrder = juce::roundToInt (std::log2 ((double) designSize));
    numPartitions = (kernelLen - B) / B;

    blockFFT  = std::make_unique<juce::dsp::FFT> (kOrderB);
    designFFT = std::make_unique<juce::dsp::FFT> (designOrder);

    const size_t specSize = (size_t) (B + 1) * 2;
    for (auto& k : kernels)
    {
        k.headReversed.assign ((size_t) B, 0.0f);
        k.partitions.assign (specSize * (size_t) numPartitions, 0.0f);
    }
    channels.resize ((size_t) numChannels);
    for (auto& c : channels)
    {
        c.history.assign ((size_t) B * 8, 0.0f);
        c.fdl.assign (specSize * (size_t) numPartitions, 0.0f);
        c.tail.assign ((size_t) B, 0.0f);
    }
    spec.assign ((size_t) designSize * 2, 0.0f);
    spec2.assign ((size_t) designSize * 2, 0.0f);
    kernelTime.assign ((size_t) kernelLen, 0.0f);
    mag.assign ((size_t) kNumHarmonics, 0.0f);
    re.assign ((size_t) kNumHarmonics, 0.0f);
    im.assign ((size_t) kNumHarmonics, 0.0f);
    ifftA.assign ((size_t) B * 4, 0.0f);
    ifftB.assign ((size_t) B * 4, 0.0f);
    reset();
}

void FilterEngine::reset()
{
    for (auto& c : channels)
    {
        std::fill (c.history.begin(), c.history.end(), 0.0f);
        std::fill (c.fdl.begin(), c.fdl.end(), 0.0f);
        std::fill (c.tail.begin(), c.tail.end(), 0.0f);
        c.writePos = B * 2;
        c.fdlPos = 0;
    }
    pos = 0;
    crossfading = false;
    designed = false;
}

void FilterEngine::setParameters (const WaveTable* t, float f, float c, float r, PhaseMode m) noexcept
{
    table = t; framePos = f; cutoff = c; reso = r; mode = m;
}

int FilterEngine::latencySamples (PhaseMode m) const noexcept
{
    return (m == PhaseMode::linear || m == PhaseMode::original) ? kernelLen / 2 : 0;
}

void FilterEngine::design (Kernel& out) noexcept
{
    dTable = table; dFrame = framePos; dCutoff = cutoff; dReso = reso; dMode = mode;
    if (table == nullptr) return;

    const auto& t = *table;
    const int L = kernelLen, N = designSize;
    auto* X = spec.data();
    auto* K = kernelTime.data();
    const auto& fft = *designFFT;

    if (mode == PhaseMode::raw)
    {
        // The frame itself is the impulse response, stretched so that harmonic 24 lands on the cutoff.
        const double cycle = 24.0 * sr / (double) cutoff;
        const int len = juce::jlimit (16, L, (int) std::ceil (cycle));
        const float p = juce::jlimit (0.0f, 1.0f, framePos) * (float) (t.numFrames - 1);
        const int a = (int) p, b = juce::jmin (a + 1, t.numFrames - 1);
        const float x = p - (float) a;
        const float* fa = t.frame (a);
        const float* fb = t.frame (b);
        for (int n = 0; n < L; ++n) K[n] = 0.0f;
        const int fadeLen = juce::jmin (len / 8, 256) + 1;
        for (int n = 0; n < len; ++n)
        {
            const double q = (double) n * kFrameSize / cycle;
            const int i = (int) q % kFrameSize, j = (i + 1) % kFrameSize;
            const float y = (float) (q - std::floor (q));
            const float va = fa[i] + y * (fa[j] - fa[i]), vb = fb[i] + y * (fb[j] - fb[i]);
            float v = va + x * (vb - va);
            if (len - n < fadeLen) v *= (float) (len - n) / (float) fadeLen;
            K[n] = v;
        }
        std::fill (X, X + N * 2, 0.0f);
        std::copy (K, K + L, X);
        fft.performRealOnlyForwardTransform (X, true);
        float peak = 1.0e-6f;
        for (int k = 0; k <= N / 2; ++k)
            peak = std::max (peak, X[k * 2] * X[k * 2] + X[k * 2 + 1] * X[k * 2 + 1]);
        const float g = 1.0f / std::sqrt (peak);
        for (int n = 0; n < L; ++n) K[n] *= g;
    }
    else
    {
        computeHarmonicResponse (t, framePos, reso, mag.data(), re.data(), im.data());
        const bool keepPhase = mode == PhaseMode::original;
        const bool minimum = mode == PhaseMode::minimum;
        const double harmonicsPerBin = harmonicForFrequency (sr / N, cutoff);

        for (int k = 0; k <= N / 2; ++k)
        {
            double h = harmonicsPerBin * k;
            float m = 0.0f, pr = 1.0f, pi = 0.0f;
            if (h < 1024.0)
            {
                h = std::max (h, 1.0);  // below the fundamental, hold it instead of cutting the bass
                const int i = (int) h;
                const float y = (float) (h - i);
                m = mag[(size_t) i] + y * (mag[(size_t) i + 1] - mag[(size_t) i]);
                if (keepPhase)
                {
                    const float r = re[(size_t) i] + y * (re[(size_t) i + 1] - re[(size_t) i]);
                    const float q = im[(size_t) i] + y * (im[(size_t) i + 1] - im[(size_t) i]);
                    const float n = std::sqrt (r * r + q * q);
                    if (n > 1.0e-9f) { pr = r / n; pi = q / n; }
                }
            }
            if (minimum)
            {
                X[k * 2] = std::log (std::max (m, 1.0e-5f));
                X[k * 2 + 1] = 0.0f;
            }
            else
            {
                X[k * 2] = m * pr;
                X[k * 2 + 1] = (k == 0 || k == N / 2) ? 0.0f : m * pi;
            }
        }

        fft.performRealOnlyInverseTransform (X);  // X[0..N) now holds the time-domain result

        if (minimum)
        {
            // Homomorphic minimum phase: fold the real cepstrum, then exponentiate its spectrum.
            for (int n = 1; n < N / 2; ++n) X[n] *= 2.0f;
            std::fill (X + N / 2 + 1, X + N * 2, 0.0f);
            fft.performRealOnlyForwardTransform (X, true);
            for (int k = 0; k <= N / 2; ++k)
            {
                const float e = std::exp (X[k * 2]);
                const float ph = X[k * 2 + 1];
                X[k * 2] = e * std::cos (ph);
                X[k * 2 + 1] = e * std::sin (ph);
            }
            X[1] = 0.0f; X[N + 1] = 0.0f;
            fft.performRealOnlyInverseTransform (X);
            const int fadeStart = L - L / 4;
            for (int n = 0; n < L; ++n)
            {
                const float w = n < fadeStart ? 1.0f
                                              : 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (n - fadeStart) / (float) (L - fadeStart));
                K[n] = X[n] * w;
            }
        }
        else
        {
            for (int n = 0; n < L; ++n)
            {
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * ((float) n + 0.5f) / (float) L);
                K[n] = X[(n - L / 2 + N) % N] * w;
            }
        }
    }

    for (int k = 0; k < B; ++k)
        out.headReversed[(size_t) k] = K[B - 1 - k];

    auto* work = spec2.data();
    const size_t specSize = (size_t) (B + 1) * 2;
    for (int p = 0; p < numPartitions; ++p)
    {
        std::fill (work, work + B * 4, 0.0f);
        std::copy (K + B + p * B, K + B + (p + 1) * B, work);
        blockFFT->performRealOnlyForwardTransform (work, true);
        std::copy (work, work + specSize, out.partitions.data() + specSize * (size_t) p);
    }
}

void FilterEngine::convolve (const Kernel& k, Channel& c, float* out) noexcept
{
    const size_t specSize = (size_t) (B + 1) * 2;
    std::fill (out, out + B * 4, 0.0f);
    int slot = c.fdlPos;
    for (int p = 0; p < numPartitions; ++p)
    {
        const float* restrict_h = k.partitions.data() + specSize * (size_t) p;
        const float* restrict_x = c.fdl.data() + specSize * (size_t) slot;
        for (size_t i = 0; i < specSize; i += 2)
        {
            out[i]     += restrict_h[i] * restrict_x[i]     - restrict_h[i + 1] * restrict_x[i + 1];
            out[i + 1] += restrict_h[i] * restrict_x[i + 1] + restrict_h[i + 1] * restrict_x[i];
        }
        slot = slot == 0 ? numPartitions - 1 : slot - 1;
    }
    blockFFT->performRealOnlyInverseTransform (out);
}

void FilterEngine::blockBoundary (int numChannels) noexcept
{
    const bool changed = table != dTable || framePos != dFrame || cutoff != dCutoff || reso != dReso || mode != dMode;
    crossfading = false;
    if (changed)
    {
        design (kernels[1 - active]);
        active = 1 - active;
        crossfading = true;
    }

    const size_t specSize = (size_t) (B + 1) * 2;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& c = channels[(size_t) ch];
        auto* work = ifftA.data();
        std::fill (work, work + B * 4, 0.0f);
        std::copy_n (c.history.data() + c.writePos - B * 2, B * 2, work);
        blockFFT->performRealOnlyForwardTransform (work, true);
        c.fdlPos = (c.fdlPos + 1) % numPartitions;
        std::copy (work, work + specSize, c.fdl.data() + specSize * (size_t) c.fdlPos);

        convolve (kernels[active], c, ifftA.data());
        if (crossfading)
        {
            convolve (kernels[1 - active], c, ifftB.data());
            for (int i = 0; i < B; ++i)
            {
                const float r = ((float) i + 0.5f) / (float) B;
                c.tail[(size_t) i] = ifftB[(size_t) (B + i)] * (1.0f - r) + ifftA[(size_t) (B + i)] * r;
            }
        }
        else
        {
            std::copy_n (ifftA.data() + B, B, c.tail.data());
        }
    }
}

static inline float dot (const float* a, const float* b, int n) noexcept
{
    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    for (int i = 0; i + 3 < n; i += 4)
    {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    return s0 + s1 + s2 + s3;
}

void FilterEngine::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();
    if (table == nullptr || numCh == 0) return;

    if (! designed)
    {
        design (kernels[0]);
        kernels[1] = kernels[0];
        active = 0;
        designed = true;
    }

    int done = 0;
    while (done < numSamples)
    {
        const int chunk = juce::jmin (B - pos, numSamples - done);
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& c = channels[(size_t) ch];
            auto* data = buffer.getWritePointer (ch, done);
            auto* hist = c.history.data();
            int wp = c.writePos;
            const float* cur = kernels[active].headReversed.data();
            const float* old = kernels[1 - active].headReversed.data();
            for (int i = 0; i < chunk; ++i)
            {
                hist[wp++] = data[i];
                const float* window = hist + wp - B;
                float y = dot (window, cur, B);
                if (crossfading)
                {
                    const float r = ((float) (pos + i) + 0.5f) / (float) B;
                    y = y * r + dot (window, old, B) * (1.0f - r);
                }
                data[i] = y + c.tail[(size_t) (pos + i)];
            }
            c.writePos = wp;
        }
        pos += chunk;
        done += chunk;

        if (pos == B)
        {
            pos = 0;
            blockBoundary (numCh);
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& c = channels[(size_t) ch];
                if (c.writePos + B > (int) c.history.size())
                {
                    std::copy (c.history.begin() + c.writePos - B * 2, c.history.begin() + c.writePos, c.history.begin());
                    c.writePos = B * 2;
                }
            }
        }
    }
}
} // namespace wavesieve
