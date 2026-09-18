// Utterance-level emotion recognition from audio (the "second head"): pooled acoustic statistics
// of a clip -> small MLP -> 6 CREMA-D classes (anger, disgust, fear, happy, neutral, sad). The
// result is mapped onto the app's expression presets so the performance layer can pick the
// emotion and its amount automatically. Training: tools/train_emotion.cpp on CREMA-D.
#pragma once
#include "audio/features.h"
#include "audio/ml_viseme_mapper.h"   // VisemeMlpWeights: portable MLP container / .frvm format
#include <array>
#include <string>
#include <vector>

namespace fr {

enum class Emotion { Anger = 0, Disgust, Fear, Happy, Neutral, Sad, Count };
const char* emotionName(Emotion e);                 ///< "anger" ...
const char* emotionPresetName(Emotion e);           ///< matching ExpressionPreset name ("angry", "disgusted", ...)
Emotion emotionFromCremaCode(const std::string& code, bool* ok = nullptr);   ///< "ANG" -> Anger

struct EmotionResult {
    bool valid = false;
    std::array<float, size_t(Emotion::Count)> probs{};
    Emotion top = Emotion::Neutral;
    float confidence = 0.0f;                        ///< top probability
    float arousal = 0.0f;                           ///< 0..1 derived from probs (anger/fear/happy high, sad/neutral low)
    /// Suggested preset for the performance layer: preset name + amount (0 for neutral / low confidence).
    std::string presetName = "neutral"; float presetAmount = 0.0f;
    std::string summary() const;                    ///< "happy 62% (neutral 21%, anger 9%)"
};

class EmotionClassifier {
public:
    static constexpr int kClasses = int(Emotion::Count);
    /// Pooled utterance descriptor (fixed length, see kDescriptorSize) computed by the same feature
    /// extractor the rest of the app uses. `window` limits pooling to the last N seconds (live mode).
    static std::vector<float> describe(const FeatureTrack& track, double windowSeconds = 0.0);
    static int descriptorSize();
    static std::vector<std::string> descriptorNames();

    bool load(const std::string& path, std::string* error = nullptr);
    bool loadDefault(const std::string& assetDir, std::string* error = nullptr);   ///< assets/models/emotion_mlp.frvm
    bool loaded() const { return weights_.valid(); }
    std::string modelInfo() const { return weights_.info; }
    const VisemeMlpWeights& weights() const { return weights_; }

    EmotionResult classify(const FeatureTrack& track, double windowSeconds = 0.0) const;
    EmotionResult classify(const AudioBuffer& audio) const;
    static EmotionResult fromProbs(const std::array<float, size_t(Emotion::Count)>& p);

private:
    VisemeMlpWeights weights_;
};

} // namespace fr
