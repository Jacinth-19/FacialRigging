#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "app/pipeline.h"
#include "export/arkit_livelink.h"
#include "export/exporter.h"
#include "audio/wav_io.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using namespace fr; using Catch::Approx;

namespace {
Pipeline ict() { Pipeline p; std::string err; REQUIRE(p.loadModel(std::string(FR_ASSET_DIR) + "/models/ict_face/ict_face.obj", &err)); p.buildDefaultRig(); return p; }
}

TEST_CASE("ARKit name table and index lookup") {
    CHECK(arkitIndex("jawOpen") == 17); CHECK(arkitIndex("JawOpen") == 17);
    CHECK(arkitIndex("mouthSmile_L") == arkitIndex("MouthSmileLeft"));
    CHECK(arkitIndex("browInnerUp") == 43);
    CHECK(arkitIndex("tongueOut") == 51);
    CHECK(arkitIndex("nope") == -1);
    std::string all; for (int i = 0; i < kArkitCount; ++i) all += kArkitNames[i];
    CHECK(kArkitCount == 52);
}

TEST_CASE("ARKit frame from the ICT rig: authored shapes map 1:1, canonical fan out, bones to rotations") {
    Pipeline p = ict(); Rig& r = p.rig;
    ArkitMapping m; m.build(r);
    CHECK(m.mappedCount() >= 45);   // ICT has 53 ARKit-named targets minus a few (tongue etc.)
    r.resetPose();
    r.setBlendWeight("mouthSmile_L", 0.8f);
    r.setGaze(15.0f, 0.0f);
    r.skeleton.bones[size_t(r.skeleton.find(Rig::kHeadBone))].poseRotation = glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0));
    ArkitFrame f = arkitFrameFromRig(r, m);
    CHECK(f.w[size_t(arkitIndex("MouthSmileLeft"))] == Approx(0.8f));
    CHECK(f.w[size_t(arkitIndex("MouthSmileRight"))] == Approx(0.0f).margin(1e-4f));
    CHECK(f.headYaw == Approx(10.0f).margin(0.1f));
    CHECK(f.eyeLYaw == Approx(15.0f).margin(0.1f));
    CHECK(f.w[size_t(arkitIndex("EyeLookOutLeft"))] == Approx(0.5f).margin(0.02f));
    CHECK(f.w[size_t(arkitIndex("EyeLookInRight"))] == Approx(0.5f).margin(0.02f));
    // canonical-only rig (procedural head): MouthSmile fans out to both sides
    Rig q; q.setMesh(makeProceduralHead(16, 24)); q.buildDefaultFaceRig(); ArkitMapping mq; mq.build(q);
    q.setBlendWeight(shapes::MouthSmile, 0.6f);
    ArkitFrame g = arkitFrameFromRig(q, mq);
    CHECK(g.w[size_t(arkitIndex("MouthSmileLeft"))] == Approx(0.6f)); CHECK(g.w[size_t(arkitIndex("MouthSmileRight"))] == Approx(0.6f));
}

TEST_CASE("mocap CSV write + read round trip") {
    Pipeline p = ict();
    REQUIRE(p.loadAudio("", nullptr)); REQUIRE(p.generateAnimation());
    std::string path = (std::filesystem::temp_directory_path() / "fr_arkit.csv").string(); std::string err;
    ArkitCsvOptions o; o.frameRate = 60.0f;
    REQUIRE(writeArkitCsv(path, p.rig, p.clip, o, &err));
    std::ifstream f(path); std::string header; std::getline(f, header);
    CHECK(header.rfind("Timecode,BlendShapeCount,EyeBlinkLeft,", 0) == 0);
    CHECK(header.find(",JawOpen,") != std::string::npos);
    CHECK(header.find("RightEyeRoll") != std::string::npos);
    size_t rows = 0; std::string line; while (std::getline(f, line)) ++rows;
    CHECK(rows == size_t(std::lround(p.clip.duration * 60) + 1));
    AnimationClip back; REQUIRE(readArkitCsv(path, p.rig, back, &err));
    CHECK(back.duration == Approx(p.clip.duration).margin(0.05f));
    // the JawOpen drive survives (compare peak values)
    const Curve<float>* jo = back.findBlendCurve("jawOpen"); if (!jo) jo = back.findBlendCurve(shapes::JawOpen);
    REQUIRE(jo);
    float peak = 0; for (float v : jo->values) peak = std::max(peak, v);
    ArkitMapping m; m.build(p.rig); float peakSrc = 0;
    for (float t = 0; t <= p.clip.duration; t += 1.0f / 60) peakSrc = std::max(peakSrc, arkitFrameFromClip(p.rig, m, p.clip, t).w[size_t(arkitIndex("JawOpen"))]);
    CHECK(peak == Approx(peakSrc).margin(0.02f));
    std::filesystem::remove(path);
}

