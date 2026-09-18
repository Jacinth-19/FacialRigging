#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "app/pipeline.h"
#include "core/scene_import.h"
#include "export/exporter.h"
#include <filesystem>
using namespace fr; using Catch::Approx;

#if FR_HAVE_ASSIMP
namespace {
Pipeline ictPipeline() { Pipeline p; std::string err; REQUIRE(p.loadModel(std::string(FR_ASSET_DIR) + "/models/ict_face/ict_face.obj", &err)); p.buildDefaultRig(); return p; }
}

TEST_CASE("import a rigged character back from our own glTF and FBX exports") {
    Pipeline p = ictPipeline();
    REQUIRE(p.loadAudio("", nullptr)); REQUIRE(p.generateAnimation());
    const size_t nV = p.rig.mesh.vertexCount(), nB = p.rig.skeleton.bones.size();
    for (const char* ext : {"glb", "fbx"}) {
        std::string path = (std::filesystem::temp_directory_path() / (std::string("fr_roundtrip.") + ext)).string();
        std::string err; ExportOptions eo;
        auto ex = makeExporterForPath(path); REQUIRE(ex);
        REQUIRE(ex->exportScene(p.rig, {p.clip}, path, eo, &err));
        SceneImportResult in; SceneImportOptions io;
        INFO(ext << ": " << err);
        REQUIRE(importScene(path, io, in, &err));
        INFO(in.log);
        CHECK(in.mesh.triangleCount() == p.rig.mesh.triangleCount());
        CHECK(in.mesh.vertexCount() >= nV * 9 / 10);
        CHECK(in.skeleton.bones.size() == nB);
        CHECK(in.skeleton.find("Jaw") >= 0);
        REQUIRE(in.hasSkin());
        // jaw weights survived: some vertices are fully on the Jaw
        int jaw = in.skeleton.find("Jaw"); size_t onJaw = 0;
        for (auto& inf : in.skin) for (int k = 0; k < 4; ++k) if (inf.bones[k] == jaw && inf.weights[k] > 0.9f) { ++onJaw; break; }
        CHECK(onJaw > 100);
        CHECK(in.blendShapes.size() >= 8);
        CHECK(in.arkitShapeCount >= 0);
        bool hasJawOpen = false; for (auto& b : in.blendShapes) hasJawOpen |= b.name == shapes::JawOpen; CHECK(hasJawOpen);
        // the exported clip came back
        REQUIRE_FALSE(in.clips.empty());
        CHECK(in.clips[0].duration == Approx(p.clip.duration).margin(0.1f));
        CHECK_FALSE(in.clips[0].blendCurves.empty());
        // install into a fresh rig and drive it with lip-sync
        Pipeline q; std::string e2;
        REQUIRE(q.loadModel(path, &e2)); q.buildDefaultRig();
        INFO(e2);
        CHECK(q.rig.skeleton.bones.size() == nB);
        CHECK(q.rig.hasSkin());
        CHECK(q.rig.findBlendShape(shapes::JawOpen) >= 0);
        REQUIRE(q.loadAudio("", nullptr)); REQUIRE(q.generateAnimation());
        CHECK(q.clip.duration > 2.0f);
        // the imported rig deforms when JawOpen is set
        q.rig.resetPose(); auto rest = q.rig.evaluate(); q.rig.setBlendWeight(shapes::JawOpen, 1.0f); auto open = q.rig.evaluate();
        float moved = 0; for (size_t i = 0; i < rest.size(); ++i) moved = std::max(moved, glm::length(open[i] - rest[i]));
        CHECK(moved > 0.01f);
        std::filesystem::remove(path);
    }
}

TEST_CASE("imported glTF morph weights animation maps onto shape curves") {
    Pipeline p = ictPipeline();
    p.rig.setBlendWeight(shapes::MouthSmile, 0.7f);
    AnimationClip clip = snapshotPose(p.rig, "Pose"); clip.duration = 1.0f;
    for (auto& c : clip.blendCurves) if (c.times.size() == 1) c.addKey(1.0f, c.values[0]);
    std::string path = (std::filesystem::temp_directory_path() / "fr_pose.glb").string(); std::string err;
    REQUIRE(makeExporterForPath(path)->exportScene(p.rig, {clip}, path, ExportOptions{}, &err));
    SceneImportResult in; REQUIRE(importScene(path, SceneImportOptions{}, in, &err));
    REQUIRE_FALSE(in.clips.empty());
    const Curve<float>* smile = in.clips[0].findBlendCurve(shapes::MouthSmile);
    REQUIRE(smile); CHECK(smile->sample(0.5f) == Approx(0.7f).margin(0.02f));
    std::filesystem::remove(path);
}
#endif
