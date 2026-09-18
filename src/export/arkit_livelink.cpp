#include "export/arkit_livelink.h"
#include "rig/blendshape_io.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fr {

const char* const kArkitNames[kArkitCount] = {
    "EyeBlinkLeft", "EyeLookDownLeft", "EyeLookInLeft", "EyeLookOutLeft", "EyeLookUpLeft", "EyeSquintLeft", "EyeWideLeft",
    "EyeBlinkRight", "EyeLookDownRight", "EyeLookInRight", "EyeLookOutRight", "EyeLookUpRight", "EyeSquintRight", "EyeWideRight",
    "JawForward", "JawLeft", "JawRight", "JawOpen", "MouthClose", "MouthFunnel", "MouthPucker", "MouthLeft", "MouthRight",
    "MouthSmileLeft", "MouthSmileRight", "MouthFrownLeft", "MouthFrownRight", "MouthDimpleLeft", "MouthDimpleRight",
    "MouthStretchLeft", "MouthStretchRight", "MouthRollLower", "MouthRollUpper", "MouthShrugLower", "MouthShrugUpper",
    "MouthPressLeft", "MouthPressRight", "MouthLowerDownLeft", "MouthLowerDownRight", "MouthUpperUpLeft", "MouthUpperUpRight",
    "BrowDownLeft", "BrowDownRight", "BrowInnerUp", "BrowOuterUpLeft", "BrowOuterUpRight",
    "CheekPuff", "CheekSquintLeft", "CheekSquintRight", "NoseSneerLeft", "NoseSneerRight", "TongueOut"};

namespace {
std::string norm(std::string s) {
    // lower-case, drop separators, map _L/_R and Left/Right suffixes to "left"/"right"
    std::string o; for (char c : s) if (std::isalnum((unsigned char)c)) o += char(std::tolower((unsigned char)c));
    auto endsWith = [&](const char* suf) { size_t n = std::strlen(suf); return o.size() > n && o.compare(o.size() - n, n, suf) == 0; };
    if (endsWith("l") && !endsWith("left")) { std::string base = s; size_t p = base.find_last_of("_"); if (p != std::string::npos && base.substr(p) == "_L") o = o.substr(0, o.size() - 1) + "left"; }
    if (endsWith("r") && !endsWith("right")) { std::string base = s; size_t p = base.find_last_of("_"); if (p != std::string::npos && base.substr(p) == "_R") o = o.substr(0, o.size() - 1) + "right"; }
    return o;
}
}

int arkitIndex(const std::string& name) {
    static std::vector<std::string> normed; if (normed.empty()) for (int i = 0; i < kArkitCount; ++i) normed.push_back(norm(kArkitNames[i]));
    std::string n = norm(name);
    for (int i = 0; i < kArkitCount; ++i) if (normed[size_t(i)] == n) return i;
    return -1;
}