TEST_CASE("Live Link UDP packet: encode layout and loopback delivery") {
    ArkitFrame f; f.w[size_t(arkitIndex("JawOpen"))] = 0.75f; f.headYaw = 45.0f;
    LiveLinkSender::Settings s; s.subjectName = "Face"; s.deviceId = "DEV"; s.frameRate = 60;
    auto pkt = LiveLinkSender::encode(f, s, 1234);
    REQUIRE(pkt.size() == 1 + 4 + 3 + 4 + 4 + 4 + 4 + 4 + 4 + 1 + 61 * 4);
    CHECK(pkt[0] == 6);
    auto rdI32 = [&](size_t o) { return int32_t((uint32_t(pkt[o]) << 24) | (uint32_t(pkt[o + 1]) << 16) | (uint32_t(pkt[o + 2]) << 8) | pkt[o + 3]); };
    auto rdF32 = [&](size_t o) { uint32_t u = uint32_t(rdI32(o)); float v; std::memcpy(&v, &u, 4); return v; };
    CHECK(rdI32(1) == 3); CHECK(std::string(pkt.begin() + 5, pkt.begin() + 8) == "DEV");
    CHECK(rdI32(8) == 4); CHECK(std::string(pkt.begin() + 12, pkt.begin() + 16) == "Face");
    CHECK(rdI32(16) == 1234);
    CHECK(rdI32(24) == 60); CHECK(rdI32(28) == 1);
    CHECK(pkt[32] == 61);
    CHECK(rdF32(33 + 4 * size_t(arkitIndex("JawOpen"))) == Approx(0.75f));
    CHECK(rdF32(33 + 4 * 52) == Approx(0.5f));   // headYaw / 90
    // loopback
    int rx = ::socket(AF_INET, SOCK_DGRAM, 0); REQUIRE(rx >= 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    REQUIRE(::bind(rx, (sockaddr*)&a, sizeof a) == 0);
    socklen_t al = sizeof a; REQUIRE(::getsockname(rx, (sockaddr*)&a, &al) == 0);
    timeval tv{2, 0}; setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    LiveLinkSender tx; std::string err; s.host = "127.0.0.1"; s.port = ntohs(a.sin_port);
    REQUIRE(tx.open(s, &err));
    CHECK(tx.send(f, 7) == int(pkt.size()));
    std::vector<uint8_t> buf(1024); ssize_t n = ::recv(rx, buf.data(), buf.size(), 0);
    REQUIRE(n == ssize_t(pkt.size()));
    buf.resize(size_t(n)); pkt = buf;
    CHECK(rdI32(16) == 7);
    CHECK(rdF32(33 + 4 * size_t(arkitIndex("JawOpen"))) == Approx(0.75f));
    CHECK(tx.framesSent() == 1);
    ::close(rx);
}

TEST_CASE("audio ships with exports: glTF extras, clip JSON, CSV manifest, GLB embed") {
    Pipeline p = ict();
    REQUIRE(p.loadAudio("", nullptr)); REQUIRE(p.generateAnimation());
    auto tmp = std::filesystem::temp_directory_path() / "fr_audio_export"; std::filesystem::create_directories(tmp);
    std::string err;
    // glb (sidecar + embed)
    p.embedAudioInGlb = true;
    REQUIRE(p.exportClip(p.clip, (tmp / "scene.glb").string(), &err));
    REQUIRE(std::filesystem::exists(tmp / "scene.wav"));
    { std::ifstream f(tmp / "scene.glb", std::ios::binary); std::string all((std::istreambuf_iterator<char>(f)), {});
      CHECK(all.find("\"extras\":{\"audio\":{\"uri\":\"scene.wav\"") != std::string::npos);
      CHECK(all.find("\"mimeType\":\"audio/wav\"") != std::string::npos);
      CHECK(all.find("RIFF") != std::string::npos); }
    AudioBuffer back; REQUIRE(loadWav((tmp / "scene.wav").string(), back, &err));
    CHECK(back.duration() == Approx(p.audio.duration()).margin(0.01));
    // json
    REQUIRE(p.exportClip(p.clip, (tmp / "clip.json").string(), &err));
    { std::ifstream f(tmp / "clip.json"); std::string all((std::istreambuf_iterator<char>(f)), {}); CHECK(all.find("\"audio\":{\"uri\":\"clip.wav\"") != std::string::npos); }
    std::vector<AnimationClip> clips; REQUIRE(loadClipsJson((tmp / "clip.json").string(), clips, &err)); REQUIRE(clips.size() == 1);
    // csv via the factory + manifest
    REQUIRE(p.exportClip(p.clip, (tmp / "take.csv").string(), &err));
    CHECK(std::filesystem::exists(tmp / "take.audio.json")); CHECK(std::filesystem::exists(tmp / "take.wav"));
    // sidecar can be disabled
    p.exportAudioSidecar = false; std::filesystem::remove(tmp / "clip.wav");
    REQUIRE(p.exportClip(p.clip, (tmp / "clip.json").string(), &err));
    CHECK_FALSE(std::filesystem::exists(tmp / "clip.wav"));
    std::filesystem::remove_all(tmp);
}
