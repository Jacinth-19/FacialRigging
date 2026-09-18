#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "audio/speaker_profile.h"
#include "audio/ml_viseme_mapper.h"
#include "audio/wav_io.h"
#include "audio/phoneme_aligner.h"
#include "app/pipeline.h"
#include "app/project.h"
#include <filesystem>
#include <cmath>
using namespace fr; using Catch::Approx;

TEST_CASE("speaker calibration from synthetic speech yields a finite profile with stats", "[speaker]") {
    AudioBuffer a = synthesizeTestSpeech(7.0);
    CalibrationStats st;
    SpeakerProfile p = calibrateSpeaker(a, "synth", 3.0f, &st);
    REQUIRE(p.valid);
    REQUIRE(p.dims == MlVisemeMapper::kInputFeatures);
    REQUIRE(p.mean.size() == size_t(p.dims)); REQUIRE(p.stdev.size() == size_t(p.dims));
    for (size_t i = 0; i < p.mean.size(); ++i) { REQUIRE(std::isfinite(p.mean[i])); REQUIRE(p.stdev[i] > 0); }
    REQUIRE(p.pitchMedianHz > 50); REQUIRE(p.pitchMedianHz < 500);
    REQUIRE(p.loudnessRef < 0); REQUIRE(p.loudnessRef > -60);
    REQUIRE(st.seconds > 1.0f);
    REQUIRE(st.activeFrames == p.framesUsed);
    // too-short recordings are rejected with an explanation
    CalibrationStats s2; SpeakerProfile q = calibrateSpeaker(synthesizeTestSpeech(1.0), "short", 3.0f, &s2);
    REQUIRE_FALSE(q.valid); REQUIRE(s2.warning.find("need at least") != std::string::npos);
    // silence is rejected
    AudioBuffer sil; sil.sampleRate = 16000; sil.channels = 1; sil.samples.assign(16000, 0.0f);
    CalibrationStats s3; REQUIRE_FALSE(calibrateSpeaker(sil, "sil", 3.0f, &s3).valid);
}

TEST_CASE("adaptModel blends normalisation by strength and keeps bounded dims' scale", "[speaker]") {
    MlVisemeMapper m; std::string err; REQUIRE(m.loadDefault(FR_ASSET_DIR, &err));
    const VisemeMlpWeights& w = m.weights();
    SpeakerProfile p = calibrateSpeaker(synthesizeTestSpeech(6.0), "s");
    p.strength = 0.0f; VisemeMlpWeights a0 = adaptModel(w, p);
    REQUIRE(a0.mean == w.mean); REQUIRE(a0.invStd == w.invStd);
    p.strength = 1.0f; VisemeMlpWeights a1 = adaptModel(w, p);
    REQUIRE(a1.mean.size() == w.mean.size());
    float diff = 0; for (size_t i = 0; i < w.mean.size(); ++i) diff += std::fabs(a1.mean[i] - w.mean[i]);
    REQUIRE(diff > 0.01f);
    for (size_t i = 0; i < w.mean.size(); ++i) {
        int d = int(i % size_t(w.inputs));
        float g = 1.0f / w.invStd[i], n = 1.0f / a1.invStd[i];
        if (d == 13 || d == 14 || d == 16) REQUIRE(n == Approx(g)); else { REQUIRE(n >= 0.5f * g - 1e-5f); REQUIRE(n <= 2.0f * g + 1e-5f); }
    }
    p.strength = 0.5f; VisemeMlpWeights ah = adaptModel(w, p);
    REQUIRE(ah.mean[0] == Approx(0.5f * (w.mean[0] + a1.mean[0])).margin(1e-5f));
    // the adapted network still produces valid posteriors
    m.applySpeaker(p);
    FeatureExtractor fx; auto post = m.map(fx.extract(synthesizeTestSpeech(2.0)));
    REQUIRE_FALSE(post.empty());
    for (auto& f : post) { float s = 0; for (float v : f.weights) { REQUIRE(v >= 0); s += v; } REQUIRE(s == Approx(1.0f).margin(1e-3f)); }
    REQUIRE(m.modelInfo().find("speaker") != std::string::npos);
}

