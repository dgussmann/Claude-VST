#include "WaveTable.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace wavesieve
{
namespace
{
std::shared_ptr<WaveTable> finalise (std::vector<std::vector<float>> frames, const juce::String& name, const juce::File& file, juce::String& error)
{
    if (frames.empty()) { error = "no frames found"; return nullptr; }

    if ((int) frames.size() > kMaxFrames)
    {
        std::vector<std::vector<float>> picked;
        picked.reserve (kMaxFrames);
        for (int i = 0; i < kMaxFrames; ++i)
            picked.push_back (std::move (frames[(size_t) std::llround ((double) i * (double) (frames.size() - 1) / (kMaxFrames - 1))]));
        frames = std::move (picked);
    }

    auto t = std::make_shared<WaveTable>();
    t->name = name;
    t->file = file;
    t->numFrames = (int) frames.size();
    t->samples.resize ((size_t) t->numFrames * kFrameSize);
    t->spectrum.resize ((size_t) t->numFrames * kNumHarmonics * 2);

    float peak = 0.0f;
    for (int f = 0; f < t->numFrames; ++f)
    {
        const auto& src = frames[(size_t) f];
        const int n = (int) src.size();
        if (n < 2) { error = "frame too short"; return nullptr; }
        auto* dst = t->samples.data() + (size_t) f * kFrameSize;
        for (int i = 0; i < kFrameSize; ++i)
        {
            const double p = (double) i * n / kFrameSize;
            const int a = (int) p % n;
            const float x = (float) (p - std::floor (p));
            float v = src[(size_t) a] + x * (src[(size_t) ((a + 1) % n)] - src[(size_t) a]);
            if (! std::isfinite (v)) v = 0.0f;
            dst[i] = v;
            peak = std::max (peak, std::abs (v));
        }
    }
    if (peak < 1.0e-6f) { error = "table is silent"; return nullptr; }

    for (auto& s : t->samples) s /= peak;

    juce::dsp::FFT fft (11);
    std::vector<float> work (kFrameSize * 2);
    for (int f = 0; f < t->numFrames; ++f)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        std::copy_n (t->frame (f), kFrameSize, work.begin());
        fft.performRealOnlyForwardTransform (work.data(), true);
        auto* out = t->spectrum.data() + (size_t) f * kNumHarmonics * 2;
        const float scale = 2.0f / kFrameSize;
        for (int i = 0; i < kNumHarmonics * 2; ++i)
            out[i] = work[(size_t) i] * scale;
    }
    return t;
}

struct WavChunks { int cycle = 0; };

WavChunks scanWavChunks (const juce::File& file)
{
    WavChunks r;
    juce::FileInputStream in (file);
    if (! in.openedOk()) return r;
    char id[4];
    if (in.read (id, 4) != 4 || memcmp (id, "RIFF", 4) != 0) return r;
    in.readInt();
    if (in.read (id, 4) != 4 || memcmp (id, "WAVE", 4) != 0) return r;

    while (! in.isExhausted())
    {
        if (in.read (id, 4) != 4) break;
        const auto size = (juce::int64) (juce::uint32) in.readInt();
        const juce::int64 next = in.getPosition() + size + size % 2;

        if (memcmp (id, "clm ", 4) == 0)
        {
            juce::MemoryBlock mb;
            in.readIntoMemoryBlock (mb, std::min<juce::int64> (size, 64));
            const auto text = mb.toString();
            if (text.startsWith ("<!>"))
                r.cycle = text.substring (3).getIntValue();
        }
        else if (memcmp (id, "srge", 4) == 0 || memcmp (id, "srgo", 4) == 0)
        {
            in.readInt();                      // version
            r.cycle = in.readInt();
        }
        else if (memcmp (id, "uhWT", 4) == 0)
        {
            if (r.cycle == 0) r.cycle = 2048;
        }
        if (! in.setPosition (next)) break;
    }
    return r;
}

WaveTablePtr loadAudio (const juce::File& file, juce::String& error)
{
    juce::WavAudioFormat wav;
    juce::FlacAudioFormat flac;
    juce::AudioFormat& format = file.hasFileExtension (".flac") ? static_cast<juce::AudioFormat&> (flac) : wav;
    std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr) { error = "not a readable audio file"; return nullptr; }
    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, 16 * 1024 * 1024);
    if (len < 16) { error = "audio file too short"; return nullptr; }

    juce::AudioBuffer<float> buf (1, len);
    reader->read (&buf, 0, len, 0, true, false);

    int cycle = scanWavChunks (file).cycle;
    if (cycle <= 1 || cycle > len)
    {
        if (len % kFrameSize == 0)  cycle = kFrameSize;
        else if (len <= 8192)       cycle = len;
        else                        cycle = kFrameSize;
    }

    std::vector<std::vector<float>> frames;
    const int count = len / cycle;
    for (int f = 0; f < count; ++f)
    {
        // Only keep what finalise() will use, so very long files stay light.
        if (count > kMaxFrames * 8 && f % (count / (kMaxFrames * 4)) != 0) continue;
        const auto* p = buf.getReadPointer (0) + f * cycle;
        frames.emplace_back (p, p + cycle);
    }
    return finalise (std::move (frames), file.getFileNameWithoutExtension(), file, error);
}

