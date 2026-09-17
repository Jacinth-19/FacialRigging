#include "audio/features.h"
#include "audio/fft.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace fr {

const AudioFrameFeatures* FeatureTrack::at(double seconds) const {
    if (frames.empty()) return nullptr;
    double dt = frameInterval();
    long i = dt > 0 ? std::lround(seconds / dt) : 0;
    i = std::clamp<long>(i, 0, long(frames.size()) - 1);
    return &frames[size_t(i)];
}

FeatureExtractor::FeatureExtractor(FeatureConfig cfg) : cfg_(cfg) {}

float FeatureExtractor::hzToMel(float hz) { return 2595.0f * std::log10(1.0f + hz / 700.0f); }
float FeatureExtractor::melToHz(float mel) { return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f); }

float FeatureExtractor::rms(const float* s, size_t n) {
    if (!n) return 0.0f;
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += double(s[i]) * s[i];
    return float(std::sqrt(acc / double(n)));
}

// YIN (de Cheveigné & Kawahara 2002) fundamental frequency estimator.
float FeatureExtractor::pitchYin(const float* s, size_t n, int sr, float minHz, float maxHz, float* confidence) {
    if (confidence) *confidence = 0.0f;
    size_t tauMin = size_t(std::max(1.0f, float(sr) / maxHz));
    size_t tauMax = std::min(n / 2, size_t(float(sr) / minHz));
    if (tauMax <= tauMin + 2) return 0.0f;
    std::vector<float> d(tauMax + 1, 0.0f), cmnd(tauMax + 1, 1.0f);
    size_t W = n / 2;
    for (size_t tau = 1; tau <= tauMax; ++tau) {
        double acc = 0.0;
        for (size_t i = 0; i < W; ++i) { float diff = s[i] - s[i + tau]; acc += double(diff) * diff; }
        d[tau] = float(acc);
    }
    double run = 0.0;
    for (size_t tau = 1; tau <= tauMax; ++tau) {
        run += d[tau];
        cmnd[tau] = run > 0.0 ? float(d[tau] * tau / run) : 1.0f;
    }
    const float thresh = 0.15f;
    size_t best = 0;
    for (size_t tau = tauMin; tau <= tauMax; ++tau) {
        if (cmnd[tau] < thresh) {
            while (tau + 1 <= tauMax && cmnd[tau + 1] < cmnd[tau]) ++tau;
            best = tau; break;
        }
    }
    if (!best) { // fall back to global minimum, low confidence
        best = tauMin;
        for (size_t tau = tauMin; tau <= tauMax; ++tau) if (cmnd[tau] < cmnd[best]) best = tau;
        if (cmnd[best] > 0.5f) return 0.0f;
    }
    // parabolic interpolation
    float tauF = float(best);
    if (best > 0 && best + 1 <= tauMax) {
        float a = cmnd[best - 1], b = cmnd[best], c = cmnd[best + 1];
        float den = a - 2 * b + c;
        if (std::abs(den) > 1e-9f) tauF += 0.5f * (a - c) / den;
    }
    if (confidence) *confidence = std::clamp(1.0f - cmnd[best], 0.0f, 1.0f);
    return float(sr) / tauF;
}

float FeatureExtractor::spectralCentroid(const std::vector<float>& mag, int sr, size_t fftSize) {
    double num = 0.0, den = 0.0;
    for (size_t k = 0; k < mag.size(); ++k) { double f = double(k) * sr / double(fftSize); num += f * mag[k]; den += mag[k]; }
    return den > 1e-12 ? float(num / den) : 0.0f;
}

std::vector<std::vector<float>> FeatureExtractor::melFilterbank(int sr, size_t bins) const {
    size_t fftSize = (bins - 1) * 2;
    float maxHz = std::min(cfg_.maxHz, sr * 0.5f);
    float mLo = hzToMel(cfg_.minHz), mHi = hzToMel(maxHz);
    std::vector<float> centres(cfg_.numMel + 2);
    for (int i = 0; i < cfg_.numMel + 2; ++i)
        centres[i] = melToHz(mLo + (mHi - mLo) * i / float(cfg_.numMel + 1)) * fftSize / float(sr); // in bins
    std::vector<std::vector<float>> fb(cfg_.numMel, std::vector<float>(bins, 0.0f));
    for (int m = 0; m < cfg_.numMel; ++m) {
        float l = centres[m], c = centres[m + 1], r = centres[m + 2];
        for (size_t k = 0; k < bins; ++k) {
            float kf = float(k);
            if (kf > l && kf < c) fb[m][k] = (kf - l) / std::max(c - l, 1e-6f);
            else if (kf >= c && kf < r) fb[m][k] = (r - kf) / std::max(r - c, 1e-6f);
        }
    }
    return fb;
}

