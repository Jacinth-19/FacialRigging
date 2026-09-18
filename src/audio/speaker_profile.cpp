#include <cstdlib>
#include "audio/speaker_profile.h"
#include "core/json_io.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fr {

const char* calibrationSentence() {
    // ~10 s at a natural pace; hits bilabials (MBP), labiodentals (FV), rounded (UW/OH), spread (EE/IH), open (AA) and TH/L.
    return "The big brown fox jumps over the lazy dog. "
           "Five very fine violins played merrily by the moonlit bay. Who threw those three thin leaves through the blue room? "
           "Papa bought a boat, Mama made more marmalade, and we all laughed out loud.";
}

namespace {
constexpr int kDims = MlVisemeMapper::kInputFeatures;   // 17
// Statistics are gathered over *all* frames (silence included) so they match how the global normalisation was estimated at training time;
// using only loud frames biases the mean and hurt TIMIT accuracy by ~9 points at strength 1.
static const float kActiveThreshold = 0.0f;
bool isBoundedDim(int d) { return d == 13 || d == 14 || d == 16; }   // loudness, voicing, centroid: keep global scale
}

SpeakerProfile calibrateSpeaker(const AudioBuffer& audio, const std::string& name, float minSeconds, CalibrationStats* st) {
    FeatureExtractor fx; FeatureTrack t = fx.extract(audio);
    SpeakerProfile p = calibrateSpeaker(t, name, minSeconds, st);
    return p;
}

SpeakerProfile calibrateSpeaker(const FeatureTrack& track, const std::string& name, float minSeconds, CalibrationStats* st) {
    SpeakerProfile p; p.name = name; p.dims = kDims;
    CalibrationStats s;
    if (track.frames.empty()) { s.warning = "no audio"; if (st) *st = s; return p; }
    // active frames (for the seconds/voicing/SNR readout use loud frames; statistics use all frames, see kActiveThreshold)
    std::vector<size_t> active; float noise = 0; int noiseN = 0;
    for (size_t i = 0; i < track.frames.size(); ++i) { if (track.frames[i].loudness > kActiveThreshold) active.push_back(i); else { noise += track.frames[i].rms; ++noiseN; } }
    s.activeFrames = int(active.size()); s.seconds = float(active.size() * track.frameInterval());
    std::vector<float> voiced; for (size_t i : active) if (track.frames[i].pitchHz > 0 && track.frames[i].voicing > 0.5f) voiced.push_back(track.frames[i].pitchHz);
    s.voicedFraction = active.empty() ? 0 : float(voiced.size()) / float(active.size());
    float speechRms = 0; for (size_t i : active) speechRms += track.frames[i].rms; speechRms /= float(std::max<size_t>(1, active.size()));
    float noiseRms = noiseN ? noise / float(noiseN) : 1e-5f;
    s.snrDb = 20.0f * std::log10(std::max(speechRms, 1e-6f) / std::max(noiseRms, 1e-6f));
    if (s.seconds < minSeconds) { s.warning = "only " + std::to_string(int(s.seconds * 10) / 10.0f).substr(0, 4) + " s of speech - need at least " + std::to_string(int(minSeconds)) + " s"; if (st) *st = s; return p; }
    if (s.voicedFraction < 0.2f) s.warning = "little voiced speech detected - check the microphone";
    if (s.snrDb < 10.0f) s.warning = "noisy recording (SNR " + std::to_string(int(s.snrDb)) + " dB) - the profile may be unreliable";
    float med = 0; if (!voiced.empty()) { std::nth_element(voiced.begin(), voiced.begin() + voiced.size() / 2, voiced.end()); med = voiced[voiced.size() / 2]; }
    p.pitchMedianHz = med;
    p.mean.assign(size_t(kDims), 0); p.stdev.assign(size_t(kDims), 1);
    std::vector<std::vector<float>> X; X.reserve(active.size());
    for (size_t i : active) X.push_back(MlVisemeMapper::featureVector(track.frames[i], med));
    for (auto& x : X) for (int d = 0; d < kDims; ++d) p.mean[size_t(d)] += x[size_t(d)];
    for (auto& m : p.mean) m /= float(X.size());
    for (auto& x : X) for (int d = 0; d < kDims; ++d) { float dd = x[size_t(d)] - p.mean[size_t(d)]; p.stdev[size_t(d)] += dd * dd; }
    for (auto& v : p.stdev) v = std::sqrt(std::max(v - 1.0f, 0.0f) / float(X.size()) + 1e-6f);
    std::vector<float> rmsDb; for (size_t i : active) rmsDb.push_back(20.0f * std::log10(std::max(track.frames[i].rms, 1e-6f)));
    std::sort(rmsDb.begin(), rmsDb.end()); p.loudnessRef = rmsDb[size_t(0.9 * double(rmsDb.size() - 1))];
    p.secondsUsed = s.seconds; p.framesUsed = int(active.size()); p.valid = true;
    if (st) *st = s;
    return p;
}