WaveTablePtr loadWt (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 12) { error = "unreadable .wt"; return nullptr; }
    const auto* d = static_cast<const char*> (mb.getData());
    if (memcmp (d, "vawt", 4) != 0) { error = "not a vawt .wt file"; return nullptr; }
    const int size   = (int) juce::ByteOrder::littleEndianInt (d + 4);
    const int count  = (int) juce::ByteOrder::littleEndianShort (d + 8);
    const int flags  = (int) juce::ByteOrder::littleEndianShort (d + 10);
    const bool i16   = (flags & 4) != 0;
    const size_t need = 12 + (size_t) size * (size_t) count * (i16 ? 2u : 4u);
    if (size < 2 || size > 65536 || count < 1 || mb.getSize() < need) { error = "truncated .wt"; return nullptr; }

    std::vector<std::vector<float>> frames ((size_t) count, std::vector<float> ((size_t) size));
    const char* p = d + 12;
    for (int f = 0; f < count; ++f)
        for (int i = 0; i < size; ++i)
        {
            if (i16)
            {
                frames[(size_t) f][(size_t) i] = (float) (juce::int16) juce::ByteOrder::littleEndianShort (p) / 32768.0f;
                p += 2;
            }
            else
            {
                float v; memcpy (&v, p, 4);
                frames[(size_t) f][(size_t) i] = v;
                p += 4;
            }
        }
    return finalise (std::move (frames), file.getFileNameWithoutExtension(), file, error);
}

// ---- Vital ---------------------------------------------------------------------------------
int vitalTypeOf (const juce::var& c)
{
    const auto& t = c["type"];
    if (t.isString())
    {
        const auto s = t.toString();
        if (s == "Wave Source")       return 0;
        if (s == "Line Source")       return 1;
        if (s == "Audio File Source") return 2;
        if (s.containsOnly ("0123456789")) return s.getIntValue();
        return -1;
    }
    return (int) t;
}

std::vector<float> renderLine (const juce::var& kf)
{
    std::vector<float> out ((size_t) kFrameSize, 0.0f);
    auto* pts = kf["points"].getArray();
    if (pts == nullptr || pts->size() < 4) return out;
    const int n = pts->size() / 2;
    int seg = 0;
    for (int i = 0; i < kFrameSize; ++i)
    {
        const float x = (float) i / kFrameSize;
        while (seg < n - 2 && (float) (double) (*pts)[(seg + 1) * 2] <= x) ++seg;
        const float x0 = (float) (double) (*pts)[seg * 2],     y0 = (float) (double) (*pts)[seg * 2 + 1];
        const float x1 = (float) (double) (*pts)[seg * 2 + 2], y1 = (float) (double) (*pts)[seg * 2 + 3];
        const float t = x1 > x0 ? juce::jlimit (0.0f, 1.0f, (x - x0) / (x1 - x0)) : 1.0f;
        out[(size_t) i] = y0 + t * (y1 - y0);
    }
    return out;
}

