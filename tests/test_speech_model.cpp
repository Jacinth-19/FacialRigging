#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "app/pipeline.h"
#include "anim/coarticulation.h"
#include "anim/idle_motion.h"
#include "audio/g2p.h"
#include "audio/phoneme_aligner.h"
#include "audio/wav_io.h"
#include <glm/gtx/quaternion.hpp>
using namespace fr;
using Catch::Approx;

TEST_CASE("co-articulation: bilabial closes fully, vowel rounding anticipates, silence relaxes") {
    // "AA  M  UW": open vowel, lip closure, rounded vowel
    std::vector<VisemeSegment> segs = {{Viseme::Silence, 0.0, 0.2}, {Viseme::AA, 0.2, 0.5}, {Viseme::MBP, 0.5, 0.58}, {Viseme::UW, 0.58, 0.9}, {Viseme::Silence, 0.9, 1.2}};
    Coarticulator c; c.setSegments(segs);
    // inside the (short) M the lips must be essentially closed - strong dominance wins locally
    VisemePose m = c.poseAt(0.54);
    CHECK(m.lipsPress > 0.8f); CHECK(m.jawOpen < 0.15f);
    // rounding starts before UW begins (anticipatory co-articulation) - during the M/AA boundary
    CHECK(c.poseAt(0.49).pucker > 0.05f);
    CHECK(c.poseAt(0.30).pucker < c.poseAt(0.49).pucker);
    // mid-vowel is close to its target but slightly pulled by neighbours
    VisemePose aa = c.poseAt(0.35); CHECK(aa.jawOpen > 0.6f); CHECK(aa.jawOpen < visemePose(Viseme::AA).jawOpen + 1e-4f);
    // silence relaxes toward closed but not instantly
    CHECK(c.poseAt(0.95).pucker > 0.1f); CHECK(c.poseAt(1.15).pucker < c.poseAt(0.95).pucker);
    // weights sum to 1 and pick the right dominant viseme
    VisemeFrame w = c.weightsAt(0.35); float s = 0; for (float x : w.weights) s += x; CHECK(s == Approx(1.0f).margin(1e-4f)); CHECK(w.dominant() == Viseme::AA);
    // disabled = hard switching
    c.settings.enabled = false; CHECK(c.poseAt(0.49).pucker == 0.0f); CHECK(c.poseAt(0.54).lipsPress == 1.0f);
}

TEST_CASE("segmentVisemes merges runs and absorbs flicker") {
    std::vector<VisemeFrame> f(20);
    for (int i = 0; i < 20; ++i) { f[size_t(i)].weights.fill(0); f[size_t(i)].weights[size_t(i < 10 ? Viseme::AA : Viseme::UW)] = 1; }
    f[5].weights.fill(0); f[5].weights[size_t(Viseme::MBP)] = 1; // one-frame flicker
    auto segs = segmentVisemes(f, 0.01, 0.03);
    REQUIRE(segs.size() == 2); CHECK(segs[0].viseme == Viseme::AA); CHECK(segs[0].end == Approx(0.10)); CHECK(segs[1].viseme == Viseme::UW);
}

TEST_CASE("G2P: lexicon words, rules, plurals, bracketed ARPAbet, digits") {
    CHECK(wordToPhonemes("the") == std::vector<std::string>{"DH", "AH"});
    CHECK(wordToPhonemes("Mouth") == std::vector<std::string>{"M", "AW", "TH"});
    CHECK(wordToPhonemes("books") == std::vector<std::string>{"B", "UH", "K", "S"});
    CHECK(wordToPhonemes("[HH AH L OW]") == std::vector<std::string>{"HH", "AH", "L", "OW"});
    auto rules = wordToPhonemes("blimp"); // not in lexicon: rules
    CHECK(rules == std::vector<std::string>{"B", "L", "IH", "M", "P"});
    CHECK(wordToPhonemes("shape") == std::vector<std::string>{"SH", "EY", "P"});
    auto t = transcriptToPhonemes("Hello, 2 mouths!");
    REQUIRE(t.size() == 3); CHECK(t[1].text == "two"); CHECK(t[2].phones.front() == "M");
}

