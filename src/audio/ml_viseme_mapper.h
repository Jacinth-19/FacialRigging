#pragma once
#include "audio/viseme_mapper.h"
#include <memory>
#include <string>

namespace fr {

/// Neural viseme mapper backed by LibTorch (TorchScript). The model takes a
/// [T, F] float tensor of per-frame features (13 MFCC + loudness + voicing + log-pitch +
/// normalised centroid = 17) and returns [T, 9] viseme logits or probabilities.
/// Falls back gracefully (available()==false) when LibTorch is not compiled in.
/// Plain-C++ MLP weights (no LibTorch needed to *run*). Layout: x[17*context] -> h1 -> h2 -> 9.
/// Saved by the trainer as ".frvm" (FacialRigging Viseme Model) and loadable in every build.
struct VisemeMlpWeights {
    int inputs = 0, context = 1;                 ///< context = number of frames stacked (odd, centred)
    std::vector<int> layerSizes;                 ///< e.g. {85, 64, 64, 9}
    std::vector<std::vector<float>> W, b;        ///< row-major [out][in], [out]
    std::vector<float> mean, invStd;             ///< input standardisation (size inputs*context)
    std::string info;                            ///< training provenance
    bool valid() const { return !layerSizes.empty() && W.size() == layerSizes.size() - 1; }
    std::vector<float> forward(const std::vector<float>& x) const;   ///< returns logits
    bool save(const std::string& path, std::string* error = nullptr) const;
    bool load(const std::string& path, std::string* error = nullptr);
};

class MlVisemeMapper : public VisemeMapper {
public:
    MlVisemeMapper();
    ~MlVisemeMapper() override;
    static bool available();                          ///< LibTorch compiled in?
    static constexpr int kInputFeatures = 17;
    static constexpr int kOutputs = int(Viseme::Count);

    /// Loads a model: ".frvm" (trained weights, works in every build) or a TorchScript ".pt"
    /// (LibTorch builds only). Returns false with an error when not possible.
    bool load(const std::string& path, std::string* error = nullptr);
    /// Loads the model shipped in assets/models/viseme_mlp.frvm (trained on TIMIT phone alignments).
    bool loadDefault(const std::string& assetDir, std::string* error = nullptr);
    /// Feature matrix with temporal context stacking, as fed to the network ([T][inputs*context]).
    static std::vector<std::vector<float>> stackedFeatures(const FeatureTrack& track, int context);
    /// Builds an in-memory model with a fixed, hand-initialised linear layer that mimics the
    /// rule-based mapper – useful as a smoke test and as a starting point for fine-tuning.
    bool loadBuiltin(std::string* error = nullptr);
    /// Saves the currently loaded module as TorchScript (only for models created in-process).
    bool save(const std::string& path, std::string* error = nullptr) const;
    bool loaded() const;
    std::string modelInfo() const { return info_; }
    const VisemeMlpWeights& weights() const { return weights_; }
    /// Replaces the loaded portable weights with a speaker-adapted copy (see speaker_profile.h).
    void applySpeaker(const struct SpeakerProfile& profile);

    std::vector<VisemeFrame> map(const FeatureTrack& track) const override;

    /// Feature vector fed to the network for one frame (exposed for training-data export).
    static std::vector<float> featureVector(const AudioFrameFeatures& f, float medianPitchHz);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string info_;
    VisemeMlpWeights weights_;      ///< portable trained model (empty when not loaded)
    std::vector<VisemeFrame> mapWithWeights(const FeatureTrack& track) const;
};

} // namespace fr
