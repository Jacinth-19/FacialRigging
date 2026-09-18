#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "app/pipeline.h"
#include "app/project.h"
#include "rig/rig_tools.h"
#include <filesystem>
#include <fstream>
using namespace fr; using Catch::Approx;

TEST_CASE("project file restores model, orientation, handles, painted weights, user shapes, correctives, settings and clip") {
    Pipeline p; std::string err;
    REQUIRE(p.loadModel(std::string(FR_ASSET_DIR) + "/models/ict_face/ict_face.obj", &err));
    p.transformModel(glm::mat3(glm::scale(glm::mat4(1), glm::vec3(-1, 1, 1))));   // mirror fix
    p.buildDefaultRig();
    Rig& r = p.rig;
    // edits
    int cp = r.addControlPoint(glm::vec3(0.1f, 0.1f, 0.3f), "Custom1"); r.bindFreeForm(cp, 0.12f); r.moveControlPoint(cp, glm::vec3(0, 0, 0.02f));
    int idx = bakePoseAsBlendShape(r, BakeShapeOptions{"Bulge"}); REQUIRE(idx >= 0);
    r.setBlendWeight(shapes::JawOpen, 1.0f); r.setBlendWeight(shapes::MouthPucker, 1.0f);
    r.moveControlPoint(cp, glm::vec3(0, 0.01f, 0.0f));
    REQUIRE(bakeCorrectiveShape(r, shapes::JawOpen, shapes::MouthPucker, "JawOpen_MouthPucker") >= 0);
    r.resetPose();
    int jaw = r.skeleton.find(Rig::kJawBone);
    WeightBrush b; b.bone = jaw; b.radius = 0.08f; b.strength = 1.0f; b.mode = WeightBrushMode::Replace; b.frontFacingOnly = false;
    glm::vec3 fore(0, 0.35f, 0.3f);
    uint32_t vi = 0; float best = 1e9f; for (size_t i = 0; i < r.mesh.vertexCount(); ++i) { float d = glm::length(r.mesh.positions[i] - fore); if (d < best) { best = d; vi = uint32_t(i); } }
    paintWeights(r, b, r.mesh.positions[vi], glm::vec3(0)); p.skinEdited = true;
    float paintedW = boneWeightOf(r.skin[vi], jaw); REQUIRE(paintedW > 0.9f);
    r.setBlendWeight("mouthSmile_L", 0.4f);
    p.lipSync.intensity = 1.3f; p.lipSync.emotion = "happy"; p.lipSync.emotionAmount = 0.5f; p.transcript = "hello world";
    p.mapperKind = Pipeline::MapperKind::Ml;
    REQUIRE(p.loadAudio("", nullptr)); REQUIRE(p.generateAnimation());
    const float dur = p.clip.duration;
    auto tmp = std::filesystem::temp_directory_path() / "fr_project"; std::filesystem::create_directories(tmp);
    std::string path = (tmp / ("session" + std::string(kProjectExtension))).string();
    REQUIRE(saveProject(path, p, ProjectSaveOptions{}, &err));
    REQUIRE(std::filesystem::file_size(path) > 1000);

    Pipeline q;
    REQUIRE(loadProject(path, q, &err));
    INFO(err);
    Rig& s = q.rig;
    CHECK(s.mesh.vertexCount() == r.mesh.vertexCount());
    // mirror fix replayed: compare a far-left vertex position
    CHECK(glm::length(s.mesh.positions[100] - r.mesh.positions[100]) < 1e-5f);
    CHECK(q.userRotation == p.userRotation);
    CHECK(s.controlPoints.size() == r.controlPoints.size());
    int cq = -1; for (size_t i = 0; i < s.controlPoints.size(); ++i) if (s.controlPoints[i].name == "Custom1") cq = int(i);
    REQUIRE(cq >= 0); CHECK(s.controlPoints[size_t(cq)].binding == BindingType::FreeForm); CHECK(s.controlPoints[size_t(cq)].radius == Approx(0.12f));
    CHECK(s.findBlendShape("Bulge") >= 0);
    CHECK(s.blendShapes[size_t(s.findBlendShape("Bulge"))].indices.size() == r.blendShapes[size_t(idx)].indices.size());
    REQUIRE(s.combinations.size() == 1); CHECK(s.combinations[0].driverA == shapes::JawOpen);
    CHECK(s.findBlendShape("JawOpen_MouthPucker") >= 0);
    CHECK(boneWeightOf(s.skin[vi], s.skeleton.find(Rig::kJawBone)) == Approx(paintedW).margin(1e-4f));
    CHECK(s.blendWeight("mouthSmile_L") == Approx(0.4f));
    CHECK(q.lipSync.intensity == Approx(1.3f)); CHECK(q.lipSync.emotion == "happy"); CHECK(q.transcript == "hello world");
    CHECK(q.mapperKind == Pipeline::MapperKind::Ml);
    CHECK(q.clip.duration == Approx(dur)); CHECK(q.clip.blendCurves.size() == p.clip.blendCurves.size());
    CHECK(q.audio.frames() == p.audio.frames());
    // relative model path was written
    { std::ifstream f(path); std::string all((std::istreambuf_iterator<char>(f)), {}); CHECK(all.find("\"model\":{\"path\":\"") != std::string::npos); CHECK(all.find("ict_face.obj") != std::string::npos); }
    std::filesystem::remove_all(tmp);
}