std::vector<float> decodeWave (const juce::var& kf)
{
    std::vector<float> out;
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, kf["wave_data"].toString())) return out;
    const size_t n = mo.getDataSize() / sizeof (float);
    out.resize (n);
    if (n > 0) memcpy (out.data(), mo.getData(), n * sizeof (float));
    return out;
}

std::vector<std::vector<float>> renderVitalGroup (const juce::var& group, juce::String& error)
{
    std::vector<std::vector<float>> frames;
    auto* comps = group["components"].getArray();
    if (comps == nullptr || comps->isEmpty()) return frames;

    // Component 0 is the source; later ones are Vital modifiers, which are not re-created here.
    const auto& src = comps->getReference (0);
    const int type = vitalTypeOf (src);
    auto* kfs = src["keyframes"].getArray();
    if (kfs == nullptr || kfs->isEmpty() || type < 0 || type > 2) { error = "unsupported Vital source"; return frames; }

    const int nk = kfs->size();
    const int interpolation = (int) src.getProperty ("interpolation_style", 1);
    std::vector<double> pos ((size_t) nk);
    for (int i = 0; i < nk; ++i) pos[(size_t) i] = (double) (*kfs)[i]["position"];

    std::vector<std::vector<float>> shapes;
    std::vector<float> audio;
    if (type == 2)
    {
        juce::MemoryOutputStream mo;
        juce::Base64::convertFromBase64 (mo, src["audio_file"].toString());
        const size_t n = mo.getDataSize() / sizeof (juce::int16);
        audio.resize (n);
        auto* d = static_cast<const juce::int16*> (mo.getData());
        for (size_t i = 0; i < n; ++i) audio[i] = (float) d[i] / 32768.0f;
        if (audio.size() < 2) { error = "empty Vital audio source"; return frames; }
    }
    else
    {
        for (int i = 0; i < nk; ++i)
            shapes.push_back (type == 0 ? decodeWave ((*kfs)[i]) : renderLine ((*kfs)[i]));
        for (auto& s : shapes) if (s.size() < 2) { error = "bad Vital keyframe"; return frames; }
    }

    const int numOut = nk > 1 ? kMaxFrames : 1;
    const double firstPos = pos.front(), lastPos = pos.back();
    for (int f = 0; f < numOut; ++f)
    {
        const double p = numOut > 1 ? juce::jmap ((double) f, 0.0, (double) (numOut - 1), firstPos, lastPos) : firstPos;
        int a = 0;
        while (a < nk - 2 && pos[(size_t) a + 1] <= p) ++a;
        const int b = juce::jmin (a + 1, nk - 1);
        double t = (b > a && pos[(size_t) b] > pos[(size_t) a]) ? juce::jlimit (0.0, 1.0, (p - pos[(size_t) a]) / (pos[(size_t) b] - pos[(size_t) a])) : 0.0;
        if (interpolation == 0) t = 0.0;

        std::vector<float> frame ((size_t) kFrameSize);
        if (type == 2)
        {
            const double defWin = (double) src.getProperty ("window_size", 2048.0);
            const auto& ka = (*kfs)[a];
            const auto& kb = (*kfs)[b];
            const double sa = (double) ka["start_position"], sb = (double) kb["start_position"];
            const double wa = (double) ka.getProperty ("window_size", defWin), wb = (double) kb.getProperty ("window_size", defWin);
            const double start = sa + t * (sb - sa);
            const double win = juce::jmax (2.0, wa + t * (wb - wa));
            const auto last = (double) audio.size() - 1.0;
            for (int i = 0; i < kFrameSize; ++i)
            {
                const double x = juce::jlimit (0.0, last, start + i * win / kFrameSize);
                const auto i0 = (size_t) x;
                const auto i1 = juce::jmin (i0 + 1, audio.size() - 1);
                frame[(size_t) i] = audio[i0] + (float) (x - (double) i0) * (audio[i1] - audio[i0]);
            }
        }
        else
        {
            const auto& A = shapes[(size_t) a];
            const auto& B = shapes[(size_t) b];
            for (int i = 0; i < kFrameSize; ++i)
            {
                const float va = A[(size_t) (i * (int) A.size() / kFrameSize)];
                const float vb = B[(size_t) (i * (int) B.size() / kFrameSize)];
                frame[(size_t) i] = va + (float) t * (vb - va);
            }
        }
        frames.push_back (std::move (frame));
    }
    return frames;
}

