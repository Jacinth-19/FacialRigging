#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "anim/animation_clip.h"
#include "anim/lipsync_generator.h"
#include "app/pipeline.h"
#include "export/exporter.h"
#include <cstring>
#include <fstream>
using namespace fr;
using Catch::Approx;

TEST_CASE("curve sampling interpolates and clamps") {
    Curve<float> c; c.addKey(0, 0); c.addKey(1, 1); c.addKey(2, 0);
    CHECK(c.sample(-1) == 0); CHECK(c.sample(0.5f) == Approx(0.5f)); CHECK(c.sample(1.5f) == Approx(0.5f)); CHECK(c.sample(9) == 0);
    Curve<glm::quat> q; q.addKey(0, glm::quat(1, 0, 0, 0)); q.addKey(1, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)));
    CHECK(glm::degrees(glm::angle(q.sample(0.5f))) == Approx(45.0f).margin(0.1));
}

TEST_CASE("lipsync generator bakes a clip aligned to the audio") {
    Rig rig; rig.setMesh(makeProceduralHead(16, 24)); rig.buildDefaultFaceRig();
    AudioBuffer a = synthesizeTestSpeech(1.5, 16000);
    LipSyncGenerator gen;
    AnimationClip clip = gen.generate(a, rig);
    CHECK(clip.duration == Approx(1.5f).margin(1e-3));
    CHECK(clip.frameCount() == 46);
    REQUIRE(clip.blendCurves.size() == rig.blendShapes.size());
    auto* jaw = clip.findBlendCurve(shapes::JawOpen); REQUIRE(jaw);
    REQUIRE(jaw->times.size() == size_t(clip.frameCount()));
    float maxJaw = 0; for (float v : jaw->values) { maxJaw = std::max(maxJaw, v); REQUIRE(v >= 0); REQUIRE(v <= 1); }
    CHECK(maxJaw > 0.2f);
    REQUIRE(clip.boneRotations.size() == 1);
    CHECK(clip.boneRotations[0].target == "Jaw");
    // applying the clip changes the rig, and variations modify curves as advertised
    clip.applyTo(rig, 0.75f);
    AnimationClip smile = clip; parseVariation("Increase smile").apply(smile);
    CHECK(smile.findBlendCurve(shapes::MouthSmile)->values[0] >= clip.findBlendCurve(shapes::MouthSmile)->values[0] + 0.3f);
    AnimationClip sub = clip; parseVariation("intensity=0.5").apply(sub);
    CHECK(sub.findBlendCurve(shapes::JawOpen)->values[10] == Approx(0.5f * jaw->values[10]));
}

TEST_CASE("glTF exporter writes a valid GLB with skin, morph targets and animation") {
    Pipeline p;
    REQUIRE(p.loadModel(""));
    p.buildDefaultRig();
    REQUIRE(p.loadAudio(""));
    REQUIRE(p.generateAnimation());
    std::string err;
    auto files = p.exportAll("/tmp/fr_export_test", "glb", {parseVariation("Raise eyebrows")}, &err);
    REQUIRE(files.size() == 2);
    std::ifstream f(files[0], std::ios::binary);
    REQUIRE(f.good());
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), {});
    REQUIRE(bytes.size() > 20);
    CHECK(std::memcmp(bytes.data(), "glTF", 4) == 0);
    uint32_t total; std::memcpy(&total, bytes.data() + 8, 4);
    CHECK(total == bytes.size());
    std::string json(bytes.data() + 20, bytes.data() + 20 + *reinterpret_cast<uint32_t*>(bytes.data() + 12));
    CHECK(json.find("\"skins\"") != std::string::npos);
    CHECK(json.find("\"targets\"") != std::string::npos);
    CHECK(json.find("\"animations\"") != std::string::npos);
    CHECK(json.find("\"path\":\"weights\"") != std::string::npos);
    CHECK(json.find("\"path\":\"rotation\"") != std::string::npos);
    CHECK(json.find("JawOpen") != std::string::npos);
    // .gltf + .bin variant
    REQUIRE(p.exportClip(p.clip, "/tmp/fr_export_test.gltf", &err));
    CHECK(std::ifstream("/tmp/fr_export_test.bin").good());
}

TEST_CASE("exporter factory picks an FBX writer when available, else glTF") {
    std::string note;
    auto ex = makeExporterForPath("foo.fbx", &note);
    REQUIRE(ex);
    if (!FR_HAVE_FBX_SDK && !FR_HAVE_ASSIMP) { CHECK(ex->fileExtension() == ".glb"); CHECK_FALSE(note.empty()); }
    else { CHECK(ex->fileExtension() == ".fbx"); CHECK(note.empty()); }
}

#if FR_HAVE_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
TEST_CASE("Assimp FBX exporter round-trips mesh, skin, blendshapes and animation") {
    Pipeline p;
    REQUIRE(p.loadModel(""));
    p.buildDefaultRig();
    REQUIRE(p.loadAudio(""));
    REQUIRE(p.generateAnimation());
    std::string err, written;
    REQUIRE(p.exportClip(p.clip, "/tmp/fr_export_test.fbx", &err, &written));
    CHECK(written == "/tmp/fr_export_test.fbx");
    Assimp::Importer imp;
    const aiScene* s = imp.ReadFile(written, 0);
    REQUIRE(s != nullptr);
    REQUIRE(s->mNumMeshes == 1);
    const aiMesh* m = s->mMeshes[0];
    CHECK(m->mNumFaces == p.rig.mesh.triangleCount());
    CHECK(m->mNumBones == 2);
    CHECK(m->mNumAnimMeshes == p.rig.blendShapes.size());
    REQUIRE(s->mNumAnimations == 1);
    const aiAnimation* a = s->mAnimations[0];
    CHECK(std::string(a->mName.C_Str()) == "LipSync");
    REQUIRE(a->mNumChannels == 1);
    CHECK(std::string(a->mChannels[0]->mNodeName.C_Str()) == "Jaw");
    CHECK(a->mChannels[0]->mNumRotationKeys >= 89);
    REQUIRE(a->mNumMorphMeshChannels == 1);
    CHECK(a->mMorphMeshChannels[0]->mNumKeys == unsigned(p.clip.frameCount()));
    // duration in seconds must match the clip
    CHECK(a->mDuration / a->mTicksPerSecond == Approx(p.clip.duration).margin(0.1));
}
#endif
