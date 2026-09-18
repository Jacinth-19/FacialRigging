#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "anim/animation_clip.h"
#include "anim/key_layer.h"
#include "app/pipeline.h"
#include "export/exporter.h"
#include <filesystem>
using namespace fr; using Catch::Approx;

TEST_CASE("key curve: hermite interpolation, auto tangents don't overshoot, flat/linear/free modes") {
    KeyCurve c;
    c.addKey(0.0f, 0.0f); c.addKey(1.0f, 1.0f); c.addKey(2.0f, 1.0f); c.addKey(3.0f, 0.0f);
    CHECK(c.evaluate(-1.0f) == Approx(0.0f)); CHECK(c.evaluate(5.0f) == Approx(0.0f));
    CHECK(c.evaluate(1.0f) == Approx(1.0f));
    // plateau between 1 and 2 must not overshoot above 1 (extrema get zero slope, Fritsch-Carlson clamp)
    for (float t = 1.0f; t <= 2.0f; t += 0.05f) CHECK(c.evaluate(t) <= 1.0f + 1e-5f);
    // monotone rise 0..1
    float prev = -1; for (float t = 0; t <= 1.0f; t += 0.05f) { float v = c.evaluate(t); CHECK(v >= prev - 1e-6f); prev = v; }
    // linear mode gives straight segments
    KeyCurve l; l.addKey(0, 0); l.addKey(2, 1); l.setTangentMode(0, TangentMode::Linear); l.setTangentMode(1, TangentMode::Linear);
    CHECK(l.evaluate(1.0f) == Approx(0.5f).margin(1e-5f)); CHECK(l.evaluate(0.5f) == Approx(0.25f).margin(1e-5f));
    // flat: ease in/out => midpoint still 0.5 but quarter point below linear
    KeyCurve f; f.addKey(0, 0); f.addKey(2, 1); f.setTangentMode(0, TangentMode::Flat); f.setTangentMode(1, TangentMode::Flat);
    CHECK(f.evaluate(1.0f) == Approx(0.5f).margin(1e-5f)); CHECK(f.evaluate(0.5f) < 0.25f);
    // free slope is preserved through updateTangents, broken tangents independent
    KeyCurve fr2; fr2.addKey(0, 0); fr2.addKey(1, 0); fr2.setTangentMode(0, TangentMode::Free); fr2.keys[0].outSlope = 2.0f; fr2.keys[0].inSlope = 2.0f; fr2.updateTangents();
    CHECK(fr2.keys[0].outSlope == Approx(2.0f)); CHECK(fr2.evaluate(0.25f) > 0.1f);
    fr2.setTangentMode(0, TangentMode::Flat, false, true); CHECK(fr2.keys[0].broken); CHECK(fr2.keys[0].outSlope == Approx(0.0f)); CHECK(fr2.keys[0].inSlope == Approx(2.0f));
    // move / remove keep order
    int i = c.moveKey(0, 2.5f); CHECK(i == 2); CHECK(c.keys[0].time == Approx(1.0f));
    c.removeKey(i); CHECK(c.keys.size() == 3);
}

