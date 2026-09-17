#include "audio/wav_io.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <random>

namespace fr {

std::vector<float> AudioBuffer::mono() const {
    if (channels <= 1) return samples;
    std::vector<float> m(frames());
    for (size_t i = 0; i < m.size(); ++i) {
        float s = 0.0f;
        for (int c = 0; c < channels; ++c) s += samples[i * channels + c];
        m[i] = s / float(channels);
    }
    return m;
}

namespace {
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
void wr32(std::ostream& o, uint32_t v) { uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; o.write((char*)b, 4); }
void wr16(std::ostream& o, uint16_t v) { uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)}; o.write((char*)b, 2); }
} // namespace

bool loadWav(const std::string& path, AudioBuffer& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parseWav(bytes, out, error);
}

bool parseWav(const std::vector<uint8_t>& b, AudioBuffer& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return fail("not a RIFF/WAVE file");
    size_t pos = 12;
    uint16_t fmt = 0, ch = 0, bits = 0; uint32_t rate = 0;
    bool haveFmt = false;
    const uint8_t* data = nullptr; uint32_t dataLen = 0;
    while (pos + 8 <= b.size()) {
        const uint8_t* hdr = b.data() + pos;
        uint32_t len = rd32(hdr + 4);
        const uint8_t* body = hdr + 8;
        size_t avail = b.size() - (pos + 8);
        if (!std::memcmp(hdr, "fmt ", 4) && len >= 16 && avail >= 16) {
            fmt = rd16(body); ch = rd16(body + 2); rate = rd32(body + 4); bits = rd16(body + 14);
            if (fmt == 0xFFFE && len >= 40 && avail >= 40) fmt = rd16(body + 24); // WAVE_FORMAT_EXTENSIBLE subformat
            haveFmt = true;
        } else if (!std::memcmp(hdr, "data", 4)) {
            data = body; dataLen = uint32_t(std::min<size_t>(len, avail));
        }
        pos += 8 + len + (len & 1);
    }
    if (!haveFmt || !data) return fail("missing fmt or data chunk");
    if (ch == 0 || bits == 0) return fail("invalid format");
    out.sampleRate = int(rate); out.channels = ch;
    size_t bytesPer = bits / 8;
    size_t n = dataLen / bytesPer;
    out.samples.resize(n);
    if (fmt == 1) {
        for (size_t i = 0; i < n; ++i) {
            const uint8_t* p = data + i * bytesPer;
            switch (bits) {
            case 8: out.samples[i] = (int(p[0]) - 128) / 128.0f; break;
            case 16: out.samples[i] = int16_t(rd16(p)) / 32768.0f; break;
            case 24: { int32_t v = int32_t((uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24)) >> 8; out.samples[i] = v / 8388608.0f; break; }
            case 32: out.samples[i] = int32_t(rd32(p)) / 2147483648.0f; break;
            default: return fail("unsupported PCM bit depth");
            }
        }
    } else if (fmt == 3) {
        for (size_t i = 0; i < n; ++i) {
            const uint8_t* p = data + i * bytesPer;
            if (bits == 32) { float v; std::memcpy(&v, p, 4); out.samples[i] = v; }
            else if (bits == 64) { double v; std::memcpy(&v, p, 8); out.samples[i] = float(v); }
            else return fail("unsupported float bit depth");
        }
    } else return fail("unsupported WAV encoding (only PCM / IEEE float)");
    return true;
}

bool saveWav(const std::string& path, const AudioBuffer& buf, std::string* error) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    uint32_t dataLen = uint32_t(buf.samples.size() * 2);
    f.write("RIFF", 4); wr32(f, 36 + dataLen); f.write("WAVE", 4);
    f.write("fmt ", 4); wr32(f, 16); wr16(f, 1); wr16(f, uint16_t(buf.channels)); wr32(f, uint32_t(buf.sampleRate));
    wr32(f, uint32_t(buf.sampleRate * buf.channels * 2)); wr16(f, uint16_t(buf.channels * 2)); wr16(f, 16);
    f.write("data", 4); wr32(f, dataLen);
    for (float s : buf.samples) {
        float c = std::fmax(-1.0f, std::fmin(1.0f, s));
        wr16(f, uint16_t(int16_t(std::lrint(c * 32767.0f))));
    }
    return true;
}

AudioBuffer synthesizeTestSpeech(double seconds, int sampleRate, unsigned seed) {
    AudioBuffer b; b.sampleRate = sampleRate; b.channels = 1;
    b.samples.assign(size_t(seconds * sampleRate), 0.0f);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    // Formant pairs (F1,F2) for a few vowels: a, e, i, o, u
    const float F[5][2] = {{730, 1090}, {530, 1840}, {270, 2290}, {570, 840}, {300, 870}};
    double t = 0.15;
    while (t < seconds - 0.1) {
        double dur = 0.12 + 0.18 * U(rng);
        int v = int(U(rng) * 5) % 5;
        float f0 = 100.0f + 60.0f * U(rng);
        size_t s0 = size_t(t * sampleRate), s1 = std::min(b.samples.size(), size_t((t + dur) * sampleRate));
        for (size_t i = s0; i < s1; ++i) {
            double tt = double(i - s0) / sampleRate;
            double env = std::sin(M_PI * (tt / dur)); // raised-sine syllable envelope
            double phase = 2.0 * M_PI * f0 * tt;
            double src = 0.0;
            for (int h = 1; h <= 12; ++h) src += std::sin(h * phase) / h; // harmonic-rich glottal source
            double form = 0.6 * std::sin(2 * M_PI * F[v][0] * tt) + 0.4 * std::sin(2 * M_PI * F[v][1] * tt);
            b.samples[i] = float(0.5 * env * (0.5 * src * 0.25 + 0.5 * form * (0.6 + 0.4 * std::sin(phase))));
        }
        t += dur + 0.05 + 0.15 * U(rng);
    }
    return b;
}

} // namespace fr
