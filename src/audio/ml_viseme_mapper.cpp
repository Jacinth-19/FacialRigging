#include "audio/ml_viseme_mapper.h"
#include <algorithm>
#include <cmath>

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
bool MlVisemeMapper::loaded() const { return impl_->haveScripted || impl_->haveBuiltin; }

bool MlVisemeMapper::load(const std::string& path, std::string* error) {
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
        impl_->haveBuiltin = true; impl_->haveScripted = false;
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
struct MlVisemeMapper::Impl {};
MlVisemeMapper::MlVisemeMapper() : impl_(new Impl) { info_ = "LibTorch not compiled in"; }
MlVisemeMapper::~MlVisemeMapper() = default;
bool MlVisemeMapper::available() { return false; }
bool MlVisemeMapper::loaded() const { return false; }
bool MlVisemeMapper::load(const std::string&, std::string* error) { if (error) *error = "LibTorch not compiled in (FR_WITH_TORCH=OFF)"; return false; }
bool MlVisemeMapper::loadBuiltin(std::string* error) { if (error) *error = "LibTorch not compiled in (FR_WITH_TORCH=OFF)"; return false; }
bool MlVisemeMapper::save(const std::string&, std::string* error) const { if (error) *error = "LibTorch not compiled in"; return false; }
std::vector<VisemeFrame> MlVisemeMapper::map(const FeatureTrack& track) const { return VisemeMapper::map(track); }
#endif

} // namespace fr
