#include "audio/ml_viseme_mapper.h"
#include "audio/speaker_profile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>

#if FR_HAVE_TORCH
#include <torch/script.h>
#include <torch/torch.h>
#endif

namespace fr {

std::vector<float> MlVisemeMapper::featureVector(const AudioFrameFeatures& f, float medianPitchHz) {
    std::vector<float> v(kInputFeatures, 0.0f);
    for (int i = 0; i < 13 && i < int(f.mfcc.size()); ++i) v[i] = f.mfcc[i] / 20.0f; // rough scale
    v[13] = f.loudness;
    v[14] = f.voicing;
    v[15] = (f.pitchHz > 0 && medianPitchHz > 0) ? std::log2(f.pitchHz / medianPitchHz) : 0.0f;
    v[16] = std::clamp(f.spectralCentroid / 4000.0f, 0.0f, 1.0f);
    return v;
}

// ------------------------------------------------------------------------------------------------
// Portable MLP (runs in every build)
std::vector<float> VisemeMlpWeights::forward(const std::vector<float>& xin) const {
    std::vector<float> x = xin;
    if (mean.size() == x.size()) for (size_t i = 0; i < x.size(); ++i) x[i] = (x[i] - mean[i]) * invStd[i];
    for (size_t l = 0; l < W.size(); ++l) {
        const int nOut = layerSizes[l + 1], nIn = layerSizes[l];
        std::vector<float> y(static_cast<size_t>(nOut), 0.0f);
        for (int o = 0; o < nOut; ++o) {
            float acc = b[l][size_t(o)];
            const float* w = &W[l][size_t(o) * size_t(nIn)];
            for (int i = 0; i < nIn; ++i) acc += w[i] * x[size_t(i)];
            y[size_t(o)] = (l + 1 < W.size()) ? std::max(acc, 0.0f) : acc; // ReLU except last
        }
        x.swap(y);
    }
    return x;
}

bool VisemeMlpWeights::save(const std::string& path, std::string* error) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    auto w32 = [&](int32_t v) { std::fwrite(&v, 4, 1, f); };
    std::fwrite("FRVM", 1, 4, f); w32(1); w32(inputs); w32(context); w32(int32_t(layerSizes.size()));
    for (int n : layerSizes) w32(n);
    w32(int32_t(mean.size())); std::fwrite(mean.data(), 4, mean.size(), f); std::fwrite(invStd.data(), 4, invStd.size(), f);
    for (size_t l = 0; l < W.size(); ++l) { std::fwrite(W[l].data(), 4, W[l].size(), f); std::fwrite(b[l].data(), 4, b[l].size(), f); }
    w32(int32_t(info.size())); std::fwrite(info.data(), 1, info.size(), f);
    std::fclose(f);
    return true;
}

bool VisemeMlpWeights::load(const std::string& path, std::string* error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    char magic[4]; int32_t ver = 0, nl = 0;
    auto r32 = [&](int32_t& v) { return std::fread(&v, 4, 1, f) == 1; };
    bool ok = std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "FRVM", 4) == 0 && r32(ver) && ver == 1 && r32(inputs) && r32(context) && r32(nl) && nl >= 2 && nl < 16;
    if (!ok) { std::fclose(f); if (error) *error = "not a valid FRVM model: " + path; return false; }
    layerSizes.assign(size_t(nl), 0); for (auto& n : layerSizes) { r32(n); }
    int32_t nm = 0; r32(nm); mean.assign(size_t(nm), 0.0f); invStd.assign(size_t(nm), 1.0f);
    ok = std::fread(mean.data(), 4, mean.size(), f) == mean.size() && std::fread(invStd.data(), 4, invStd.size(), f) == invStd.size();
    W.clear(); b.clear();
    for (size_t l = 0; ok && l + 1 < layerSizes.size(); ++l) {
        W.emplace_back(size_t(layerSizes[l]) * size_t(layerSizes[l + 1])); b.emplace_back(size_t(layerSizes[l + 1]));
        ok = std::fread(W.back().data(), 4, W.back().size(), f) == W.back().size() && std::fread(b.back().data(), 4, b.back().size(), f) == b.back().size();
    }
    int32_t ni = 0; if (ok && r32(ni) && ni >= 0 && ni < 65536) { info.assign(size_t(ni), '\0'); ok = std::fread(info.data(), 1, info.size(), f) == info.size(); }
    std::fclose(f);
    if (!ok) { if (error) *error = "truncated FRVM model: " + path; W.clear(); layerSizes.clear(); return false; }
    return true;
}

