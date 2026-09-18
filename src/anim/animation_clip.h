#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "anim/key_layer.h"
#include <string>
#include <vector>

namespace fr {

class Rig;

/// A sampled channel: keys at a fixed frame rate (baked), optionally with explicit times.
template <typename T>
struct Curve {
    std::string target;              ///< blendshape name or bone name
    std::vector<float> times;        ///< seconds, strictly increasing
    std::vector<T> values;
    bool empty() const { return times.empty(); }
    void addKey(float t, const T& v) { times.push_back(t); values.push_back(v); }
    T sample(float t) const;         ///< linear (quat: slerp) interpolation, clamped
};

/// Baked animation: one curve per blendshape weight and per animated bone.
struct AnimationClip {
    std::string name = "Take001";
    float frameRate = 30.0f;
    float duration = 0.0f;
    std::vector<Curve<float>> blendCurves;
    std::vector<Curve<glm::quat>> boneRotations;
    std::vector<Curve<glm::vec3>> boneTranslations;
    KeyLayer keyLayer;                 ///< user keys on top of the baked curves (see key_layer.h)

    int frameCount() const { return int(duration * frameRate + 0.5f) + 1; }
    Curve<float>* findBlendCurve(const std::string& target);
    const Curve<float>* findBlendCurve(const std::string& target) const;
    /// Composite values (baked + key layer) - what playback and export use.
    float blendAt(const Curve<float>& baked, float t) const;
    glm::quat rotationAt(const Curve<glm::quat>& baked, float t) const;
    /// Copy with the key layer merged into the baked curves (same key times) and cleared.
    AnimationClip flattened() const;
    /// Writes the composite pose at time t into the rig (weights + bone poses).
    void applyTo(Rig& rig, float t) const;
    /// Temporal smoothing of blend curves (box filter of `radius` frames), for co-articulation / denoise.
    void smoothBlendCurves(int radius);
    /// Global multiplier on all blend curves (expression intensity variation).
    void scaleBlendCurves(float factor, float clampMax = 1.0f);
    /// Adds a constant to a named curve (e.g. "MouthSmile" += 0.3), clamped to [0,1].
    void offsetBlendCurve(const std::string& target, float offset);
};

/// Captures the current rig pose as a single-key clip (useful for tests / static exports).
AnimationClip snapshotPose(const Rig& rig, const std::string& name = "Pose");

} // namespace fr