VisemeMlpWeights adaptModel(const VisemeMlpWeights& model, const SpeakerProfile& profile) {
    VisemeMlpWeights m = model;
    if (!profile.valid || !m.valid() || m.inputs != profile.dims || m.mean.size() != size_t(m.inputs * m.context)) return m;
    const float a = std::clamp(profile.strength, 0.0f, 1.0f);
    // Global per-base-dim stats are replicated per context slot; recover them from slot 0..context-1 (identical by construction).
    for (size_t i = 0; i < m.mean.size(); ++i) {
        int d = int(i % size_t(m.inputs));
        float gMean = model.mean[i], gStd = 1.0f / std::max(model.invStd[i], 1e-6f);
        float sMean = profile.mean[size_t(d)], sStd = profile.stdev[size_t(d)];
        float nMean = gMean + a * (sMean - gMean);
        float nStd = isBoundedDim(d) ? gStd : gStd + a * (std::clamp(sStd, 0.5f * gStd, 2.0f * gStd) - gStd);   // limit scale change to 2x
        m.mean[i] = nMean; m.invStd[i] = 1.0f / std::max(nStd, 1e-6f);
    }
    if (!profile.lastW.empty() && profile.lastW.size() == m.W.back().size() && profile.lastB.size() == m.b.back().size()) { m.W.back() = profile.lastW; m.b.back() = profile.lastB; }
    if (!m.info.empty()) m.info += " + speaker '" + profile.name + "'";
    return m;
}

bool fineTuneLastLayer(const VisemeMlpWeights& model, SpeakerProfile& profile, const FeatureTrack& track, const std::vector<int>& labels, int steps, float lr, float anchor, float* accBefore, float* accAfter) {
    if (!profile.valid || !model.valid() || labels.size() != track.frames.size()) return false;
    VisemeMlpWeights base = adaptModel(model, profile); base.W.back() = model.W.back(); base.b.back() = model.b.back();
    auto X = MlVisemeMapper::stackedFeatures(track, base.context);
    // penultimate activations
    const size_t L = base.W.size(); const int K = base.layerSizes.back(), H = base.layerSizes[L - 1];
    std::vector<std::vector<float>> hidden; std::vector<int> y;
    for (size_t t = 0; t < X.size(); ++t) {
        if (labels[t] < 0 || labels[t] >= K) continue;
        std::vector<float> x = X[t];
        for (size_t i = 0; i < x.size(); ++i) x[i] = (x[i] - base.mean[i]) * base.invStd[i];
        for (size_t l = 0; l + 1 < L; ++l) {
            const int nOut = base.layerSizes[l + 1], nIn = base.layerSizes[l]; std::vector<float> yv(size_t(nOut), 0.0f);
            for (int o = 0; o < nOut; ++o) { float acc = base.b[l][size_t(o)]; const float* w = &base.W[l][size_t(o) * size_t(nIn)]; for (int i = 0; i < nIn; ++i) acc += w[i] * x[size_t(i)]; yv[size_t(o)] = std::max(acc, 0.0f); }
            x.swap(yv);
        }
        hidden.push_back(x); y.push_back(labels[t]);
    }
    if (hidden.size() < 20) return false;
    std::vector<float> W = model.W.back(), B = model.b.back(); const std::vector<float> W0 = W, B0 = B;
    std::vector<int> counts(size_t(K), 0); for (int c : y) ++counts[size_t(c)];
    std::vector<float> cw(size_t(K), 0); for (int k = 0; k < K; ++k) cw[size_t(k)] = counts[size_t(k)] ? 1.0f / std::sqrt(float(counts[size_t(k)])) : 0; { float s = 0; for (float w : cw) s += w; for (float& w : cw) w *= float(K) / std::max(s, 1e-6f); }
    auto evalAcc = [&](const std::vector<float>& Wc, const std::vector<float>& Bc) {
        int ok = 0; for (size_t n = 0; n < hidden.size(); ++n) { int best = 0; float bv = -1e9f; for (int k = 0; k < K; ++k) { float a = Bc[size_t(k)]; for (int i = 0; i < H; ++i) a += Wc[size_t(k * H + i)] * hidden[n][size_t(i)]; if (a > bv) { bv = a; best = k; } } ok += best == y[n]; }
        return float(ok) / float(hidden.size());
    };
    if (accBefore) *accBefore = evalAcc(W, B);
    std::vector<float> mW(W.size(), 0), vW(W.size(), 0), mB(B.size(), 0), vB(B.size(), 0);
    const float b1 = 0.9f, b2 = 0.999f, eps = 1e-8f;
    for (int s = 1; s <= steps; ++s) {
        std::vector<float> gW(W.size(), 0), gB(B.size(), 0); float wsum = 0;
        for (size_t n = 0; n < hidden.size(); ++n) {
            std::vector<float> p(size_t(K), 0.0f); float mx = -1e9f;
            for (int k = 0; k < K; ++k) { float a = B[size_t(k)]; for (int i = 0; i < H; ++i) a += W[size_t(k * H + i)] * hidden[n][size_t(i)]; p[size_t(k)] = a; mx = std::max(mx, a); }
            float sum = 0; for (auto& v : p) { v = std::exp(v - mx); sum += v; } for (auto& v : p) v /= sum;
            float w = cw[size_t(y[n])]; wsum += w;
            for (int k = 0; k < K; ++k) { float d = w * (p[size_t(k)] - (k == y[n] ? 1.0f : 0.0f)); gB[size_t(k)] += d; for (int i = 0; i < H; ++i) gW[size_t(k * H + i)] += d * hidden[n][size_t(i)]; }
        }
        float inv = 1.0f / std::max(wsum, 1e-6f), c1 = 1 - std::pow(b1, float(s)), c2 = 1 - std::pow(b2, float(s));
        auto adam = [&](std::vector<float>& P, const std::vector<float>& P0, std::vector<float>& g, std::vector<float>& m, std::vector<float>& v) {
            for (size_t i = 0; i < P.size(); ++i) { float gi = g[i] * inv + anchor * (P[i] - P0[i]); m[i] = b1 * m[i] + (1 - b1) * gi; v[i] = b2 * v[i] + (1 - b2) * gi * gi; P[i] -= lr * (m[i] / c1) / (std::sqrt(v[i] / c2) + eps); }
        };
        adam(W, W0, gW, mW, vW); adam(B, B0, gB, mB, vB);
    }
    float after = evalAcc(W, B); if (accAfter) *accAfter = after;
    profile.lastW = W; profile.lastB = B;
    char buf[96]; std::snprintf(buf, sizeof buf, "last layer fine-tuned on %zu frames, %d steps", hidden.size(), steps); profile.fineTuneInfo = buf;
    return true;
}