void MlVisemeMapper::applySpeaker(const SpeakerProfile& profile) {
    if (!weights_.valid() || !profile.valid) return;
    weights_ = adaptModel(weights_, profile);
    info_ += " + speaker '" + profile.name + "'" + (profile.lastW.empty() ? "" : " (fine-tuned)");
}

std::vector<std::vector<float>> MlVisemeMapper::stackedFeatures(const FeatureTrack& track, int context) {
    std::vector<float> voiced;
    for (auto& f : track.frames) if (f.pitchHz > 0 && f.voicing > 0.5f) voiced.push_back(f.pitchHz);
    float med = 0.0f;
    if (!voiced.empty()) { std::nth_element(voiced.begin(), voiced.begin() + voiced.size() / 2, voiced.end()); med = voiced[voiced.size() / 2]; }
    const int T = int(track.frames.size()), half = std::max(context, 1) / 2;
    std::vector<std::vector<float>> base(static_cast<size_t>(T), std::vector<float>{});
    for (int t = 0; t < T; ++t) base[size_t(t)] = featureVector(track.frames[size_t(t)], med);
    std::vector<std::vector<float>> out(static_cast<size_t>(T), std::vector<float>{});
    for (int t = 0; t < T; ++t) {
        out[size_t(t)].reserve(size_t(kInputFeatures * context));
        for (int c = -half; c <= half; ++c) {
            int u = std::clamp(t + c, 0, T - 1);
            out[size_t(t)].insert(out[size_t(t)].end(), base[size_t(u)].begin(), base[size_t(u)].end());
        }
    }
    return out;
}

std::vector<VisemeFrame> MlVisemeMapper::mapWithWeights(const FeatureTrack& track) const {
    std::vector<VisemeFrame> out;
    auto X = stackedFeatures(track, weights_.context);
    std::array<float, size_t(Viseme::Count)> prev{}; prev[0] = 1.0f;
    for (size_t t = 0; t < X.size(); ++t) {
        VisemeFrame vf; vf.time = track.frames[t].time;
        auto logits = weights_.forward(X[t]);
        float mx = *std::max_element(logits.begin(), logits.end()), sum = 0.0f;
        for (auto& v : logits) { v = std::exp(v - mx); sum += v; }
        if (track.frames[t].loudness < silenceLoudness) { vf.weights = {}; vf.weights[0] = 1.0f; }
        else for (size_t k = 0; k < vf.weights.size() && k < logits.size(); ++k) vf.weights[k] = logits[k] / sum;
        for (size_t i = 0; i < vf.weights.size(); ++i) vf.weights[i] = smoothing * prev[i] + (1.0f - smoothing) * vf.weights[i];
        prev = vf.weights;
        out.push_back(vf);
    }
    return out;
}

bool MlVisemeMapper::loadDefault(const std::string& assetDir, std::string* error) {
    return load(assetDir + "/models/viseme_mlp.frvm", error);
}

#if FR_HAVE_TORCH
// ------------------------------------------------------------------------------------------------
// LibTorch implementation
struct MlVisemeMapper::Impl {
    torch::jit::script::Module scripted;
    bool haveScripted = false;
    torch::nn::Sequential builtin{nullptr};
    bool haveBuiltin = false;
};

MlVisemeMapper::MlVisemeMapper() : impl_(new Impl) {}
MlVisemeMapper::~MlVisemeMapper() = default;
bool MlVisemeMapper::available() { return true; }
bool MlVisemeMapper::loaded() const { return impl_->haveScripted || impl_->haveBuiltin || weights_.valid(); }