TEST_CASE("key layer composites over the baked clip, survives regeneration, flattens into exports, round-trips JSON") {
    Pipeline p; std::string err;
    REQUIRE(p.loadModel("", &err)); p.buildDefaultRig();
    REQUIRE(p.loadAudio("", &err)); REQUIRE(p.generateAnimation());
    AnimationClip& clip = p.clip;
    Curve<float>* jaw = clip.findBlendCurve(shapes::JawOpen); REQUIRE(jaw);
    const float t = 1.0f, baked = jaw->sample(t);
    KeyCurve& kc = clip.keyLayer.get(shapes::JawOpen);
    kc.addKey(0.8f, 0.0f); kc.addKey(1.0f, 0.3f); kc.addKey(1.2f, 0.0f);
    CHECK(clip.blendAt(*jaw, t) == Approx(std::min(1.0f, baked + 0.3f)));
    CHECK(clip.blendAt(*jaw, 0.0f) == Approx(jaw->sample(0.0f)));
    // bone axis key: +10 deg about X on the Jaw bone
    REQUIRE(!clip.boneRotations.empty());
    const Curve<glm::quat>& jb = clip.boneRotations[0];
    clip.keyLayer.get(jb.target, 0).addKey(t, 10.0f);
    glm::quat q = clip.rotationAt(jb, t), q0 = jb.sample(t);
    CHECK(glm::degrees(glm::angle(glm::inverse(q0) * q)) == Approx(10.0f).margin(1e-3f));
    // applyTo uses the composite
    clip.applyTo(p.rig, t); CHECK(p.rig.blendWeight(shapes::JawOpen) == Approx(std::min(1.0f, baked + 0.3f)).margin(1e-4f));
    // layer weight / enable
    clip.keyLayer.weight = 0.5f; CHECK(clip.blendAt(*jaw, t) == Approx(std::min(1.0f, baked + 0.15f))); clip.keyLayer.weight = 1.0f;
    clip.keyLayer.enabled = false; CHECK(clip.blendAt(*jaw, t) == Approx(baked)); clip.keyLayer.enabled = true;
    // regeneration keeps the layer
    const int keys = clip.keyLayer.keyCount();
    REQUIRE(p.generateAnimation()); CHECK(p.clip.keyLayer.keyCount() == keys);
    // flattened clip has no layer but the same composite
    AnimationClip flat = p.clip.flattened(); CHECK(flat.keyLayer.empty());
    CHECK(flat.findBlendCurve(shapes::JawOpen)->sample(t) == Approx(p.clip.blendAt(*p.clip.findBlendCurve(shapes::JawOpen), t)).margin(1e-3f));
    // JSON round trip
    auto tmp = std::filesystem::temp_directory_path() / "fr_keylayer"; std::filesystem::create_directories(tmp);
    std::string path = (tmp / "clip.json").string();
    REQUIRE(p.exportClip(p.clip, path, &err));
    std::vector<AnimationClip> in; REQUIRE(loadClipsJson(path, in, &err)); REQUIRE(in.size() == 1);
    CHECK(in[0].keyLayer.keyCount() == keys);
    const KeyCurve* rc = in[0].keyLayer.find(shapes::JawOpen); REQUIRE(rc); CHECK(rc->evaluate(1.0f) == Approx(0.3f));
    CHECK(in[0].keyLayer.find(jb.target, 0) != nullptr);
    std::filesystem::remove_all(tmp);
}

#include "ui/timeline_common.h"
TEST_CASE("graph editor channel view: normalisation ranges and composite values") {
    AnimationClip clip; clip.duration = 2.0f; clip.frameRate = 30.0f;
    Curve<float> w; w.target = "JawOpen"; w.addKey(0.0f, 0.0f); w.addKey(2.0f, 0.5f); clip.blendCurves.push_back(w);
    Curve<glm::quat> r; r.target = "Head"; r.addKey(0.0f, glm::quat(1, 0, 0, 0)); r.addKey(2.0f, glm::angleAxis(glm::radians(12.0f), glm::vec3(1, 0, 0))); clip.boneRotations.push_back(r);
    auto ch = tl::channels(clip);
    REQUIRE(ch.size() == 4);                     // 1 blend + 3 bone axes
    CHECK(tl::channelLabel(ch[1]) == "Head X (deg)");
    float lo, hi; tl::channelRange(clip, ch[0], lo, hi); CHECK(lo == 0.0f); CHECK(hi == 1.0f);
    tl::channelRange(clip, ch[1], lo, hi); CHECK(lo == Approx(-15.0f)); CHECK(hi == Approx(15.0f));   // 12 deg rounded up to a multiple of 5, symmetric
    CHECK(tl::bakedAt(clip, ch[1], 2.0f) == Approx(12.0f).margin(0.05f));
    clip.keyLayer.get("Head", 0).addKey(1.0f, 10.0f);
    CHECK(tl::compositeAt(clip, ch[1], 1.0f) == Approx(6.0f + 10.0f).margin(0.1f));
    tl::channelRange(clip, ch[1], lo, hi); CHECK(hi == Approx(25.0f));   // key holds after 1 s: composite at 2 s = 12 + 10 -> rounded up to 25
    clip.keyLayer.get("JawOpen").addKey(2.0f, 0.3f);
    CHECK(tl::compositeAt(clip, ch[0], 2.0f) == Approx(0.8f));
}
