#pragma once
#include "audio/wav_io.h"
#include <vector>

namespace fr {

struct FeatureConfig {
    int frameSize = 1024;       ///< analysis window in samples
    int hopSize = 512;          ///< hop between frames
    int numMel = 26;            ///< mel filterbank size
    int numMfcc = 13;           ///< MFCC coefficients kept (incl. c0)
    float minHz = 40.0f, maxHz = 8000.0f;
    float pitchMinHz = 60.0f, pitchMaxHz = 400.0f;
    float onsetThreshold = 1.5f; ///< spectral-flux peak threshold relative to local median
};

/// Per-frame acoustic features.
struct AudioFrameFeatures {
    double time = 0.0;             ///< frame centre in seconds
    float rms = 0.0f;              ///< root-mean-square energy
    float loudness = 0.0f;         ///< normalised [0,1] energy (relative to clip max)
    float pitchHz = 0.0f;          ///< 0 when unvoiced
    float voicing = 0.0f;          ///< [0,1] periodicity confidence
    float spectralCentroid = 0.0f; ///< Hz
    float spectralFlux = 0.0f;
    bool onset = false;
    std::vector<float> mfcc;       ///< numMfcc coefficients
    std::vector<float> melEnergies;///< log-mel band energies
};

struct FeatureTrack {
    FeatureConfig config;
    int sampleRate = 0;
    std::vector<AudioFrameFeatures> frames;
    double duration = 0.0;
    double frameInterval() const { return sampleRate ? double(config.hopSize) / sampleRate : 0.0; }
    /// Nearest-frame lookup (clamped). Returns nullptr when empty.
    const AudioFrameFeatures* at(double seconds) const;
};

class FeatureExtractor {
public:
    explicit FeatureExtractor(FeatureConfig cfg = {});
    FeatureTrack extract(const AudioBuffer& audio) const;
    /// Single-frame helpers exposed for unit testing.
    std::vector<float> mfcc(const std::vector<float>& magnitude, int sampleRate, std::vector<float>* melOut = nullptr) const;
    static float rms(const float* s, size_t n);
    static float pitchYin(const float* s, size_t n, int sampleRate, float minHz, float maxHz, float* confidence);
    static float spectralCentroid(const std::vector<float>& mag, int sampleRate, size_t fftSize);
    static float hzToMel(float hz);
    static float melToHz(float mel);

private:
    FeatureConfig cfg_;
    std::vector<std::vector<float>> melFilterbank(int sampleRate, size_t bins) const;
};

} // namespace fr
