#include "audio/emotion_classifier.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace fr {

const char* emotionName(Emotion e) { static const char* n[] = {"anger", "disgust", "fear", "happy", "neutral", "sad"}; return n[std::clamp(int(e), 0, 5)]; }
const char* emotionPresetName(Emotion e) { static const char* n[] = {"angry", "disgusted", "surprised", "happy", "neutral", "sad"}; return n[std::clamp(int(e), 0, 5)]; }
Emotion emotionFromCremaCode(const std::string& c, bool* ok) {
    if (ok) *ok = true;
    if (c == "ANG") return Emotion::Anger; if (c == "DIS") return Emotion::Disgust; if (c == "FEA") return Emotion::Fear;
    if (c == "HAP") return Emotion::Happy; if (c == "NEU") return Emotion::Neutral; if (c == "SAD") return Emotion::Sad;
    if (ok) *ok = false; return Emotion::Neutral;
}

std::string EmotionResult::summary() const {
    if (!valid) return "no emotion estimate";
    std::vector<int> idx(size_t(Emotion::Count)); std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return probs[size_t(a)] > probs[size_t(b)]; });
    char buf[160]; std::snprintf(buf, sizeof buf, "%s %.0f%% (%s %.0f%%, %s %.0f%%)", emotionName(Emotion(idx[0])), probs[size_t(idx[0])] * 100, emotionName(Emotion(idx[1])), probs[size_t(idx[1])] * 100, emotionName(Emotion(idx[2])), probs[size_t(idx[2])] * 100);
    return buf;
}

namespace {
// per-dimension statistics over the frames: mean, std, p10, p90 (robust range) and mean delta
constexpr int kStats = 5;
constexpr int kNumMfcc = 13, kNumMel = 26;
// scalar streams: loudness, voicing, log-pitch (voiced frames), centroid, flux  -> 5 streams
constexpr int kScalars = 5;
// extra globals: voiced fraction, onset rate (/s), speech fraction, pitch range (p90-p10 semitones),
// loudness dynamics (p90-p10), duration (log s), pitch slope, loudness slope
constexpr int kGlobals = 8;
constexpr int kDims = kNumMfcc + kNumMel + kScalars;

void stats(std::vector<float>& v, float* out) {
    if (v.empty()) { std::fill(out, out + kStats, 0.0f); return; }
    double m = 0; for (float x : v) m += x; m /= double(v.size());
    double var = 0; for (float x : v) var += (x - m) * (x - m); var /= double(v.size());
    double dm = 0; for (size_t i = 1; i < v.size(); ++i) dm += std::fabs(v[i] - v[i - 1]); dm /= double(std::max<size_t>(1, v.size() - 1));
    std::vector<float> s = v; std::sort(s.begin(), s.end());
    auto pct = [&](double p) { return s[size_t(std::clamp(p * double(s.size() - 1), 0.0, double(s.size() - 1)))]; };
    out[0] = float(m); out[1] = float(std::sqrt(var)); out[2] = pct(0.1); out[3] = pct(0.9); out[4] = float(dm);
}
float slope(const std::vector<float>& y) {   // least-squares slope per frame, x = 0..n-1
    size_t n = y.size(); if (n < 3) return 0.0f;
    double mx = double(n - 1) / 2.0, my = 0; for (float v : y) my += v; my /= double(n);
    double num = 0, den = 0; for (size_t i = 0; i < n; ++i) { num += (double(i) - mx) * (y[i] - my); den += (double(i) - mx) * (double(i) - mx); }
    return den > 0 ? float(num / den * double(n)) : 0.0f;   // total change over the utterance
}
} // namespace

int EmotionClassifier::descriptorSize() { return kDims * kStats + kGlobals; }

std::vector<std::string> EmotionClassifier::descriptorNames() {
    std::vector<std::string> n; static const char* st[] = {"mean", "std", "p10", "p90", "delta"};
    for (int d = 0; d < kDims; ++d) {
        std::string base = d < kNumMfcc ? "mfcc" + std::to_string(d) : d < kNumMfcc + kNumMel ? "mel" + std::to_string(d - kNumMfcc) : std::vector<std::string>{"loud", "voicing", "logpitch", "centroid", "flux"}[size_t(d - kNumMfcc - kNumMel)];
        for (int s = 0; s < kStats; ++s) n.push_back(base + "." + st[s]);
    }
    for (const char* g : {"voicedFrac", "onsetRate", "speechFrac", "pitchRangeSt", "loudRange", "logDur", "pitchSlope", "loudSlope"}) n.push_back(g);
    return n;
}

