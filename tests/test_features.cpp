#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "app/pipeline.h"
#include "anim/lipsync_generator.h"
#include "audio/wav_io.h"
#include "export/exporter.h"
#include "rig/blendshape_io.h"
#include <glm/gtx/quaternion.hpp>
using namespace fr;
using Catch::Approx;

TEST_CASE("ICT head gets eye bones; look-at rotates only the eyeballs") {
    Pipeline p; std::string err;
    REQUIRE(p.loadModel(std::string(FR_ASSET_DIR) + "/models/ict_face/ict_face.obj", &err));
    p.buildDefaultRig();
    REQUIRE(p.rig.hasEyeBones());
    REQUIRE(p.rig.skeleton.bones.size() == 5); // Head, Jaw, EyeL, EyeR, Tongue
    int eL = p.rig.skeleton.find(Rig::kEyeLBone), eR = p.rig.skeleton.find(Rig::kEyeRBone);
    auto parts = p.rig.detectParts();
    for (uint32_t v : p.rig.mesh.partVertices(parts.eyeL)) CHECK(p.rig.skin[v].bones[0] == eL);
    for (uint32_t v : p.rig.mesh.partVertices(parts.eyeR)) CHECK(p.rig.skin[v].bones[0] == eR);
    // pivot at eyeball centre
    glm::vec3 lo, hi; p.rig.mesh.partBounds(parts.eyeL, lo, hi);
    auto W = p.rig.skeleton.bindWorldMatrices();
    CHECK(glm::length(glm::vec3(W[size_t(eL)][3]) - 0.5f * (lo + hi)) < 1e-4f);
    // looking far to the model's +x yaws both eyes positively; face vertices don't move
    glm::vec3 c = 0.5f * (lo + hi);
    p.rig.lookAt(c + glm::vec3(100.0f, 0, 100.0f));
    float yawL = glm::degrees(glm::yaw(p.rig.skeleton.bones[size_t(eL)].poseRotation));
    CHECK(yawL == Approx(35.0f).margin(0.5f)); // clamped
    p.rig.lookAt(c + glm::vec3(0, 0, 100.0f));
    CHECK(glm::length(glm::vec3(glm::eulerAngles(p.rig.skeleton.bones[size_t(eL)].poseRotation))) < 1e-3f);
    p.rig.setGaze(10.0f, -5.0f);
    CHECK(glm::degrees(glm::yaw(p.rig.skeleton.bones[size_t(eR)].poseRotation)) == Approx(10.0f).margin(0.1f));
    p.rig.setGaze(0, 0);
    // eye bone poses never move the face part
    auto rest = p.rig.mesh.positions;
    p.rig.setGaze(20.0f, 10.0f);
    std::vector<glm::vec3> out = p.rig.evaluate();
    for (uint32_t v : p.rig.mesh.partVertices(parts.face)) REQUIRE(glm::length(out[v] - rest[v]) < 1e-5f);
    bool moved = false; for (uint32_t v : p.rig.mesh.partVertices(parts.eyeL)) if (glm::length(out[v] - rest[v]) > 1e-4f) moved = true;
    CHECK(moved);
}

TEST_CASE("expression presets and emotion variations drive the new shapes") {
    REQUIRE(findExpressionPreset("happy")); REQUIRE(findExpressionPreset("angry")); CHECK(findExpressionPreset("bored") == nullptr);
    Rig rig; rig.setMesh(makeProceduralHead(16, 24)); rig.buildDefaultFaceRig();
    CHECK(rig.findBlendShape(shapes::MouthFrown) >= 0); CHECK(rig.findBlendShape(shapes::BrowDown) >= 0); CHECK(rig.findBlendShape(shapes::EyeWide) >= 0);
    AudioBuffer a = synthesizeTestSpeech(1.0, 16000);
    LipSyncGenerator gen; gen.settings.emotion = "angry"; gen.settings.emotionAmount = 1.0f;
    AnimationClip clip = gen.generate(a, rig);
    auto* bd = clip.findBlendCurve(shapes::BrowDown); REQUIRE(bd);
    for (float v : bd->values) CHECK(v == Approx(0.8f).margin(1e-4f));
    LipSyncGenerator neutral; AnimationClip base = neutral.generate(a, rig);
    CHECK(base.findBlendCurve(shapes::BrowDown)->values[10] == Approx(0.0f));
    // variation text
    AnimationClip v = base; parseVariation("surprised").apply(v);
    CHECK(v.findBlendCurve(shapes::EyeWide)->values[5] == Approx(0.8f * 0.8f).margin(1e-4f));
    AnimationClip v2 = base; parseVariation("sad=0.5").apply(v2);
    CHECK(v2.findBlendCurve(shapes::MouthFrown)->values[5] == Approx(0.55f * 0.5f).margin(1e-4f));
    // head motion can be disabled; when enabled it stays subtle
    LipSyncGenerator still; still.settings.headMotion = 0.0f;
    CHECK(still.generate(a, rig).boneRotations.size() == 1);
    for (const auto& c : base.boneRotations) if (c.target == Rig::kHeadBone) for (const auto& q : c.values) CHECK(glm::degrees(glm::angle(q)) < 12.0f);
}

TEST_CASE("clip JSON exporter writes ARKit aliases and round-trips") {
    Rig rig; rig.setMesh(makeProceduralHead(16, 24)); rig.buildDefaultFaceRig();
    AudioBuffer a = synthesizeTestSpeech(0.8, 16000);
    AnimationClip clip = LipSyncGenerator{}.generate(a, rig); clip.name = "Test \"quoted\"";
    std::string note; auto ex = makeExporterForPath("/tmp/fr_clip.json", &note);
    CHECK(ex->formatName() == "Clip JSON");
    std::string err; REQUIRE(ex->exportScene(rig, {clip}, "/tmp/fr_clip.json", ExportOptions{}, &err));
    std::vector<AnimationClip> in; REQUIRE(loadClipsJson("/tmp/fr_clip.json", in, &err));
    REQUIRE(in.size() == 1);
    CHECK(in[0].name == clip.name); CHECK(in[0].duration == Approx(clip.duration).margin(1e-3f)); CHECK(in[0].frameRate == Approx(clip.frameRate));
    REQUIRE(in[0].blendCurves.size() == clip.blendCurves.size());
    auto* j = in[0].findBlendCurve(shapes::JawOpen); REQUIRE(j);
    REQUIRE(j->times.size() == clip.findBlendCurve(shapes::JawOpen)->times.size());
    for (size_t i = 0; i < j->values.size(); ++i) CHECK(j->values[i] == Approx(clip.findBlendCurve(shapes::JawOpen)->values[i]).margin(1e-4f));
    REQUIRE(in[0].boneRotations.size() == clip.boneRotations.size());
    CHECK(in[0].boneRotations[0].target == "Jaw");
    CHECK(glm::dot(in[0].boneRotations[0].values.back(), clip.boneRotations[0].values.back()) == Approx(1.0f).margin(1e-4f));
    auto names = arkitNamesForShape(shapes::JawOpen, rig); REQUIRE(names.size() == 1); CHECK(names[0] == "jawOpen");
    CHECK(arkitNamesForShape(shapes::MouthSmile, rig).size() == 3);
    CHECK_FALSE(loadClipsJson("/nonexistent/x.json", in, &err));
}