std::vector<float> FeatureExtractor::mfcc(const std::vector<float>& mag, int sr, std::vector<float>* melOut) const {
    auto fb = melFilterbank(sr, mag.size());
    std::vector<float> logMel(cfg_.numMel);
    for (int m = 0; m < cfg_.numMel; ++m) {
        double e = 0.0;
        for (size_t k = 0; k < mag.size(); ++k) e += double(fb[m][k]) * mag[k] * mag[k];
        logMel[m] = std::log(float(e) + 1e-10f);
    }
    if (melOut) *melOut = logMel;
    std::vector<float> c(cfg_.numMfcc, 0.0f); // DCT-II
    for (int i = 0; i < cfg_.numMfcc; ++i) {
        double acc = 0.0;
        for (int m = 0; m < cfg_.numMel; ++m) acc += logMel[m] * std::cos(M_PI * i * (m + 0.5) / cfg_.numMel);
        c[i] = float(acc);
    }
    return c;
}

FeatureTrack FeatureExtractor::extract(const AudioBuffer& audio) const {
    FeatureTrack track; track.config = cfg_; track.sampleRate = audio.sampleRate; track.duration = audio.duration();
    std::vector<float> x = audio.mono();
    if (x.empty() || audio.sampleRate <= 0) return track;
    const size_t N = size_t(cfg_.frameSize), H = size_t(cfg_.hopSize);
    const size_t fftSize = nextPow2(N);
    auto win = hannWindow(N);
    std::vector<float> frame(N), prevMag;
    std::vector<float> fluxes;
    float maxRms = 1e-6f;
    for (size_t start = 0; start + N / 2 < x.size() + N / 2; start += H) {
        if (start >= x.size()) break;
        for (size_t i = 0; i < N; ++i) frame[i] = (start + i < x.size() ? x[start + i] : 0.0f);
        AudioFrameFeatures f;
        f.time = double(start + N / 2) / audio.sampleRate;
        f.rms = rms(frame.data(), N);
        maxRms = std::max(maxRms, f.rms);
        float conf = 0.0f;
        f.pitchHz = pitchYin(frame.data(), N, audio.sampleRate, cfg_.pitchMinHz, cfg_.pitchMaxHz, &conf);
        f.voicing = conf;
        for (size_t i = 0; i < N; ++i) frame[i] *= win[i];
        auto mag = magnitudeSpectrum(frame, fftSize);
        f.spectralCentroid = spectralCentroid(mag, audio.sampleRate, fftSize);
        if (!prevMag.empty()) {
            double flux = 0.0;
            for (size_t k = 0; k < mag.size(); ++k) { float d = mag[k] - prevMag[k]; if (d > 0) flux += d; }
            f.spectralFlux = float(flux);
        }
        prevMag = mag;
        f.mfcc = mfcc(mag, audio.sampleRate, &f.melEnergies);
        fluxes.push_back(f.spectralFlux);
        track.frames.push_back(std::move(f));
    }
    // normalised loudness
    for (auto& f : track.frames) f.loudness = std::clamp(f.rms / maxRms, 0.0f, 1.0f);
    // onset picking: flux peak above threshold * local median, and a minimum gap
    const int half = 5; size_t lastOnset = size_t(-100);
    for (size_t i = 1; i + 1 < fluxes.size(); ++i) {
        std::vector<float> w;
        for (int k = -half; k <= half; ++k) { long j = long(i) + k; if (j >= 0 && size_t(j) < fluxes.size()) w.push_back(fluxes[size_t(j)]); }
        std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
        float med = w[w.size() / 2];
        bool peak = fluxes[i] > fluxes[i - 1] && fluxes[i] >= fluxes[i + 1];
        if (peak && fluxes[i] > cfg_.onsetThreshold * med + 1e-3f && track.frames[i].loudness > 0.05f && i - lastOnset > 3) {
            track.frames[i].onset = true; lastOnset = i;
        }
    }
    return track;
}

} // namespace fr