std::vector<float> EmotionClassifier::describe(const FeatureTrack& track, double windowSeconds) {
    std::vector<float> out(size_t(descriptorSize()), 0.0f);
    if (track.frames.empty()) return out;
    size_t first = 0;
    if (windowSeconds > 0) { double t0 = track.frames.back().time - windowSeconds; while (first + 1 < track.frames.size() && track.frames[first].time < t0) ++first; }
    // active frames: drop leading/trailing silence so pauses don't dominate the statistics
    float maxRms = 0; for (size_t i = first; i < track.frames.size(); ++i) maxRms = std::max(maxRms, track.frames[i].rms);
    const float thr = std::max(maxRms * 0.05f, 1e-4f);
    std::vector<size_t> active; for (size_t i = first; i < track.frames.size(); ++i) if (track.frames[i].rms > thr) active.push_back(i);
    if (active.size() < 3) for (size_t i = first; i < track.frames.size(); ++i) active.push_back(i);
    std::vector<std::vector<float>> streams; streams.resize(size_t(kDims));
    std::vector<float> logPitch; int onsets = 0, voiced = 0;
    for (size_t i : active) {
        const AudioFrameFeatures& f = track.frames[i];
        for (int k = 0; k < kNumMfcc; ++k) streams[size_t(k)].push_back(k < int(f.mfcc.size()) ? f.mfcc[size_t(k)] / 20.0f : 0.0f);
        for (int k = 0; k < kNumMel; ++k) streams[size_t(kNumMfcc + k)].push_back(k < int(f.melEnergies.size()) ? f.melEnergies[size_t(k)] / 10.0f : 0.0f);
        streams[size_t(kNumMfcc + kNumMel + 0)].push_back(f.loudness);
        streams[size_t(kNumMfcc + kNumMel + 1)].push_back(f.voicing);
        if (f.pitchHz > 0) { logPitch.push_back(std::log2(f.pitchHz / 100.0f)); ++voiced; }
        streams[size_t(kNumMfcc + kNumMel + 3)].push_back(std::clamp(f.spectralCentroid / 4000.0f, 0.0f, 2.0f));
        streams[size_t(kNumMfcc + kNumMel + 4)].push_back(std::min(f.spectralFlux, 10.0f));
        onsets += f.onset ? 1 : 0;
    }
    streams[size_t(kNumMfcc + kNumMel + 2)] = logPitch;
    for (int d = 0; d < kDims; ++d) stats(streams[size_t(d)], &out[size_t(d * kStats)]);
    float* g = &out[size_t(kDims * kStats)];
    const double dur = std::max(0.05, track.frames[active.back()].time - track.frames[active.front()].time + track.frameInterval());
    g[0] = float(voiced) / float(active.size());
    g[1] = float(onsets / dur);
    g[2] = float(active.size()) / float(track.frames.size() - first);
    g[3] = logPitch.empty() ? 0.0f : (out[size_t((kNumMfcc + kNumMel + 2) * kStats + 3)] - out[size_t((kNumMfcc + kNumMel + 2) * kStats + 2)]) * 12.0f;
    g[4] = out[size_t((kNumMfcc + kNumMel + 0) * kStats + 3)] - out[size_t((kNumMfcc + kNumMel + 0) * kStats + 2)];
    g[5] = float(std::log(dur));
    g[6] = slope(logPitch);
    g[7] = slope(streams[size_t(kNumMfcc + kNumMel + 0)]);
    return out;
}

bool EmotionClassifier::load(const std::string& path, std::string* error) {
    VisemeMlpWeights w; if (!w.load(path, error)) return false;
    if (w.layerSizes.empty() || w.layerSizes.front() != descriptorSize() || w.layerSizes.back() != kClasses) { if (error) *error = "not an emotion model (layer sizes mismatch)"; return false; }
    weights_ = std::move(w); return true;
}
bool EmotionClassifier::loadDefault(const std::string& assetDir, std::string* error) { return load(assetDir + "/models/emotion_mlp.frvm", error); }

EmotionResult EmotionClassifier::fromProbs(const std::array<float, size_t(Emotion::Count)>& p) {
    EmotionResult r; r.valid = true; r.probs = p;
    int top = int(std::max_element(p.begin(), p.end()) - p.begin()); r.top = Emotion(top); r.confidence = p[size_t(top)];
    // arousal: high for anger/fear/happy, mid for disgust, low for neutral/sad
    r.arousal = std::clamp(0.9f * p[0] + 0.5f * p[1] + 0.85f * p[2] + 0.75f * p[3] + 0.2f * p[4] + 0.15f * p[5], 0.0f, 1.0f);
    if (r.top == Emotion::Neutral || r.confidence < 0.3f) { r.presetName = "neutral"; r.presetAmount = 0.0f; }
    else {
        r.presetName = emotionPresetName(r.top);
        // amount grows with margin over neutral; 0.35..0.9
        float margin = std::clamp(r.confidence - p[size_t(Emotion::Neutral)], 0.0f, 1.0f);
        r.presetAmount = std::clamp(0.35f + 0.6f * margin, 0.0f, 0.9f);
    }
    return r;
}

EmotionResult EmotionClassifier::classify(const FeatureTrack& track, double windowSeconds) const {
    if (!loaded() || track.frames.empty()) return {};
    std::vector<float> x = describe(track, windowSeconds);
    std::vector<float> logits = weights_.forward(x);
    if (int(logits.size()) != kClasses) return {};
    float mx = *std::max_element(logits.begin(), logits.end()), sum = 0; std::array<float, size_t(Emotion::Count)> p{};
    for (int k = 0; k < kClasses; ++k) { p[size_t(k)] = std::exp(logits[size_t(k)] - mx); sum += p[size_t(k)]; }
    for (auto& v : p) v /= sum;
    return fromProbs(p);
}

EmotionResult EmotionClassifier::classify(const AudioBuffer& audio) const {
    if (audio.samples.empty()) return {};
    FeatureExtractor fx; return classify(fx.extract(audio));
}

} // namespace fr
