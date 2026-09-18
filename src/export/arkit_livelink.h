#pragma once
// ARKit 52-blendshape interchange:
//   * `arkitFrame()` samples a clip at time t into the standard 52 coefficients (+ head / eye
//     rotations) from the rig's canonical shapes, aliases and bones;
//   * `writeArkitCsv()` writes the "mocap CSV" layout produced by Live Link Face
//     (Timecode,BlendShapeCount,EyeBlinkLeft,...,HeadYaw,HeadPitch,HeadRoll,LeftEyeYaw,...);
//   * `LiveLinkSender` streams frames over UDP in the Live Link Face protocol (version 6 packets)
//     so Unreal's Live Link Face source (or Unity's ARKit Face Capture) previews in real time.
#include "anim/animation_clip.h"
#include "rig/rig.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fr {

constexpr int kArkitCount = 52;
/// Live Link Face order (matches the CSV column order and the UDP payload order).
extern const char* const kArkitNames[kArkitCount];
int arkitIndex(const std::string& name);              ///< -1 when unknown; case-insensitive, accepts "_L"/"Left" variants

struct ArkitFrame {
    std::array<float, kArkitCount> w{};               ///< 0..1 coefficients
    float headYaw = 0, headPitch = 0, headRoll = 0;    ///< degrees
    float eyeLYaw = 0, eyeLPitch = 0, eyeRYaw = 0, eyeRPitch = 0;
    double time = 0.0;
};

/// How canonical / authored shapes map onto ARKit coefficients. Built once per rig.
struct ArkitMapping {
    /// For each ARKit coefficient: list of (rig blendshape index, gain). Authored ARKit-named
    /// shapes map 1:1; canonical shapes fan out (MouthSmile -> mouthSmileLeft/Right, ...).
    std::vector<std::vector<std::pair<int, float>>> sources;
    int headBone = -1, eyeLBone = -1, eyeRBone = -1, jawBone = -1;
    float jawBoneToJawOpen = 0.0f;                    ///< deg of jaw rotation that equals jawOpen = 1 (0 = ignore bone)
    void build(const Rig& rig, float jawFullOpenDeg = 25.0f);
    int mappedCount() const;                          ///< number of ARKit coefficients with at least one source
};

/// Coefficients from the rig's *current* pose (weights + bone rotations).
ArkitFrame arkitFrameFromRig(const Rig& rig, const ArkitMapping& map);
/// Coefficients from a clip at time t (applies the clip to a scratch copy of the rig's weights/bones).
ArkitFrame arkitFrameFromClip(const Rig& rig, const ArkitMapping& map, const AnimationClip& clip, float t);

struct ArkitCsvOptions {
    float frameRate = 60.0f;                          ///< Live Link Face records at 60 fps by default
    bool includeTimecode = true;                      ///< "HH:MM:SS:FF.mmm" first column
    bool includeHeadAndEyes = true;                   ///< the 9 rotation columns after the 52 shapes
};
bool writeArkitCsv(const std::string& path, const Rig& rig, const AnimationClip& clip, const ArkitCsvOptions& opts, std::string* error = nullptr, const ArkitMapping* map = nullptr);
/// Reads a Live Link Face / mocap CSV back into a clip on this rig (ARKit names -> aliases /
/// canonical shapes; Head* -> Head bone). Missing columns are ignored.
bool readArkitCsv(const std::string& path, const Rig& rig, AnimationClip& out, std::string* error = nullptr);

/// Live Link Face UDP sender (Unreal "Live Link Face" source, packet version 6).
class LiveLinkSender {
public:
    struct Settings { std::string host = "127.0.0.1"; uint16_t port = 11111; std::string subjectName = "FacialRigging"; std::string deviceId = "FR-0001"; float frameRate = 60.0f; };
    ~LiveLinkSender();
    bool open(const Settings& s, std::string* error = nullptr);
    void close();
    bool isOpen() const { return sock_ >= 0; }
    /// Sends one frame; `frameIndex` feeds the packet's timecode. Returns bytes sent or -1.
    int send(const ArkitFrame& f, uint32_t frameIndex);
    /// Builds the packet without sending (tests / documentation).
    static std::vector<uint8_t> encode(const ArkitFrame& f, const Settings& s, uint32_t frameIndex);
    const Settings& settings() const { return settings_; }
    uint64_t framesSent() const { return frames_; }
private:
    int sock_ = -1; Settings settings_; uint64_t frames_ = 0;
};

} // namespace fr
