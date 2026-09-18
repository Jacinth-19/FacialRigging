#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "app/pipeline.h"
#include "rig/rig_tools.h"
#include <glm/gtx/norm.hpp>
using namespace fr; using Catch::Approx;

namespace {
Rig proceduralRig() { Rig r; r.setMesh(makeProceduralHead(24, 32)); r.buildDefaultFaceRig(); return r; }
float sumW(const VertexInfluence& i) { return i.weights.x + i.weights.y + i.weights.z + i.weights.w; }
}

TEST_CASE("weight brush: add / subtract / replace / smooth keep weights normalised and local") {
    Rig r = proceduralRig();
    REQUIRE(r.hasSkin());
    const int jaw = r.skeleton.find(Rig::kJawBone); REQUIRE(jaw >= 0);
    VertexAdjacency adj; adj.build(r.mesh); REQUIRE_FALSE(adj.empty());
    // pick a forehead vertex (high y, front): jaw weight should be ~0 there
    glm::vec3 lo = r.mesh.boundsMin(), hi = r.mesh.boundsMax();
    glm::vec3 c(0, lo.y + 0.8f * (hi.y - lo.y), hi.z);
    uint32_t vi = 0; float best = 1e9f;
    for (size_t i = 0; i < r.mesh.vertexCount(); ++i) { float d = glm::length2(r.mesh.positions[i] - c); if (d < best) { best = d; vi = uint32_t(i); } }
    REQUIRE(boneWeightOf(r.skin[vi], jaw) < 0.05f);
    WeightBrush b; b.bone = jaw; b.radius = 0.15f * (hi.y - lo.y); b.strength = 0.5f; b.frontFacingOnly = false;
    int n = paintWeights(r, b, r.mesh.positions[vi], glm::vec3(0));
    CHECK(n > 0);
    float w1 = boneWeightOf(r.skin[vi], jaw);
    CHECK(w1 == Approx(0.5f).margin(0.06f));
    for (auto& inf : r.skin) CHECK(sumW(inf) == Approx(1.0f).margin(1e-4f));
    // locality: a vertex far away (chin) is unchanged by the forehead dab
    glm::vec3 chin(0, lo.y + 0.05f * (hi.y - lo.y), hi.z);
    uint32_t cj = 0; best = 1e9f; for (size_t i = 0; i < r.mesh.vertexCount(); ++i) { float d = glm::length2(r.mesh.positions[i] - chin); if (d < best) { best = d; cj = uint32_t(i); } }
    CHECK(boneWeightOf(r.skin[cj], jaw) > 0.5f);
    // subtract brings it back
    b.mode = WeightBrushMode::Subtract; paintWeights(r, b, r.mesh.positions[vi], glm::vec3(0));
    CHECK(boneWeightOf(r.skin[vi], jaw) < 0.1f);
    // replace to 1
    b.mode = WeightBrushMode::Replace; b.value = 1.0f; b.strength = 1.0f; paintWeights(r, b, r.mesh.positions[vi], glm::vec3(0));
    CHECK(boneWeightOf(r.skin[vi], jaw) == Approx(1.0f).margin(1e-3f));
    // smooth pulls it toward its neighbours (which are < 1 at the brush edge)
    b.mode = WeightBrushMode::Smooth; b.strength = 1.0f; b.radius *= 0.3f;
    float before = boneWeightOf(r.skin[vi], jaw);
    paintWeights(r, b, r.mesh.positions[vi], glm::vec3(0), &adj);
    CHECK(boneWeightOf(r.skin[vi], jaw) <= before + 1e-4f);
    for (auto& inf : r.skin) CHECK(sumW(inf) == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("front-facing test skips vertices behind the head") {
    Rig r = proceduralRig();
    const int jaw = r.skeleton.find(Rig::kJawBone);
    glm::vec3 lo = r.mesh.boundsMin(), hi = r.mesh.boundsMax();
    // vertex at the back of the head, same height as the forehead
    glm::vec3 back(0, lo.y + 0.8f * (hi.y - lo.y), lo.z);
    uint32_t vb = 0; float best = 1e9f; for (size_t i = 0; i < r.mesh.vertexCount(); ++i) { float d = glm::length2(r.mesh.positions[i] - back); if (d < best) { best = d; vb = uint32_t(i); } }
    WeightBrush b; b.bone = jaw; b.radius = 0.1f * (hi.y - lo.y); b.strength = 1.0f; b.frontFacingOnly = true;
    // camera looks down -z (viewDir = -z): the back vertex's normal (+z... no: -z) faces away
    glm::vec3 viewDir(0, 0, -1);
    float w0 = boneWeightOf(r.skin[vb], jaw);
    paintWeights(r, b, r.mesh.positions[vb], viewDir);
    CHECK(boneWeightOf(r.skin[vb], jaw) == Approx(w0));
    b.frontFacingOnly = false; paintWeights(r, b, r.mesh.positions[vb], viewDir);
    CHECK(boneWeightOf(r.skin[vb], jaw) > w0 + 0.5f);
}

TEST_CASE("mirror weights L->R copies painted weights and swaps L/R bones") {
    Pipeline p; std::string err;
    REQUIRE(p.loadModel(std::string(FR_ASSET_DIR) + "/models/ict_face/ict_face.obj", &err));
    p.buildDefaultRig();
    Rig& r = p.rig; REQUIRE(r.hasEyeBones());
    MirrorMap mm; mm.build(r.mesh);
    size_t paired = 0; for (int q : mm.partner) paired += q >= 0;
    CHECK(paired > r.mesh.vertexCount() * 0.9);
    const int eyeL = r.skeleton.find(Rig::kEyeLBone), eyeR = r.skeleton.find(Rig::kEyeRBone);
    CHECK(mirrorBone(r.skeleton, eyeL) == eyeR); CHECK(mirrorBone(r.skeleton, eyeR) == eyeL);
    CHECK(mirrorBone(r.skeleton, r.skeleton.find(Rig::kJawBone)) == r.skeleton.find(Rig::kJawBone));
    // paint EyeL weight onto a left-cheek vertex (x > 0 is the model's left) then mirror
    glm::vec3 lo = r.mesh.boundsMin(), hi = r.mesh.boundsMax();
    glm::vec3 cheek(0.25f * (hi.x - lo.x), lo.y + 0.55f * (hi.y - lo.y), hi.z);
    uint32_t vc = 0; float best = 1e9f; for (size_t i = 0; i < r.mesh.vertexCount(); ++i) { float d = glm::length2(r.mesh.positions[i] - cheek); if (d < best) { best = d; vc = uint32_t(i); } }
    WeightBrush b; b.bone = eyeL; b.radius = 0.06f * (hi.y - lo.y); b.strength = 1.0f; b.mode = WeightBrushMode::Replace; b.frontFacingOnly = false;
    paintWeights(r, b, r.mesh.positions[vc], glm::vec3(0));
    REQUIRE(boneWeightOf(r.skin[vc], eyeL) == Approx(1.0f).margin(1e-3f));
    int partner = mm.partner[vc]; REQUIRE(partner >= 0);
    CHECK(boneWeightOf(r.skin[size_t(partner)], eyeR) < 0.5f);
    int n = mirrorWeights(r, true, mm);
    CHECK(n > 0);
    CHECK(boneWeightOf(r.skin[size_t(partner)], eyeR) == Approx(1.0f).margin(1e-3f));
    CHECK(boneWeightOf(r.skin[size_t(partner)], eyeL) == Approx(0.0f).margin(1e-3f));
    // symmetric brush does both sides in one dab
    b.bone = eyeL; b.symmetric = true; b.value = 0.0f;
    paintWeights(r, b, r.mesh.positions[vc], glm::vec3(0));
    CHECK(boneWeightOf(r.skin[vc], eyeL) < 1e-3f);
    CHECK(boneWeightOf(r.skin[size_t(partner)], eyeR) < 1e-3f);
}

TEST_CASE("bake free-form handle pose as a new blendshape reproduces the deformation") {
    Rig r = proceduralRig();
    glm::vec3 lo = r.mesh.boundsMin(), hi = r.mesh.boundsMax();
    int cp = r.addControlPoint(glm::vec3(0.1f * (hi.x - lo.x), lo.y + 0.4f * (hi.y - lo.y), hi.z), "Bulge");
    r.bindFreeForm(cp, 0.2f * (hi.y - lo.y));
    r.moveControlPoint(cp, glm::vec3(0, 0, 0.05f));
    std::vector<glm::vec3> posed = r.evaluate();
    size_t nShapes = r.blendShapes.size();
    BakeShapeOptions o; o.name = "CheekBulge";
    int idx = bakePoseAsBlendShape(r, o);
    REQUIRE(idx == int(nShapes));
    CHECK(r.blendShapes.size() == nShapes + 1);
    CHECK(r.blendShapes[size_t(idx)].indices.size() > 10);
    CHECK(r.blendShapes[size_t(idx)].indices.size() < r.mesh.vertexCount());   // sparse
    CHECK(glm::length(r.controlPoints[size_t(cp)].offset) == Approx(0.0f));    // handle consumed
    // weight 0 -> bind pose; weight 1 -> the posed mesh
    std::vector<glm::vec3> rest = r.evaluate();
    for (size_t i = 0; i < rest.size(); i += 97) CHECK(glm::length(rest[i] - r.mesh.positions[i]) < 1e-6f);
    r.setBlendWeight(idx, 1.0f);
    std::vector<glm::vec3> again = r.evaluate();
    float maxErr = 0; for (size_t i = 0; i < again.size(); ++i) maxErr = std::max(maxErr, glm::length(again[i] - posed[i]));
    CHECK(maxErr < 1e-5f);
    // mirrored pair
    r.setBlendWeight(idx, 0.0f); r.moveControlPoint(cp, glm::vec3(0, 0, 0.05f));
    MirrorMap mm; mm.build(r.mesh); o.name = "Cheek"; o.mirrorToOtherSide = true;
    int li = bakePoseAsBlendShape(r, o, &mm);
    REQUIRE(li >= 0); REQUIRE(r.blendShapes.size() == nShapes + 3);
    CHECK(r.blendShapes[size_t(li)].name == "Cheek_L"); CHECK(r.blendShapes[size_t(li) + 1].name == "Cheek_R");
    // the R shape lives on x<0
    for (size_t k = 0; k < r.blendShapes[size_t(li) + 1].indices.size(); ++k) CHECK(r.mesh.positions[r.blendShapes[size_t(li) + 1].indices[k]].x <= 1e-4f);
    CHECK(r.blendShapes[size_t(li) + 1].indices.size() > 0);
}

TEST_CASE("corrective combination shape fires from its drivers and restores the sculpted fix") {
    Rig r = proceduralRig();
    r.setBlendWeight(shapes::JawOpen, 1.0f); r.setBlendWeight(shapes::MouthPucker, 1.0f);
    // "fix": push the lips forward with a free-form handle
    glm::vec3 lo = r.mesh.boundsMin(), hi = r.mesh.boundsMax();
    int cp = r.addControlPoint(glm::vec3(0, lo.y + 0.3f * (hi.y - lo.y), hi.z), "LipFix");
    r.bindFreeForm(cp, 0.15f * (hi.y - lo.y)); r.moveControlPoint(cp, glm::vec3(0, 0, 0.03f));
    std::vector<glm::vec3> fixedPose = r.evaluate();
    int idx = bakeCorrectiveShape(r, shapes::JawOpen, shapes::MouthPucker, "JawOpen_MouthPucker");
    REQUIRE(idx >= 0);
    REQUIRE(r.combinations.size() == 1);
    CHECK(r.blendShapes[size_t(idx)].weight == Approx(1.0f));      // both drivers at 1 -> fires fully
    std::vector<glm::vec3> viaCorrective = r.evaluate();
    float maxErr = 0; for (size_t i = 0; i < viaCorrective.size(); ++i) maxErr = std::max(maxErr, glm::length(viaCorrective[i] - fixedPose[i]));
    CHECK(maxErr < 1e-5f);
    // one driver off -> corrective off; half/half -> quarter
    r.setBlendWeight(shapes::MouthPucker, 0.0f); r.applyCombinations();
    CHECK(r.blendShapes[size_t(idx)].weight == Approx(0.0f));
    r.setBlendWeight(shapes::MouthPucker, 0.5f); r.setBlendWeight(shapes::JawOpen, 0.5f); r.applyCombinations();
    CHECK(r.blendShapes[size_t(idx)].weight == Approx(0.25f));
    // clip playback evaluates correctives
    AnimationClip clip; clip.frameRate = 30; clip.duration = 1.0f;
    Curve<float> a; a.target = shapes::JawOpen; a.addKey(0, 0); a.addKey(1, 1); clip.blendCurves.push_back(a);
    Curve<float> b; b.target = shapes::MouthPucker; b.addKey(0, 1); b.addKey(1, 1); clip.blendCurves.push_back(b);
    clip.applyTo(r, 1.0f);
    CHECK(r.blendShapes[size_t(idx)].weight == Approx(1.0f));
    clip.applyTo(r, 0.0f);
    CHECK(r.blendShapes[size_t(idx)].weight == Approx(0.0f));
    // re-baking the same name replaces rather than duplicates
    size_t n = r.blendShapes.size();
    r.setBlendWeight(shapes::JawOpen, 1.0f); r.setBlendWeight(shapes::MouthPucker, 1.0f); r.applyCombinations();
    r.moveControlPoint(cp, glm::vec3(0, 0, 0.01f));
    int idx2 = bakeCorrectiveShape(r, shapes::JawOpen, shapes::MouthPucker, "JawOpen_MouthPucker");
    CHECK(idx2 == idx); CHECK(r.blendShapes.size() == n); CHECK(r.combinations.size() == 1);
}