// ------------------------------------------------------------------ mapping
void ArkitMapping::build(const Rig& rig, float jawFullOpenDeg) {
    sources.assign(kArkitCount, {});
    // 1. authored shapes with ARKit names map 1:1 (skip the canonical merged copies)
    std::vector<char> direct(kArkitCount, 0);
    for (size_t i = 0; i < rig.blendShapes.size(); ++i) {
        const auto& bs = rig.blendShapes[i];
        bool canonical = false; for (const char* c : shapes::All) canonical |= bs.name == c;
        if (canonical) continue;
        int k = arkitIndex(bs.name); if (k >= 0) { sources[size_t(k)].push_back({int(i), 1.0f}); direct[size_t(k)] = 1; }
    }
    // 2. canonical shapes fan out onto the ARKit coefficients they stand for - only where no
    //    authored shape already provides that coefficient (otherwise we'd double count)
    struct Fan { const char* canonical; const char* arkit; float gain; };
    static const Fan fans[] = {
        {shapes::JawOpen, "JawOpen", 1.0f}, {shapes::MouthSmile, "MouthSmileLeft", 1.0f}, {shapes::MouthSmile, "MouthSmileRight", 1.0f},
        {shapes::MouthPucker, "MouthPucker", 1.0f}, {shapes::MouthWide, "MouthStretchLeft", 1.0f}, {shapes::MouthWide, "MouthStretchRight", 1.0f},
        {shapes::LipsPress, "MouthPressLeft", 1.0f}, {shapes::LipsPress, "MouthPressRight", 1.0f}, {shapes::LipsPress, "MouthClose", 0.5f},
        {shapes::BrowRaise, "BrowInnerUp", 1.0f}, {shapes::BrowRaise, "BrowOuterUpLeft", 0.7f}, {shapes::BrowRaise, "BrowOuterUpRight", 0.7f},
        {shapes::EyeBlink, "EyeBlinkLeft", 1.0f}, {shapes::EyeBlink, "EyeBlinkRight", 1.0f}, {shapes::MouthFunnel, "MouthFunnel", 1.0f},
        {shapes::MouthFrown, "MouthFrownLeft", 1.0f}, {shapes::MouthFrown, "MouthFrownRight", 1.0f},
        {shapes::BrowDown, "BrowDownLeft", 1.0f}, {shapes::BrowDown, "BrowDownRight", 1.0f}, {shapes::EyeWide, "EyeWideLeft", 1.0f}, {shapes::EyeWide, "EyeWideRight", 1.0f}};
    for (const Fan& f : fans) {
        int k = arkitIndex(f.arkit); int s = rig.findBlendShape(f.canonical);
        if (k < 0 || s < 0 || direct[size_t(k)]) continue;
        // an authored canonical shape already *is* the sum of its aliases: still map it (the aliases were skipped above only if they carry ARKit names, in which case direct[] is set)
        sources[size_t(k)].push_back({s, f.gain});
    }
    headBone = rig.skeleton.find(Rig::kHeadBone); eyeLBone = rig.skeleton.find(Rig::kEyeLBone); eyeRBone = rig.skeleton.find(Rig::kEyeRBone); jawBone = rig.skeleton.find(Rig::kJawBone);
    jawBoneToJawOpen = jawFullOpenDeg;
}
int ArkitMapping::mappedCount() const { int n = 0; for (const auto& s : sources) n += !s.empty(); return n; }

namespace {
void eulerDeg(const glm::quat& q, float& yaw, float& pitch, float& roll) {
    glm::vec3 e = glm::degrees(glm::eulerAngles(glm::normalize(q)));   // pitch(x), yaw(y), roll(z)
    pitch = e.x; yaw = e.y; roll = e.z;
}
}

ArkitFrame arkitFrameFromRig(const Rig& rig, const ArkitMapping& map) {
    ArkitFrame f;
    for (int k = 0; k < kArkitCount && size_t(k) < map.sources.size(); ++k) {
        float w = 0; for (const auto& [s, g] : map.sources[size_t(k)]) if (s >= 0 && size_t(s) < rig.blendShapes.size()) w += rig.blendShapes[size_t(s)].weight * g;
        f.w[size_t(k)] = std::clamp(w, 0.0f, 1.0f);
    }
    // jaw bone adds to JawOpen (the rig splits jaw motion between shape and bone)
    if (map.jawBone >= 0 && map.jawBoneToJawOpen > 0) {
        float yaw, pitch, roll; eulerDeg(rig.skeleton.bones[size_t(map.jawBone)].poseRotation, yaw, pitch, roll);
        int k = arkitIndex("JawOpen"); f.w[size_t(k)] = std::clamp(f.w[size_t(k)] + std::abs(pitch) / map.jawBoneToJawOpen, 0.0f, 1.0f);
    }
    if (map.headBone >= 0) eulerDeg(rig.skeleton.bones[size_t(map.headBone)].poseRotation, f.headYaw, f.headPitch, f.headRoll);
    float r;
    if (map.eyeLBone >= 0) eulerDeg(rig.skeleton.bones[size_t(map.eyeLBone)].poseRotation, f.eyeLYaw, f.eyeLPitch, r);
    if (map.eyeRBone >= 0) eulerDeg(rig.skeleton.bones[size_t(map.eyeRBone)].poseRotation, f.eyeRYaw, f.eyeRPitch, r);
    // ARKit eye-look coefficients from the gaze (yaw + = model's left / +x; ARKit "Left" eye = user's left)
    auto look = [&](float yaw, float pitch, const char* in, const char* out, const char* up, const char* down, bool leftEye) {
        float y = std::clamp(yaw / 30.0f, -1.0f, 1.0f), p = std::clamp(pitch / 30.0f, -1.0f, 1.0f);
        // looking toward +x (model's left) is "In" for the right eye and "Out" for the left eye
        float toLeft = std::max(y, 0.0f), toRight = std::max(-y, 0.0f);
        f.w[size_t(arkitIndex(leftEye ? out : in))] = std::max(f.w[size_t(arkitIndex(leftEye ? out : in))], toLeft);
        f.w[size_t(arkitIndex(leftEye ? in : out))] = std::max(f.w[size_t(arkitIndex(leftEye ? in : out))], toRight);
        f.w[size_t(arkitIndex(up))] = std::max(f.w[size_t(arkitIndex(up))], std::max(-p, 0.0f));   // our pitch: -x rotation looks up
        f.w[size_t(arkitIndex(down))] = std::max(f.w[size_t(arkitIndex(down))], std::max(p, 0.0f));
    };
    if (map.eyeLBone >= 0) look(f.eyeLYaw, f.eyeLPitch, "EyeLookInLeft", "EyeLookOutLeft", "EyeLookUpLeft", "EyeLookDownLeft", true);
    if (map.eyeRBone >= 0) look(f.eyeRYaw, f.eyeRPitch, "EyeLookInRight", "EyeLookOutRight", "EyeLookUpRight", "EyeLookDownRight", false);
    return f;
}

