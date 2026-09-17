#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <chrono>
#include <thread>
#include <array>
#include <algorithm>
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

TEST_CASE("live capture pipeline runs end-to-end on the built-in test signal") {
    LiveCapture lc; std::string err;
    REQUIRE(lc.start(LiveCapture::kTestSignalDevice, 16000, &err));
    REQUIRE(lc.running());
    int valid = 0, speaking = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 1.5) {
        auto f = lc.poll();
        if (f.valid) { ++valid; if (f.viseme.dominant() != Viseme::Silence) ++speaking; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    lc.stop();
    CHECK_FALSE(lc.running());
    CHECK(valid > 20);          // ~100 hops/s at 10 ms hop
    CHECK(speaking > 0);        // synthetic speech opens the mouth
    CHECK(lc.recent(0.5).size() == 8000);
    CHECK(lc.inputLevel() > 0.01f);
}

TEST_CASE("shipped viseme model is trained (not hand-set) and drives speech-reactive visemes") {
    MlVisemeMapper m; std::string err;
    REQUIRE(m.loadDefault(FR_ASSET_DIR, &err));
    CHECK(m.modelInfo().find("TIMIT") != std::string::npos);      // provenance recorded by the trainer
    CHECK(m.modelInfo().find("held-out acc") != std::string::npos);
    AudioBuffer a = synthesizeTestSpeech(2.0, 16000);
    FeatureTrack t = FeatureExtractor().extract(a);
    auto frames = m.map(t);
    REQUIRE(frames.size() == t.frames.size());
    int nonSilent = 0; std::array<bool, size_t(Viseme::Count)> seen{};
    for (auto& f : frames) {
        float sum = 0; for (float w : f.weights) { CHECK(w >= 0.0f); sum += w; }
        CHECK(sum == Approx(1.0f).margin(1e-3f));
        auto k = size_t(std::max_element(f.weights.begin(), f.weights.end()) - f.weights.begin());
        seen[k] = true; if (k != 0) ++nonSilent;
    }
    CHECK(nonSilent > int(frames.size()) / 4);
    int distinct = 0; for (bool b : seen) distinct += b;
    CHECK(distinct >= 3);
}

TEST_CASE("FRVM weights round-trip through save/load") {
    VisemeMlpWeights w; w.inputs = 17; w.context = 1; w.layerSizes = {17, 4, 9};
    w.W = {std::vector<float>(17 * 4, 0.1f), std::vector<float>(4 * 9, -0.2f)}; w.b = {std::vector<float>(4, 0.5f), std::vector<float>(9, 0.0f)};
    w.mean.assign(17, 0.0f); w.invStd.assign(17, 1.0f); w.info = "unit";
    REQUIRE(w.save("/tmp/fr_unit.frvm"));
    VisemeMlpWeights r; REQUIRE(r.load("/tmp/fr_unit.frvm"));
    CHECK(r.info == "unit"); CHECK(r.layerSizes == w.layerSizes);
    std::vector<float> x(17, 1.0f);
    auto a = w.forward(x), b = r.forward(x);
    REQUIRE(a.size() == 9); for (size_t i = 0; i < 9; ++i) CHECK(a[i] == Approx(b[i]));
    CHECK(a[0] == Approx(-0.2f * 4 * (0.1f * 17 + 0.5f)));
}