TEST_CASE("forced alignment recovers the phrase order and silences on synthetic speech") {
    // Build 3 "words" from the synthetic-speech visemes: we can't know the words the synthesiser
    // 'said', so align a transcript to a track whose posteriors we fabricate from that transcript
    // (with noise); the aligner must recover boundaries within a couple of frames.
    FeatureTrack track; track.sampleRate = 16000; track.config.hopSize = 160; // 10 ms frames
    const double dt = track.frameInterval();
    struct Ph { const char* p; int frames; }; std::vector<Ph> truth = {{"SIL", 20}, {"M", 6}, {"AA", 15}, {"P", 5}, {"SIL", 12}, {"F", 8}, {"IY", 14}, {"T", 5}, {"SIL", 20}};
    std::vector<VisemeFrame> post; std::vector<Viseme> gt;
    unsigned rng = 3;
    for (auto& ph : truth) for (int k = 0; k < ph.frames; ++k) {
        Viseme v = phonemeToViseme(ph.p); gt.push_back(v);
        AudioFrameFeatures f; f.time = post.size() * dt; f.loudness = v == Viseme::Silence ? 0.02f : 0.6f; track.frames.push_back(f);
        VisemeFrame vf; vf.time = f.time; for (auto& w : vf.weights) { rng = rng * 1103515245u + 12345u; w = 0.05f + 0.15f * float((rng >> 16) & 0xff) / 255.0f; }
        vf.weights[size_t(v)] += 0.6f; float s = 0; for (float w : vf.weights) s += w; for (auto& w : vf.weights) w /= s; post.push_back(vf);
    }
    track.duration = post.size() * dt;
    PhonemeAligner al;
    AlignmentResult r = al.align("[M AA P] [F IY T]", track, post);
    REQUIRE(!r.phones.empty());
    // every non-silence frame labelled with the true viseme class within a 2-frame tolerance
    int wrong = 0, total = 0;
    for (size_t t = 2; t + 2 < gt.size(); ++t) {
        Viseme got = Viseme::Silence; for (auto& ph : r.phones) if (t * dt >= ph.start - 1e-9 && t * dt < ph.end - 1e-9) got = ph.viseme;
        bool ok = false; for (int d = -2; d <= 2; ++d) if (gt[size_t(long(t) + d)] == got) ok = true;
        total++; wrong += !ok;
    }
    CHECK(wrong <= total / 20);
    CHECK(r.coverage > 0.9f);
    // word indices preserved in order
    int lastW = -1; for (auto& ph : r.phones) if (ph.wordIndex >= 0) { CHECK(ph.wordIndex >= lastW); lastW = ph.wordIndex; }
    CHECK(lastW == 1);
    // segments merge phones of equal viseme and are contiguous
    for (size_t i = 1; i < r.segments.size(); ++i) { CHECK(r.segments[i].start == Approx(r.segments[i - 1].end)); CHECK(r.segments[i].viseme != r.segments[i - 1].viseme); }
}

TEST_CASE("pipeline with a transcript drives the mouth from aligned segments") {
    Pipeline p; std::string err; REQUIRE(p.loadModel("", &err)); p.buildDefaultRig(); REQUIRE(p.loadAudio("", &err));
    p.transcript = "she had your dark suit in greasy wash water all year";
    REQUIRE(p.generateAnimation());
    CHECK(!p.lastAlignment.phones.empty()); CHECK(p.lastAlignment.words.size() == 11);
    CHECK(p.lastSegments == p.lastAlignment.segments);
    // 'suit' / 'wash water' should produce some rounding somewhere, and press for the 'p'-less phrase stays low mostly
    auto* pk = p.clip.findBlendCurve(shapes::MouthPucker); REQUIRE(pk); float mx = 0; for (float v : pk->values) mx = std::max(mx, v); CHECK(mx > 0.2f);
    // without a transcript we still get a clip
    p.transcript.clear(); REQUIRE(p.generateAnimation()); CHECK(p.lastAlignment.phones.empty()); CHECK(!p.lastSegments.empty());
}

