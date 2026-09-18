#include <catch2/catch_test_macros.hpp>
#include "audio/emotion_classifier.h"
#include "audio/wav_io.h"
#include "app/pipeline.h"
#include <filesystem>
#include <fstream>
using namespace fr;

TEST_CASE("emotion descriptor is fixed-size, finite and window-aware", "[emotion]") {
    AudioBuffer a = synthesizeTestSpeech(2.0);
    FeatureExtractor fx; FeatureTrack t = fx.extract(a);
    auto d = EmotionClassifier::describe(t);
    REQUIRE(int(d.size()) == EmotionClassifier::descriptorSize());
    REQUIRE(EmotionClassifier::descriptorNames().size() == d.size());
    for (float v : d) REQUIRE(std::isfinite(v));
    auto d2 = EmotionClassifier::describe(t, 0.5);
    REQUIRE(d2.size() == d.size());
    REQUIRE(d2[d2.size() - 3] < d[d.size() - 3]);   // logDur shorter for the window
}

TEST_CASE("crema codes and preset mapping", "[emotion]") {
    bool ok; REQUIRE(emotionFromCremaCode("HAP", &ok) == Emotion::Happy); REQUIRE(ok);
    emotionFromCremaCode("XXX", &ok); REQUIRE_FALSE(ok);
    std::array<float, 6> p{0.05f, 0.05f, 0.05f, 0.7f, 0.1f, 0.05f};
    EmotionResult r = EmotionClassifier::fromProbs(p);
    REQUIRE(r.top == Emotion::Happy); REQUIRE(r.presetName == "happy"); REQUIRE(r.presetAmount > 0.5f); REQUIRE(r.arousal > 0.5f);
    std::array<float, 6> n{0.1f, 0.1f, 0.1f, 0.1f, 0.5f, 0.1f};
    REQUIRE(EmotionClassifier::fromProbs(n).presetAmount == 0.0f);
}

TEST_CASE("shipped emotion model is trained on CREMA-D and separates emotions on held-out-style clips", "[emotion][model]") {
    EmotionClassifier clf; std::string err;
    REQUIRE(clf.loadDefault(FR_ASSET_DIR, &err));
    INFO(clf.modelInfo());
    REQUIRE(clf.modelInfo().find("CREMA-D") != std::string::npos);
    REQUIRE(clf.weights().layerSizes.front() == EmotionClassifier::descriptorSize());
    // weights must not be degenerate (trained, not hand-set): a spread of magnitudes in the first layer
    const auto& W0 = clf.weights().W[0]; float mn = 1e9f, mx = -1e9f; for (float w : W0) { mn = std::min(mn, w); mx = std::max(mx, w); }
    REQUIRE(mx - mn > 0.5f);
    // if the CREMA-D clips are present locally, check a handful of actors' clips classify above chance
    std::filesystem::path dir = std::filesystem::path(FR_ASSET_DIR).parent_path() / "data" / "crema_d";
    if (!std::filesystem::exists(dir)) { WARN("data/crema_d not present - skipping accuracy spot check"); return; }
    int n = 0, correct = 0;
    for (const char* f : {"1090_IEO_ANG_HI", "1090_IEO_HAP_HI", "1090_IEO_SAD_HI", "1090_IEO_NEU_XX", "1090_IEO_FEA_HI", "1090_IEO_DIS_HI",
                          "1091_IEO_ANG_HI", "1091_IEO_HAP_HI", "1091_IEO_SAD_HI", "1091_IEO_NEU_XX", "1091_IEO_FEA_HI", "1091_IEO_DIS_HI",
                          "1001_IEO_ANG_HI", "1001_IEO_HAP_HI", "1001_IEO_SAD_HI", "1001_IEO_NEU_XX", "1001_IEO_FEA_HI", "1001_IEO_DIS_HI"}) {
        AudioBuffer a; std::string e; if (!loadWav((dir / (std::string(f) + ".wav")).string(), a, &e)) continue;
        EmotionResult r = clf.classify(a); REQUIRE(r.valid); ++n;
        bool ok; Emotion truth = emotionFromCremaCode(std::string(f).substr(9, 3), &ok);
        correct += r.top == truth;
    }
    if (n) { INFO("spot check " << correct << "/" << n); REQUIRE(correct * 6 > n * 2); }   // > 2x chance
}

TEST_CASE("pipeline auto-emotion fills the performance layer", "[emotion]") {
    Pipeline p; std::string err; REQUIRE(p.loadModel("", &err)); p.buildDefaultRig(); REQUIRE(p.loadAudio(""));
    p.autoEmotion = true;
    if (!p.emotionClassifier()) { WARN("no emotion model"); return; }
    REQUIRE(p.generateAnimation());
    REQUIRE(p.lastEmotion.valid);
    REQUIRE(findExpressionPreset(p.lipSync.emotion) != nullptr);
}
