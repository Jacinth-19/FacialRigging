#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "audio/ml_viseme_mapper.h"
#include "audio/live_capture.h"
#include "app/pipeline.h"
using namespace fr;
using Catch::Approx;

TEST_CASE("ML mapper feature vector layout") {
    AudioFrameFeatures f; f.mfcc.assign(13, 20.0f); f.loudness = 0.5f; f.voicing = 0.9f; f.pitchHz = 200; f.spectralCentroid = 2000;
    auto v = MlVisemeMapper::featureVector(f, 100.0f);
    REQUIRE(v.size() == size_t(MlVisemeMapper::kInputFeatures));
    CHECK(v[0] == Approx(1.0f)); CHECK(v[13] == Approx(0.5f)); CHECK(v[15] == Approx(1.0f)); CHECK(v[16] == Approx(0.5f));
}

TEST_CASE("ML mapper falls back to rule-based when no model is loaded") {
    MlVisemeMapper m;
    CHECK_FALSE(m.loaded());
    AudioBuffer a = synthesizeTestSpeech(1.0, 16000);
    FeatureTrack t = FeatureExtractor().extract(a);
    auto ml = m.map(t), rules = VisemeMapper().map(t);
    REQUIRE(ml.size() == rules.size());
    for (size_t i = 0; i < ml.size(); ++i) for (size_t k = 0; k < ml[i].weights.size(); ++k) CHECK(ml[i].weights[k] == Approx(rules[i].weights[k]));
}

#if FR_HAVE_TORCH
TEST_CASE("built-in LibTorch MLP produces normalised, speech-reactive viseme weights") {
    MlVisemeMapper m; std::string err;
    REQUIRE(m.loadBuiltin(&err));
    REQUIRE(m.loaded());
    AudioBuffer a = synthesizeTestSpeech(1.5, 16000);
    FeatureTrack t = FeatureExtractor().extract(a);
    auto vis = m.map(t);
    REQUIRE(vis.size() == t.frames.size());
    bool anyMouth = false;
    for (auto& v : vis) { float s = 0; for (float w : v.weights) s += w; CHECK(s == Approx(1.0f).margin(1e-3)); if (v.dominant() != Viseme::Silence) anyMouth = true; }
    CHECK(anyMouth);
    CHECK(vis.front().dominant() == Viseme::Silence);
    // pipeline integration
    Pipeline p; REQUIRE(p.loadModel("")); p.buildDefaultRig(); REQUIRE(p.loadAudio(""));
    p.mapperKind = Pipeline::MapperKind::Ml;
    REQUIRE(p.generateAnimation());
    bool noted = false; for (auto& l : p.log) if (l.find("ML viseme mapper") != std::string::npos) noted = true;
    CHECK(noted);
    REQUIRE(m.save("/tmp/fr_builtin_mlp.pt", &err));
}
#endif

TEST_CASE("live capture API is safe without a device") {
    LiveCapture lc;
    CHECK_FALSE(lc.running());
    CHECK_FALSE(lc.poll().valid);
    std::string err;
    auto devs = LiveCapture::listInputDevices(&err);
    if (devs.empty()) CHECK_FALSE(err.empty());
    // feed synthetic samples directly (as the audio thread would) and analyse
    // - only meaningful once start() sized the ring; emulate by starting if a device exists.
    if (!devs.empty() && lc.start(devs.front().index, 16000, &err)) {
        CHECK(lc.running()); lc.stop(); CHECK_FALSE(lc.running());
    }
}