TEST_CASE("idle model: asymmetric blinks, pause-triggered, double blinks, lids follow gaze, breathing") {
    IdleMotionModel m;
    // profile: fast close, hold, slow open
    CHECK(m.blinkProfile(0.0f) == Approx(0.0f)); CHECK(m.blinkProfile(0.08f) == Approx(1.0f)); CHECK(m.blinkProfile(0.10f) == Approx(1.0f));
    CHECK(m.blinkProfile(0.04f) > 0.4f); CHECK(m.blinkProfile(0.04f) < 0.6f);
    CHECK(m.blinkProfile(0.11f + 0.075f) < 0.7f); CHECK(m.blinkProfile(0.11f + 0.075f) > 0.2f); CHECK(m.blinkProfile(0.4f) == 0.0f);
    // track: 2 s speech, 1 s pause, 2 s speech, 3 s silence
    FeatureTrack tr; tr.sampleRate = 16000; tr.config.hopSize = 160; const double dt = tr.frameInterval();
    for (int i = 0; i < 800; ++i) { AudioFrameFeatures f; f.time = i * dt; bool sp = (i < 200) || (i >= 300 && i < 500); f.loudness = sp ? 0.5f : 0.01f; tr.frames.push_back(f); }
    tr.duration = 8.0;
    auto s = m.bake(tr, 1.0f / 60.0f);
    // statistics over seeds: ~2-4 blinks in 8 s (17-20/min active), and a blink shortly after
    // the pause at t=2 s (clause boundary) in most runs
    int totalBlinks = 0, pauseBlinks = 0; const int seeds = 12;
    for (int sd = 1; sd <= seeds; ++sd) {
        IdleMotionModel ms; ms.settings.seed = unsigned(sd); auto ss = ms.bake(tr, 1.0f / 60.0f);
        int blinks = 0; for (auto& x : ss) blinks += x.blinkStart; CHECK(blinks >= 1); CHECK(blinks <= 9); totalBlinks += blinks;
        bool pb = false; for (size_t i = size_t(2.0f * 60); i < size_t(2.6f * 60); ++i) pb |= ss[i].blinkStart; pauseBlinks += pb;
    }
    CHECK(totalBlinks >= 2 * seeds); CHECK(totalBlinks <= 6 * seeds);
    CHECK(pauseBlinks >= seeds / 2);
    float maxBlink = 0; for (auto& x : s) maxBlink = std::max(maxBlink, x.blink); CHECK(maxBlink == Approx(1.0f));
    // deterministic for a seed, different for another
    auto s2 = m.bake(tr, 1.0f / 60.0f); CHECK(s2[100].blink == s[100].blink);
    IdleMotionModel m3; m3.settings.seed = 99; auto s3 = m3.bake(tr, 1.0f / 60.0f); bool differs = false; for (size_t i = 0; i < s.size(); ++i) differs |= s3[i].blinkStart != s[i].blinkStart; CHECK(differs);
    // lids follow downward gaze
    std::vector<float> gaze(s.size(), glm::radians(-20.0f));
    auto s4 = m.bake(tr, 1.0f / 60.0f, &gaze); float minLid = 1; for (auto& x : s4) minLid = std::min(minLid, x.blink); CHECK(minLid > 0.15f);
    // breathing oscillates in [-1,1] with ~0.25-0.33 Hz -> 2-3 cycles in 8 s
    int crossings = 0; for (size_t i = 1; i < s.size(); ++i) crossings += (s[i - 1].breath < 0) != (s[i].breath < 0);
    CHECK(crossings >= 3); CHECK(crossings <= 7);
    bool inhaleCue = false; for (size_t i = size_t(2.8f * 60); i < size_t(3.3f * 60); ++i) inhaleCue |= s[i].inhaleCue > 0.3f; CHECK(inhaleCue);
    // disabled
    IdleMotionModel off; off.settings.blinkRate = 0; off.settings.breathing = 0; auto s5 = off.bake(tr, 1.0f / 60.0f); for (auto& x : s5) { CHECK(x.blink == 0.0f); CHECK(x.breath == 0.0f); }
}

TEST_CASE("tongue bone on the ICT head is driven by L/TH visemes and hidden behind closed lips") {
    Pipeline p; std::string err; REQUIRE(p.loadModel(std::string(FR_ASSET_DIR) + "/models/ict_face/ict_face.obj", &err)); p.buildDefaultRig();
    REQUIRE(p.rig.hasTongueBone());
    int tb = p.rig.skeleton.find(Rig::kTongueBone); CHECK(p.rig.skeleton.bones[size_t(tb)].parent == p.rig.skeleton.find(Rig::kJawBone));
    auto parts = p.rig.detectParts(); auto tv = p.rig.mesh.partVertices(parts.tongue);
    // tip vertices (max z) are fully on the tongue bone, root vertices on the jaw
    glm::vec3 lo, hi; p.rig.mesh.partBounds(parts.tongue, lo, hi);
    for (uint32_t v : tv) { float z = (p.rig.mesh.positions[v].z - lo.z) / (hi.z - lo.z); if (z > 0.7f) CHECK(p.rig.skin[v].weights[0] == Approx(1.0f)); }
    auto rest = p.rig.evaluate();
    p.rig.setTongue(1.0f, 0.0f); auto up = p.rig.evaluate();
    float tipRise = -1e9f; for (uint32_t v : tv) tipRise = std::max(tipRise, up[v].y - rest[v].y); CHECK(tipRise > 0.005f);
    for (uint32_t v : p.rig.mesh.partVertices(parts.face)) REQUIRE(glm::length(up[v] - rest[v]) < 1e-6f);
    p.rig.setTongue(0.0f, 1.0f); auto out = p.rig.evaluate();
    float push = -1e9f; for (uint32_t v : tv) push = std::max(push, out[v].z - rest[v].z); CHECK(push > 0.02f);
    // generator: L_TH frames give tongue curves; MBP gives none (hidden)
    LipSyncGenerator gen;
    VisemeFrame lth; lth.weights.fill(0); lth.weights[size_t(Viseme::L_TH)] = 1; auto mp = gen.mouthPose(lth, 0.5f); CHECK(mp.tongueUp > 0.5f);
    VisemeFrame mbp; mbp.weights.fill(0); mbp.weights[size_t(Viseme::MBP)] = 1; CHECK(gen.mouthPose(mbp, 0.5f).tongueUp == 0.0f);
    REQUIRE(p.loadAudio("", &err)); REQUIRE(p.generateAnimation());
    bool hasTongue = false; for (auto& c : p.clip.boneRotations) hasTongue |= c.target == Rig::kTongueBone; CHECK(hasTongue);
    bool hasTr = false; for (auto& c : p.clip.boneTranslations) hasTr |= c.target == Rig::kTongueBone; CHECK(hasTr);
}
