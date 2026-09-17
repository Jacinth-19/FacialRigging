#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fr {

struct AudioBuffer {
    int sampleRate = 44100;
    int channels = 1;
    std::vector<float> samples;        ///< interleaved, [-1,1]
    size_t frames() const { return channels ? samples.size() / channels : 0; }
    double duration() const { return sampleRate ? double(frames()) / sampleRate : 0.0; }
    std::vector<float> mono() const;   ///< channel-averaged copy
};

/// Reads RIFF/WAVE PCM 8/16/24/32-bit and IEEE float 32/64. Returns false on failure.
bool loadWav(const std::string& path, AudioBuffer& out, std::string* error = nullptr);
bool parseWav(const std::vector<uint8_t>& bytes, AudioBuffer& out, std::string* error = nullptr);
/// Writes 16-bit PCM WAV.
bool saveWav(const std::string& path, const AudioBuffer& buf, std::string* error = nullptr);

/// Synthesises a crude "speech-like" test signal: a sequence of vowel-ish formant bursts
/// separated by silence. Handy for tests/demos when no recording is available.
AudioBuffer synthesizeTestSpeech(double seconds = 3.0, int sampleRate = 22050, unsigned seed = 7);

} // namespace fr