bool MlVisemeMapper::load(const std::string& path, std::string* error) {
    if (path.size() > 5 && path.compare(path.size() - 5, 5, ".frvm") == 0) {
        if (!weights_.load(path, error)) return false;
        impl_->haveScripted = impl_->haveBuiltin = false;
        info_ = "trained MLP " + path + (weights_.info.empty() ? "" : " [" + weights_.info + "]");
        return true;
    }
    try {
        impl_->scripted = torch::jit::load(path, torch::kCPU);
        impl_->scripted.eval();
        impl_->haveScripted = true; impl_->haveBuiltin = false;
        info_ = "TorchScript model: " + path;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool MlVisemeMapper::loadBuiltin(std::string* error) {
    try {
        torch::NoGradGuard ng;
        // Two-layer MLP with hand-set weights on the interpretable features (13..16) so the
        // network reproduces the acoustic heuristics; the MFCC inputs start at zero weight and
        // are what a training run would learn from.
        auto l1 = torch::nn::Linear(kInputFeatures, 16);
        auto l2 = torch::nn::Linear(16, kOutputs);
        l1->weight.zero_(); l1->bias.zero_(); l2->weight.zero_(); l2->bias.zero_();
        // hidden units: 0 loud, 1 voiced, 2 bright, 3 dark(1-bright), 4 quiet(1-loud), 5 unvoiced, 6 highpitch
        auto W1 = l1->weight.accessor<float, 2>(); auto b1 = l1->bias.accessor<float, 1>();
        W1[0][13] = 4.0f;  b1[0] = -1.0f;
        W1[1][14] = 4.0f;  b1[1] = -2.0f;
        W1[2][16] = 6.0f;  b1[2] = -2.0f;
        W1[3][16] = -6.0f; b1[3] = 2.0f;
        W1[4][13] = -4.0f; b1[4] = 1.0f;
        W1[5][14] = -4.0f; b1[5] = 2.0f;
        W1[6][15] = 3.0f;  b1[6] = 0.0f;
        auto W2 = l2->weight.accessor<float, 2>(); auto b2 = l2->bias.accessor<float, 1>();
        const int S = int(Viseme::Silence), AA = int(Viseme::AA), EE = int(Viseme::EE), IH = int(Viseme::IH), OH = int(Viseme::OH), UW = int(Viseme::UW), MBP = int(Viseme::MBP), FV = int(Viseme::FV), LTH = int(Viseme::L_TH);
        b2[S] = 1.0f; W2[S][4] = 3.0f; W2[S][0] = -3.0f;
        W2[AA][0] = 2.0f; W2[AA][1] = 1.5f; W2[AA][3] = 0.5f;
        W2[EE][0] = 1.0f; W2[EE][1] = 1.5f; W2[EE][2] = 2.0f;
        W2[IH][0] = 0.8f; W2[IH][1] = 1.0f; b2[IH] = 0.2f;
        W2[OH][0] = 1.2f; W2[OH][1] = 1.5f; W2[OH][3] = 1.5f;
        W2[UW][1] = 1.0f; W2[UW][3] = 2.0f; W2[UW][4] = 0.5f;
        W2[MBP][5] = 1.5f; W2[MBP][3] = 1.0f; W2[MBP][4] = 0.5f;
        W2[FV][5] = 1.5f; W2[FV][2] = 1.5f;
        W2[LTH][1] = 0.5f; b2[LTH] = -0.5f;
        impl_->builtin = torch::nn::Sequential(l1, torch::nn::ReLU(), l2);
        impl_->builtin->eval();
        impl_->haveBuiltin = true; impl_->haveScripted = false; weights_ = VisemeMlpWeights{};
        info_ = "built-in MLP (17-16-9, hand-initialised)";
        return true;
    } catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}

bool MlVisemeMapper::save(const std::string& path, std::string* error) const {
    if (!impl_->haveBuiltin) { if (error) *error = "only in-process (built-in) models can be saved; TorchScript files are already on disk"; return false; }
    try {
        // Serialise the parameters (not a full TorchScript graph) so a Python trainer can load them.
        std::vector<torch::Tensor> params; for (auto& p : impl_->builtin->parameters()) params.push_back(p);
        torch::save(params, path);
        return true;
    } catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}

std::vector<VisemeFrame> MlVisemeMapper::map(const FeatureTrack& track) const {
    std::vector<VisemeFrame> out;
    if (!loaded()) return VisemeMapper::map(track); // graceful: rule-based
    if (track.frames.empty()) return out;
    if (weights_.valid()) return mapWithWeights(track);
    std::vector<float> voiced;
    for (auto& f : track.frames) if (f.pitchHz > 0 && f.voicing > 0.5f) voiced.push_back(f.pitchHz);
    float med = 0.0f;
    if (!voiced.empty()) { std::nth_element(voiced.begin(), voiced.begin() + voiced.size() / 2, voiced.end()); med = voiced[voiced.size() / 2]; }
    const int64_t T = int64_t(track.frames.size());
    torch::Tensor x = torch::zeros({T, kInputFeatures});
    auto acc = x.accessor<float, 2>();
    for (int64_t t = 0; t < T; ++t) { auto v = featureVector(track.frames[size_t(t)], med); for (int i = 0; i < kInputFeatures; ++i) acc[t][i] = v[size_t(i)]; }
    torch::Tensor y;
    try {
        torch::NoGradGuard ng;
        if (impl_->haveScripted) y = impl_->scripted.forward({x}).toTensor();
        else y = impl_->builtin->forward(x);
        if (y.dim() == 3) y = y.squeeze(0);
        y = torch::softmax(y.to(torch::kFloat32), -1).contiguous();
    } catch (const std::exception&) { return VisemeMapper::map(track); }
    auto ya = y.accessor<float, 2>();
    std::array<float, size_t(Viseme::Count)> prev{}; prev[0] = 1.0f;
    for (int64_t t = 0; t < T; ++t) {
        VisemeFrame vf; vf.time = track.frames[size_t(t)].time;
        if (track.frames[size_t(t)].loudness < silenceLoudness) { vf.weights = {}; vf.weights[0] = 1.0f; }
        else for (int k = 0; k < kOutputs && k < int(y.size(1)); ++k) vf.weights[size_t(k)] = ya[t][k];
        for (size_t i = 0; i < vf.weights.size(); ++i) vf.weights[i] = smoothing * prev[i] + (1.0f - smoothing) * vf.weights[i];
        prev = vf.weights;
        out.push_back(vf);
    }
    return out;
}

#else
// ------------------------------------------------------------------------------------------------
// Stub when LibTorch is unavailable: behaves like the rule-based mapper.
// Without LibTorch: trained .frvm models still run (pure C++), TorchScript / training do not.
struct MlVisemeMapper::Impl {};
MlVisemeMapper::MlVisemeMapper() : impl_(new Impl) { info_ = "LibTorch not compiled in (.frvm models still supported)"; }
MlVisemeMapper::~MlVisemeMapper() = default;
bool MlVisemeMapper::available() { return true; }
bool MlVisemeMapper::loaded() const { return weights_.valid(); }
bool MlVisemeMapper::load(const std::string& path, std::string* error) {
    if (path.size() > 5 && path.compare(path.size() - 5, 5, ".frvm") == 0) {
        if (!weights_.load(path, error)) return false;
        info_ = "trained MLP " + path + (weights_.info.empty() ? "" : " [" + weights_.info + "]");
        return true;
    }
    if (error) *error = "TorchScript models need a LibTorch build (FR_WITH_TORCH=ON); use a .frvm model";
    return false;
}
bool MlVisemeMapper::loadBuiltin(std::string* error) { if (error) *error = "built-in torch MLP needs FR_WITH_TORCH=ON"; return false; }
bool MlVisemeMapper::save(const std::string&, std::string* error) const { if (error) *error = "LibTorch not compiled in"; return false; }
std::vector<VisemeFrame> MlVisemeMapper::map(const FeatureTrack& track) const { return weights_.valid() ? mapWithWeights(track) : VisemeMapper::map(track); }
#endif

} // namespace fr
