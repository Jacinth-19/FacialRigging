#pragma once
// Channel view over an AnimationClip shared by the viewport timeline and the graph editor:
// baked blend curves (axis -1, weight 0..1) and bone rotation axes (Euler degrees, additive layer).
#include "anim/animation_clip.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace fr::tl {

struct Channel { std::string target; int axis; int bakedIndex; bool bone; };

inline std::vector<Channel> channels(const AnimationClip& clip) {
    std::vector<Channel> ch;
    for (size_t i = 0; i < clip.blendCurves.size(); ++i) ch.push_back({clip.blendCurves[i].target, -1, int(i), false});
    for (size_t i = 0; i < clip.boneRotations.size(); ++i) for (int ax = 0; ax < 3; ++ax) ch.push_back({clip.boneRotations[i].target, ax, int(i), true});
    return ch;
}
inline std::string channelLabel(const Channel& c) { return c.bone ? c.target + (c.axis == 0 ? " X (deg)" : c.axis == 1 ? " Y (deg)" : " Z (deg)") : c.target; }
/// Baked value in channel units (weight 0..1 or degrees).
inline float bakedAt(const AnimationClip& clip, const Channel& c, float t) {
    if (!c.bone) return clip.blendCurves[size_t(c.bakedIndex)].sample(t);
    glm::vec3 e = glm::degrees(glm::eulerAngles(clip.boneRotations[size_t(c.bakedIndex)].sample(t))); return e[c.axis];
}
inline float compositeAt(const AnimationClip& clip, const Channel& c, float t) { return bakedAt(clip, c, t) + clip.keyLayer.evaluate(c.target, c.axis, t); }
/// Natural display range of a channel: weights 0..1, degrees symmetric around 0 (multiple of 5, covering the data).
inline void channelRange(const AnimationClip& clip, const Channel& c, float& lo, float& hi) {
    if (!c.bone) { lo = 0.0f; hi = 1.0f; return; }
    float m = 5.0f; const auto& bc = clip.boneRotations[size_t(c.bakedIndex)];
    const size_t step = std::max<size_t>(1, bc.times.size() / 256);
    for (size_t k = 0; k < bc.times.size(); k += step) m = std::max(m, std::fabs(compositeAt(clip, c, bc.times[k])));
    if (!bc.times.empty()) m = std::max(m, std::fabs(compositeAt(clip, c, bc.times.back())));
    if (const KeyCurve* kc = clip.keyLayer.find(c.target, c.axis)) for (const Key& k : kc->keys) m = std::max(m, std::fabs(compositeAt(clip, c, k.time)));
    m = std::ceil(m / 5.0f) * 5.0f; lo = -m; hi = m;
}

} // namespace fr::tl