ArkitFrame arkitFrameFromClip(const Rig& rig, const ArkitMapping& map, const AnimationClip& clip, float t) {
    Rig scratch; scratch.skeleton = rig.skeleton; scratch.blendShapes.reserve(rig.blendShapes.size());
    for (const auto& bs : rig.blendShapes) { BlendShape b; b.name = bs.name; b.weight = bs.weight; scratch.blendShapes.push_back(std::move(b)); }
    scratch.combinations = rig.combinations;
    clip.applyTo(scratch, t);
    ArkitFrame f = arkitFrameFromRig(scratch, map); f.time = t; return f;
}

// ------------------------------------------------------------------ CSV
namespace {
std::string timecode(double t, float fps) {
    int h = int(t / 3600); t -= h * 3600; int m = int(t / 60); t -= m * 60; int s = int(t); double frac = t - s;
    int frame = int(frac * fps); double ms = (frac * fps - frame) * 1000.0 / fps;
    char b[48]; std::snprintf(b, sizeof b, "%02d:%02d:%02d:%02d.%03d", h, m, s, frame, int(ms)); return b;
}
}

bool writeArkitCsv(const std::string& path, const Rig& rig, const AnimationClip& clip, const ArkitCsvOptions& opts, std::string* error, const ArkitMapping* mapIn) {
    ArkitMapping local; if (!mapIn) { local.build(rig); mapIn = &local; }
    std::ofstream f(path); if (!f) { if (error) *error = "cannot write " + path; return false; }
    if (opts.includeTimecode) f << "Timecode,";
    f << "BlendShapeCount";
    for (int k = 0; k < kArkitCount; ++k) f << ',' << kArkitNames[k];
    if (opts.includeHeadAndEyes) f << ",HeadYaw,HeadPitch,HeadRoll,LeftEyeYaw,LeftEyePitch,LeftEyeRoll,RightEyeYaw,RightEyePitch,RightEyeRoll";
    f << '\n';
    int frames = std::max(1, int(std::round(clip.duration * opts.frameRate)) + 1);
    f.setf(std::ios::fixed); f.precision(6);
    for (int i = 0; i < frames; ++i) {
        double t = double(i) / opts.frameRate; if (t > clip.duration) t = clip.duration;
        ArkitFrame fr = arkitFrameFromClip(rig, *mapIn, clip, float(t));
        if (opts.includeTimecode) f << timecode(t, opts.frameRate) << ',';
        f << kArkitCount;
        for (int k = 0; k < kArkitCount; ++k) f << ',' << fr.w[size_t(k)];
        if (opts.includeHeadAndEyes) f << ',' << fr.headYaw << ',' << fr.headPitch << ',' << fr.headRoll << ',' << fr.eyeLYaw << ',' << fr.eyeLPitch << ",0," << fr.eyeRYaw << ',' << fr.eyeRPitch << ",0";
        f << '\n';
    }
    return true;
}