TEST_CASE("speaker profile JSON round trip", "[speaker]") {
    SpeakerProfile p = calibrateSpeaker(synthesizeTestSpeech(6.0), "round");
    p.strength = 0.42f; p.source = "test"; p.lastW = {1, 2, 3}; p.lastB = {0.5f}; p.fineTuneInfo = "x";
    std::string js = p.toJson();
    SpeakerProfile q; REQUIRE(q.fromJson(js));
    REQUIRE(q.valid); REQUIRE(q.name == "round"); REQUIRE(q.dims == p.dims);
    REQUIRE(q.mean.size() == p.mean.size());
    for (size_t i = 0; i < p.mean.size(); ++i) { REQUIRE(q.mean[i] == Approx(p.mean[i])); REQUIRE(q.stdev[i] == Approx(p.stdev[i])); }
    REQUIRE(q.pitchMedianHz == Approx(p.pitchMedianHz)); REQUIRE(q.strength == Approx(0.42f)); REQUIRE(q.lastW == p.lastW); REQUIRE(q.lastB == p.lastB); REQUIRE(q.fineTuneInfo == "x");
    auto path = (std::filesystem::temp_directory_path() / "fr_speaker.json").string();
    std::string err; REQUIRE(p.save(path, &err)); SpeakerProfile r; REQUIRE(r.load(path, &err)); REQUIRE(r.name == "round");
    REQUIRE_FALSE(SpeakerProfile{}.fromJson("{\"valid\":true}"));   // missing stats -> invalid
}

TEST_CASE("last-layer fine-tune improves agreement with the given labels and is anchored", "[speaker]") {
    MlVisemeMapper m; std::string err; REQUIRE(m.loadDefault(FR_ASSET_DIR, &err));
    AudioBuffer a = synthesizeTestSpeech(6.0);
    FeatureExtractor fx; FeatureTrack tr = fx.extract(a);
    SpeakerProfile p = calibrateSpeaker(a, "ft");
    // labels: a systematic relabelling of the mapper's own argmax (every AA frame -> OH) - a consistent shift the last layer can learn
    auto post = m.map(tr);
    std::vector<int> labels(tr.frames.size(), -1);
    for (size_t t = 0; t < tr.frames.size(); ++t) { Viseme v = post[t].dominant(); labels[t] = int(v == Viseme::AA ? Viseme::OH : v); }
    float before = 0, after = 0;
    REQUIRE(fineTuneLastLayer(m.weights(), p, tr, labels, 60, 2e-3f, 1e-2f, &before, &after));
    INFO("before " << before << " after " << after);
    REQUIRE(after > before);
    REQUIRE(p.lastW.size() == m.weights().W.back().size());
    float drift = 0; for (size_t i = 0; i < p.lastW.size(); ++i) drift = std::max(drift, std::fabs(p.lastW[i] - m.weights().W.back()[i]));
    REQUIRE(drift > 0); REQUIRE(drift < 1.0f);
    REQUIRE_FALSE(p.fineTuneInfo.empty());
    std::vector<int> few(tr.frames.size(), -1); few[0] = 0;
    REQUIRE_FALSE(fineTuneLastLayer(m.weights(), p, tr, few, 5));   // not enough labelled frames
}

TEST_CASE("pipeline calibration with transcript and project round trip", "[speaker][project]") {
    Pipeline p; std::string err;
    REQUIRE(p.loadModel(std::string(FR_ASSET_DIR) + "/models/ict_face/ict_face.obj", &err));
    p.buildDefaultRig();
    REQUIRE(p.calibrateFromAudio(synthesizeTestSpeech(7.0), calibrationSentence(), "mic", true));
    REQUIRE(p.speaker.valid); REQUIRE(p.speaker.name == "mic");
    REQUIRE(p.lastCalibration.seconds > 1.0f);
    p.mapperKind = Pipeline::MapperKind::Ml;
    auto mapper = p.makeMapper();
    REQUIRE(mapper);
    REQUIRE(dynamic_cast<MlVisemeMapper*>(mapper.get())->modelInfo().find("speaker 'mic'") != std::string::npos);
    auto tmp = std::filesystem::temp_directory_path() / "fr_speaker_proj"; std::filesystem::create_directories(tmp);
    std::string path = (tmp / ("s" + std::string(kProjectExtension))).string();
    ProjectSaveOptions o; o.includeClip = false;
    REQUIRE(saveProject(path, p, o, &err));
    Pipeline q; REQUIRE(loadProject(path, q, &err));
    REQUIRE(q.speaker.valid); REQUIRE(q.speaker.name == "mic");
    REQUIRE(q.speaker.pitchMedianHz == Approx(p.speaker.pitchMedianHz));
    REQUIRE(q.speaker.mean.size() == p.speaker.mean.size());
    REQUIRE(q.speaker.lastW.size() == p.speaker.lastW.size());
    REQUIRE(calibrationSentence()[0] != '\0');
}