WaveTablePtr loadVitalJson (const juce::File& file, juce::String& error)
{
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject()) { error = "not valid JSON"; return nullptr; }

    juce::var table = parsed;
    juce::String name = file.getFileNameWithoutExtension();
    if (parsed.hasProperty ("settings"))   // a .vital preset: pick its first non-default table
    {
        auto* list = parsed["settings"]["wavetables"].getArray();
        if (list == nullptr || list->isEmpty()) { error = "preset has no wavetables"; return nullptr; }
        table = list->getReference (0);
        for (auto& t : *list)
            if (t["name"].toString() != "Init") { table = t; break; }
        name = file.getFileNameWithoutExtension() + " - " + table["name"].toString();
    }

    auto* groups = table["groups"].getArray();
    if (groups == nullptr || groups->isEmpty()) { error = "no wavetable groups"; return nullptr; }

    std::vector<std::vector<float>> sum;
    for (auto& g : *groups)
    {
        auto frames = renderVitalGroup (g, error);
        if (frames.empty()) continue;
        if (sum.empty()) { sum = std::move (frames); continue; }
        if (frames.size() > sum.size() && sum.size() == 1) sum.resize (frames.size(), sum[0]);
        for (size_t f = 0; f < sum.size(); ++f)
        {
            const auto& o = frames[juce::jmin (f, frames.size() - 1)];
            for (size_t i = 0; i < sum[f].size(); ++i) sum[f][i] += o[i];
        }
    }
    if (sum.empty()) return nullptr;
    error.clear();
    return finalise (std::move (sum), name, file, error);
}
} // namespace

bool isWaveTableExtension (const juce::File& file)
{
    return file.hasFileExtension (".wav;.flac;.wt;.vitaltable;.vital");
}

int readWavFrameSizeHint (const juce::File& file)  { return scanWavChunks (file).cycle; }

WaveTablePtr loadWaveTable (const juce::File& file, juce::String& error)
{
    error.clear();
    if (! file.existsAsFile()) { error = "file not found"; return nullptr; }
    if (file.hasFileExtension (".wav;.flac"))           return loadAudio (file, error);
    if (file.hasFileExtension (".wt"))                  return loadWt (file, error);
    if (file.hasFileExtension (".vitaltable;.vital"))   return loadVitalJson (file, error);
    error = "unsupported file type";
    return nullptr;
}

WaveTablePtr makeInitTable()
{
    // A vowel-like formant that climbs across the frames.
    const int numFrames = 64;
    std::vector<std::vector<float>> frames ((size_t) numFrames, std::vector<float> ((size_t) kFrameSize, 0.0f));
    for (int f = 0; f < numFrames; ++f)
    {
        const double morph = (double) f / (numFrames - 1);
        const double f1 = 4.0 + 28.0 * morph, f2 = 16.0 + 120.0 * morph * morph;
        for (int h = 1; h <= 512; ++h)
        {
            const double a = std::exp (-std::pow ((h - f1) / (1.5 + 0.2 * f1), 2.0))
                           + 0.6 * std::exp (-std::pow ((h - f2) / (3.0 + 0.2 * f2), 2.0))
                           + 0.02 / h;
            if (a < 1.0e-5) continue;
            for (int i = 0; i < kFrameSize; ++i)
                frames[(size_t) f][(size_t) i] += (float) (a * std::sin (juce::MathConstants<double>::twoPi * h * i / kFrameSize));
        }
        float framePeak = 1.0e-6f;
        for (auto v : frames[(size_t) f]) framePeak = std::max (framePeak, std::abs (v));
        for (auto& v : frames[(size_t) f]) v /= framePeak;
    }
    juce::String error;
    return finalise (std::move (frames), "Init Formant", {}, error);
}
} // namespace wavesieve