// ---------------------------------------------------------------- JSON
std::string SpeakerProfile::toJson() const {
    std::ostringstream o; o.precision(7);
    auto arr = [&](const std::vector<float>& v) { o << "["; for (size_t i = 0; i < v.size(); ++i) { if (i) o << ","; o << v[i]; } o << "]"; };
    o << "{\"name\":" << json::str(name) << ",\"valid\":" << (valid ? "true" : "false") << ",\"dims\":" << dims << ",\"pitchMedianHz\":" << pitchMedianHz << ",\"loudnessRef\":" << loudnessRef
      << ",\"strength\":" << strength << ",\"secondsUsed\":" << secondsUsed << ",\"framesUsed\":" << framesUsed << ",\"source\":" << json::str(source) << ",\"fineTuneInfo\":" << json::str(fineTuneInfo)
      << ",\"mean\":"; arr(mean); o << ",\"stdev\":"; arr(stdev); o << ",\"lastW\":"; arr(lastW); o << ",\"lastB\":"; arr(lastB); o << "}";
    return o.str();
}

bool SpeakerProfile::fromJson(const std::string& s, std::string* error) {
    json::Cursor c(s); *this = SpeakerProfile{};
    auto numArr = [&](std::vector<float>& out) { return c.numArr(out); };
    bool ok = json::objEach(c, [&](const std::string& k) {
        if (k == "name") return c.str(name);
        if (k == "source") return c.str(source);
        if (k == "fineTuneInfo") return c.str(fineTuneInfo);
        if (k == "valid") return json::boolean(c, valid);
        float v; if (k == "dims") { if (!c.num(v)) return false; dims = int(v); return true; } if (k == "pitchMedianHz") return c.num(pitchMedianHz); if (k == "loudnessRef") return c.num(loudnessRef);
        if (k == "strength") return c.num(strength);
        if (k == "secondsUsed") return c.num(secondsUsed);
        if (k == "framesUsed") { if (!c.num(v)) return false; framesUsed = int(v); return true; }
        if (k == "mean") return numArr(mean); if (k == "stdev") return numArr(stdev); if (k == "lastW") return numArr(lastW); if (k == "lastB") return numArr(lastB);
        return c.skipValue();
    });
    if (!ok) { if (error) *error = "malformed speaker profile"; valid = false; return false; }
    if (valid && (dims <= 0 || int(mean.size()) != dims || int(stdev.size()) != dims)) { if (error) *error = "speaker profile dimension mismatch"; valid = false; return false; }
    return true;
}

bool SpeakerProfile::save(const std::string& path, std::string* error) const { std::ofstream f(path); if (!f) { if (error) *error = "cannot write " + path; return false; } f << toJson() << "\n"; return true; }
bool SpeakerProfile::load(const std::string& path, std::string* error) { std::ifstream f(path); if (!f) { if (error) *error = "cannot open " + path; return false; } std::stringstream ss; ss << f.rdbuf(); return fromJson(ss.str(), error); }

} // namespace fr