bool readArkitCsv(const std::string& path, const Rig& rig, AnimationClip& out, std::string* error) {
    std::ifstream f(path); if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::string line; if (!std::getline(f, line)) { if (error) *error = "empty file"; return false; }
    auto split = [](const std::string& s) { std::vector<std::string> v; std::string cur; for (char c : s) { if (c == ',') { v.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; } v.push_back(cur); return v; };
    std::vector<std::string> header = split(line);
    int tcCol = -1, headYaw = -1, headPitch = -1, headRoll = -1;
    // column -> list of rig shapes it drives
    struct Col { int col; std::vector<int> shapes; };
    std::vector<Col> cols;
    // canonical targets get the *max* of their contributing ARKit columns (e.g. mouthSmileLeft/Right)
    std::vector<std::pair<int, std::string>> canonCols;
    for (size_t c = 0; c < header.size(); ++c) {
        const std::string& h = header[c];
        if (h == "Timecode") { tcCol = int(c); continue; }
        if (h == "HeadYaw") headYaw = int(c); else if (h == "HeadPitch") headPitch = int(c); else if (h == "HeadRoll") headRoll = int(c);
        int k = arkitIndex(h); if (k < 0) continue;
        Col col{int(c), {}};
        for (size_t s = 0; s < rig.blendShapes.size(); ++s) {
            const auto& bs = rig.blendShapes[s]; bool canonical = false; for (const char* cn : shapes::All) canonical |= bs.name == cn;
            if (!canonical && arkitIndex(bs.name) == k) col.shapes.push_back(int(s));
        }
        cols.push_back(col);
        // canonical: which canonical name does this ARKit coefficient feed?
        std::string lowerFirst = std::string(kArkitNames[k]); lowerFirst[0] = char(std::tolower((unsigned char)lowerFirst[0]));
        // try ICT-style "_L/_R" naming for the canonical table
        std::string ict = lowerFirst; for (const char* suf : {"Left", "Right"}) { size_t n = std::strlen(suf); if (ict.size() > n && ict.compare(ict.size() - n, n, suf) == 0) { ict = ict.substr(0, ict.size() - n) + (suf[0] == 'L' ? "_L" : "_R"); break; } }
        std::string canon = canonicalShapeName(ict); if (canon.empty()) canon = canonicalShapeName(lowerFirst);
        if (!canon.empty() && rig.findBlendShape(canon) >= 0) canonCols.push_back({int(c), canon});
    }
    out = AnimationClip{}; out.name = std::filesystem::path(path).stem().string(); out.frameRate = 60.0f;
    std::map<int, Curve<float>> byShape; std::map<std::string, Curve<float>> byCanon; Curve<glm::quat> head; head.target = Rig::kHeadBone;
    int row = 0; double lastT = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::vector<std::string> v = split(line);
        double t = row / 60.0;
        if (tcCol >= 0 && size_t(tcCol) < v.size()) { int hh, mm, ss, ff; double ms = 0; if (std::sscanf(v[size_t(tcCol)].c_str(), "%d:%d:%d:%d.%lf", &hh, &mm, &ss, &ff, &ms) >= 4) t = hh * 3600 + mm * 60 + ss + (ff + ms / 1000.0) / 60.0; }
        if (row > 0 && t <= lastT) t = lastT + 1.0 / 60.0;
        lastT = t;
        auto val = [&](int c) { return c >= 0 && size_t(c) < v.size() ? float(std::atof(v[size_t(c)].c_str())) : 0.0f; };
        for (const Col& c : cols) for (int s : c.shapes) { auto& cv = byShape[s]; if (cv.target.empty()) cv.target = rig.blendShapes[size_t(s)].name; cv.addKey(float(t), val(c.col)); }
        std::map<std::string, float> canonMax; for (const auto& [c, name] : canonCols) canonMax[name] = std::max(canonMax[name], val(c));
        for (const auto& [name, w] : canonMax) { auto& cv = byCanon[name]; if (cv.target.empty()) cv.target = name; cv.addKey(float(t), w); }
        if (headYaw >= 0) head.addKey(float(t), glm::quat(glm::radians(glm::vec3(val(headPitch), val(headYaw), val(headRoll)))));
        ++row;
    }
    if (row == 0) { if (error) *error = "no data rows"; return false; }
    out.duration = float(lastT);
    for (auto& [s, cv] : byShape) { bool any = false; for (float w : cv.values) any |= w > 1e-4f; if (any) out.blendCurves.push_back(std::move(cv)); }
    for (auto& [n, cv] : byCanon) { bool any = false; for (float w : cv.values) any |= w > 1e-4f; if (any) out.blendCurves.push_back(std::move(cv)); }
    if (!head.empty() && rig.skeleton.find(Rig::kHeadBone) >= 0) { bool any = false; for (const auto& q : head.values) any |= std::abs(glm::angle(q)) > 1e-3f; if (any) out.boneRotations.push_back(std::move(head)); }
    return true;
}

