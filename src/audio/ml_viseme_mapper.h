#pragma once
#include "audio/viseme_mapper.h"
#include <memory>
#include <string>

namespace fr {

/// Neural viseme mapper backed by LibTorch (TorchScript). The model takes a
/// [T, F] float tensor of per-frame features (13 MFCC + loudness + voicing + log-pitch +
/// normalised centroid = 17) and returns [T, 9] viseme logits or probabilities.
/// Falls back gracefully (available()==false) when LibTorch is not compiled in.
class MlVisemeMapper : public VisemeMapper {
public:
    MlVisemeMapper();
    ~MlVisemeMapper() override;
    static bool available();                          ///< LibTorch compiled in?
    static constexpr int kInputFeatures = 17;
    static constexpr int kOutputs = int(Viseme::Count);

    /// Loads a TorchScript module (.pt). Returns false with an error when not possible.
    bool load(const std::string& path, std::string* error = nullptr);
    /// Builds an in-memory model with a fixed, hand-initialised linear layer that mimics the
    /// rule-based mapper – useful as a smoke test and as a starting point for fine-tuning.
    bool loadBuiltin(std::string* error = nullptr);
    /// Saves the currently loaded module as TorchScript (only for models created in-process).
    bool save(const std::string& path, std::string* error = nullptr) const;
    bool loaded() const;
    std::string modelInfo() const { return info_; }

    std::vector<VisemeFrame> map(const FeatureTrack& track) const override;

    /// Feature vector fed to the network for one frame (exposed for training-data export).
    static std::vector<float> featureVector(const AudioFrameFeatures& f, float medianPitchHz);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string info_;
};

} // namespace fr
