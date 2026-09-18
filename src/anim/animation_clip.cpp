#include "anim/animation_clip.h"
#include "rig/rig.h"
#include <algorithm>

namespace fr {

namespace {
template <typename T> T lerpT(const T& a, const T& b, float t) { return a + (b - a) * t; }
template <> glm::quat lerpT<glm::quat>(const glm::quat& a, const glm::quat& b, float t) { return glm::slerp(a, b, t); }
} // namespace

template <typename T>
T Curve<T>::sample(float t) const {
    if (times.empty()) return T{};
    if (t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();
    auto it = std::upper_bound(times.begin(), times.end(), t);
    size_t i = size_t(it - times.begin());
    float t0 = times[i - 1], t1 = times[i];
    float u = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0f;
    return lerpT(values[i - 1], values[i], u);
}
template struct Curve<float>;
template struct Curve<glm::vec3>;
template struct Curve<glm::quat>;

Curve<float>* AnimationClip::findBlendCurve(const std::string& target) {
    for (auto& c : blendCurves) if (c.target == target) return &c;
    return nullptr;
}
const Curve<float>* AnimationClip::findBlendCurve(const std::string& target) const {
    for (auto& c : blendCurves) if (c.target == target) return &c;
    return nullptr;
}

float AnimationClip::blendAt(const Curve<float>& baked, float t) const {
    return std::clamp(baked.sample(t) + keyLayer.evaluate(baked.target, -1, t), 0.0f, 1.0f);
}

glm::quat AnimationClip::rotationAt(const Curve<glm::quat>& baked, float t) const {
    glm::quat q = baked.sample(t);
    if (keyLayer.empty() || !keyLayer.enabled) return q;
    glm::vec3 e(keyLayer.evaluate(baked.target, 0, t), keyLayer.evaluate(baked.target, 1, t), keyLayer.evaluate(baked.target, 2, t));
    if (e == glm::vec3(0.0f)) return q;
    return glm::normalize(q * glm::quat(glm::radians(e)));
}

AnimationClip AnimationClip::flattened() const {
    AnimationClip out = *this;
    if (!keyLayer.empty() && keyLayer.enabled) {
        for (auto& c : out.blendCurves) for (size_t k = 0; k < c.times.size(); ++k) c.values[k] = blendAt(c, c.times[k]);
        for (auto& c : out.boneRotations) for (size_t k = 0; k < c.times.size(); ++k) c.values[k] = rotationAt(c, c.times[k]);
    }
    out.keyLayer.clear();
    return out;
}

void AnimationClip::applyTo(Rig& rig, float t) const {
    for (const auto& c : blendCurves) rig.setBlendWeight(c.target, blendAt(c, t));
    rig.applyCombinations();
    for (const auto& c : boneRotations) { int b = rig.skeleton.find(c.target); if (b >= 0) rig.skeleton.bones[b].poseRotation = rotationAt(c, t); }
    for (const auto& c : boneTranslations) { int b = rig.skeleton.find(c.target); if (b >= 0) rig.skeleton.bones[b].poseTranslation = c.sample(t); }
    rig.syncControlPointsFromRig();
}

void AnimationClip::smoothBlendCurves(int radius) {
    if (radius <= 0) return;
    for (auto& c : blendCurves) {
        std::vector<float> out(c.values.size());
        for (size_t i = 0; i < c.values.size(); ++i) {
            float acc = 0.0f; int n = 0;
            for (int k = -radius; k <= radius; ++k) {
                long j = long(i) + k;
                if (j >= 0 && size_t(j) < c.values.size()) { acc += c.values[size_t(j)]; ++n; }
            }
            out[i] = n ? acc / n : c.values[i];
        }
        c.values = out;
    }
}

void AnimationClip::scaleBlendCurves(float factor, float clampMax) {
    for (auto& c : blendCurves) for (auto& v : c.values) v = std::clamp(v * factor, 0.0f, clampMax);
}
void AnimationClip::offsetBlendCurve(const std::string& target, float offset) {
    if (auto* c = findBlendCurve(target)) for (auto& v : c->values) v = std::clamp(v + offset, 0.0f, 1.0f);
}

AnimationClip snapshotPose(const Rig& rig, const std::string& name) {
    AnimationClip clip; clip.name = name; clip.duration = 0.0f;
    for (const auto& bs : rig.blendShapes) { Curve<float> c; c.target = bs.name; c.addKey(0.0f, bs.weight); clip.blendCurves.push_back(c); }
    for (const auto& b : rig.skeleton.bones) {
        Curve<glm::quat> r; r.target = b.name; r.addKey(0.0f, b.poseRotation); clip.boneRotations.push_back(r);
        Curve<glm::vec3> t; t.target = b.name; t.addKey(0.0f, b.poseTranslation); clip.boneTranslations.push_back(t);
    }
    return clip;
}

} // namespace fr