// ------------------------------------------------------------------ Live Link UDP
namespace {
void putU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void putU32BE(std::vector<uint8_t>& b, uint32_t v) { b.push_back(uint8_t(v >> 24)); b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
void putI32BE(std::vector<uint8_t>& b, int32_t v) { putU32BE(b, uint32_t(v)); }
void putF32BE(std::vector<uint8_t>& b, float f) { uint32_t u; std::memcpy(&u, &f, 4); putU32BE(b, u); }
void putStr(std::vector<uint8_t>& b, const std::string& s) { putI32BE(b, int32_t(s.size())); b.insert(b.end(), s.begin(), s.end()); }
}

// Packet layout (Live Link Face app, version 6):
//   u8 version(6) | FString deviceId (i32 len + utf8) | FString subject | i32 frame | i32 subframe(float bits)
//   i32 fps numerator | i32 fps denominator | u8 blendshapeCount (61) | 61 x f32 big-endian
// The 61 values are the 52 ARKit coefficients followed by HeadYaw, HeadPitch, HeadRoll,
// LeftEyeYaw, LeftEyePitch, LeftEyeRoll, RightEyeYaw, RightEyePitch, RightEyeRoll (radians? no - the
// app sends degrees/π... Unreal's source normalises; we send the same units the CSV uses: degrees / 90
// so that ±90° maps to ±1 like the app).
std::vector<uint8_t> LiveLinkSender::encode(const ArkitFrame& f, const Settings& s, uint32_t frameIndex) {
    std::vector<uint8_t> b; b.reserve(400);
    putU8(b, 6);
    putStr(b, s.deviceId); putStr(b, s.subjectName);
    putI32BE(b, int32_t(frameIndex)); putF32BE(b, 0.0f);
    putI32BE(b, int32_t(std::lround(s.frameRate))); putI32BE(b, 1);
    putU8(b, 61);
    for (int k = 0; k < kArkitCount; ++k) putF32BE(b, f.w[size_t(k)]);
    const float rot[9] = {f.headYaw / 90.0f, f.headPitch / 90.0f, f.headRoll / 90.0f, f.eyeLYaw / 90.0f, f.eyeLPitch / 90.0f, 0.0f, f.eyeRYaw / 90.0f, f.eyeRPitch / 90.0f, 0.0f};
    for (float r : rot) putF32BE(b, r);
    return b;
}

LiveLinkSender::~LiveLinkSender() { close(); }
void LiveLinkSender::close() {
#ifndef _WIN32
    if (sock_ >= 0) ::close(sock_);
#endif
    sock_ = -1;
}
bool LiveLinkSender::open(const Settings& s, std::string* error) {
    close(); settings_ = s; frames_ = 0;
#ifdef _WIN32
    if (error) *error = "Live Link sender not built for Windows yet"; return false;
#else
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM; addrinfo* res = nullptr;
    if (getaddrinfo(s.host.c_str(), std::to_string(s.port).c_str(), &hints, &res) != 0 || !res) { if (error) *error = "cannot resolve " + s.host; return false; }
    sock_ = ::socket(res->ai_family, SOCK_DGRAM, 0);
    if (sock_ < 0) { freeaddrinfo(res); if (error) *error = "socket() failed"; return false; }
    int bcast = 1; setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof bcast);
    if (::connect(sock_, res->ai_addr, res->ai_addrlen) != 0) { freeaddrinfo(res); close(); if (error) *error = "connect() failed for " + s.host; return false; }
    freeaddrinfo(res);
    return true;
#endif
}
int LiveLinkSender::send(const ArkitFrame& f, uint32_t frameIndex) {
    if (sock_ < 0) return -1;
    std::vector<uint8_t> pkt = encode(f, settings_, frameIndex);
#ifdef _WIN32
    return -1;
#else
    ssize_t n = ::send(sock_, pkt.data(), pkt.size(), 0);
    if (n > 0) ++frames_;
    return int(n);
#endif
}

} // namespace fr
