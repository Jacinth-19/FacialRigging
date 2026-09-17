#include "rig/rig.h"
#include "rig/blendshape_io.h"
#include <map>
#include "rig/rbf_deformer.h"
#include "core/raycast.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace fr {

// ---------------------------------------------------------------- Skeleton
int Skeleton::find(const std::string& name) const {
    for (size_t i = 0; i < bones.size(); ++i) if (bones[i].name == name) return int(i);
    return -1;
}

static glm::mat4 trs(const glm::vec3& t, const glm::quat& r) {
    return glm::translate(glm::mat4(1.0f), t) * glm::toMat4(r);
}

std::vector<glm::mat4> Skeleton::bindWorldMatrices() const {
    std::vector<glm::mat4> w(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        glm::mat4 local = trs(bones[i].bindTranslation, bones[i].bindRotation);
        w[i] = bones[i].parent >= 0 ? w[bones[i].parent] * local : local; // parents precede children
    }
    return w;
}

std::vector<glm::mat4> Skeleton::poseWorldMatrices() const {
    std::vector<glm::mat4> w(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        const Bone& b = bones[i];
        glm::mat4 local = trs(b.bindTranslation + b.poseTranslation, b.bindRotation * b.poseRotation);
        w[i] = b.parent >= 0 ? w[b.parent] * local : local;
    }
    return w;
}

std::vector<glm::mat4> Skeleton::skinningMatrices() const {
    auto bind = bindWorldMatrices();
    auto pose = poseWorldMatrices();
    for (size_t i = 0; i < bones.size(); ++i) pose[i] = pose[i] * glm::inverse(bind[i]);
    return pose;
}

void Skeleton::resetPose() {
    for (auto& b : bones) { b.poseRotation = glm::quat(1, 0, 0, 0); b.poseTranslation = glm::vec3(0.0f); }
}

// ---------------------------------------------------------------- VertexInfluence
void VertexInfluence::add(int bone, float w) {
    if (w <= 0.0f) return;
    int slot = 0;
    for (int i = 0; i < 4; ++i) if (weights[i] < weights[slot]) slot = i;
    if (weights[slot] < w) { bones[slot] = bone; weights[slot] = w; }
}
void VertexInfluence::normalize() {
    float s = weights.x + weights.y + weights.z + weights.w;
    if (s > 1e-9f) weights /= s;
}

// ---------------------------------------------------------------- BlendShape
std::vector<glm::vec3> BlendShape::dense(size_t vertexCount) const {
    std::vector<glm::vec3> d(vertexCount, glm::vec3(0.0f));
    for (size_t i = 0; i < indices.size(); ++i) if (indices[i] < vertexCount) d[indices[i]] = deltas[i];
    return d;
}

// ---------------------------------------------------------------- Rig
void Rig::setMesh(const Mesh& m) {
    mesh = m;
    if (mesh.normals.size() != mesh.positions.size()) mesh.recomputeNormals();
    skin.clear(); blendShapes.clear(); controlPoints.clear(); skeleton = Skeleton{};
}

int Rig::findBlendShape(const std::string& name) const {
    for (size_t i = 0; i < blendShapes.size(); ++i) if (blendShapes[i].name == name) return int(i);
    return -1;
}
float Rig::blendWeight(const std::string& name) const { int i = findBlendShape(name); return i < 0 ? 0.0f : blendShapes[i].weight; }
void Rig::setBlendWeight(const std::string& name, float w) { int i = findBlendShape(name); if (i >= 0) setBlendWeight(i, w); }
void Rig::setBlendWeight(int i, float w) { if (i >= 0 && size_t(i) < blendShapes.size()) blendShapes[i].weight = w; }
std::vector<float> Rig::blendWeights() const {
    std::vector<float> w; w.reserve(blendShapes.size());
    for (auto& b : blendShapes) w.push_back(b.weight);
    return w;
}
void Rig::setBlendWeights(const std::vector<float>& w) {
    for (size_t i = 0; i < w.size() && i < blendShapes.size(); ++i) blendShapes[i].weight = w[i];
}
void Rig::resetPose() {
    for (auto& b : blendShapes) b.weight = 0.0f;
    skeleton.resetPose();
    for (auto& c : controlPoints) c.offset = glm::vec3(0.0f);
}

int Rig::addControlPoint(const glm::vec3& p, const std::string& name) {
    ControlPoint cp;
    cp.name = name.empty() ? "CP" + std::to_string(controlPoints.size() + 1) : name;
    cp.restPosition = p;
    cp.nearestVertex = closestVertex(mesh.positions, p);
    controlPoints.push_back(cp);
    return int(controlPoints.size()) - 1;
}
void Rig::removeControlPoint(int i) { if (i >= 0 && size_t(i) < controlPoints.size()) controlPoints.erase(controlPoints.begin() + i); }
void Rig::bindToBone(int cp, int bone) { auto& c = controlPoints.at(cp); c.binding = BindingType::Bone; c.target = bone; }
void Rig::bindToBlendShape(int cp, int shape, const glm::vec3& axis, float range) {
    auto& c = controlPoints.at(cp); c.binding = BindingType::BlendShape; c.target = shape;
    c.driveAxis = glm::normalize(axis); c.driveRange = std::max(range, 1e-4f);
}
void Rig::bindFreeForm(int cp, float radius) { auto& c = controlPoints.at(cp); c.binding = BindingType::FreeForm; c.radius = radius; c.target = -1; }

int Rig::mirrorPartner(int i) const {
    if (i < 0 || size_t(i) >= controlPoints.size()) return -1;
    const ControlPoint& c = controlPoints[size_t(i)];
    auto swapSuffix = [](std::string n) -> std::string {
        if (n.size() < 2) return "";
        char last = n.back(); std::string base = n.substr(0, n.size() - 1);
        if (last == 'L') return base + "R"; if (last == 'R') return base + "L";
        if (n.size() > 2 && n.compare(n.size() - 2, 2, "_L") == 0) return n.substr(0, n.size() - 2) + "_R";
        if (n.size() > 2 && n.compare(n.size() - 2, 2, "_R") == 0) return n.substr(0, n.size() - 2) + "_L";
        return "";
    };
    std::string want = swapSuffix(c.name);
    if (!want.empty()) for (size_t k = 0; k < controlPoints.size(); ++k) if (int(k) != i && controlPoints[k].name == want) return int(k);
    if (std::abs(c.restPosition.x) < 1e-3f) return -1; // on the centre line: no partner
    glm::vec3 m = c.restPosition * glm::vec3(-1, 1, 1);
    int best = -1; float bestD = 0.05f * 0.05f;
    for (size_t k = 0; k < controlPoints.size(); ++k) { if (int(k) == i) continue; float d = glm::dot(controlPoints[k].restPosition - m, controlPoints[k].restPosition - m); if (d < bestD) { bestD = d; best = int(k); } }
    return best;
}

void Rig::moveControlPoint(int i, const glm::vec3& off) {
    if (forceSymmetry) {
        int j = mirrorPartner(i);
        if (j >= 0) { bool saved = forceSymmetry; forceSymmetry = false; moveControlPoint(j, off * glm::vec3(-1, 1, 1)); forceSymmetry = saved; }
    }
    ControlPoint& c = controlPoints.at(i);
    c.offset = off;
    switch (c.binding) {
    case BindingType::Bone:
        if (c.target >= 0 && size_t(c.target) < skeleton.bones.size()) {
            // Translate the bone so the handle follows; rotation is edited via the UI/animation.
            skeleton.bones[c.target].poseTranslation = off;
        }
        break;
    case BindingType::BlendShape:
        if (c.target >= 0 && size_t(c.target) < blendShapes.size()) {
            float w = glm::dot(off, c.driveAxis) / c.driveRange;
            blendShapes[c.target].weight = std::clamp(w, 0.0f, 1.0f);
        }
        break;
    default: break;
    }
}

void Rig::syncControlPointsFromRig() {
    for (auto& c : controlPoints) {
        if (c.binding == BindingType::Bone && c.target >= 0 && size_t(c.target) < skeleton.bones.size())
            c.offset = skeleton.bones[c.target].poseTranslation;
        else if (c.binding == BindingType::BlendShape && c.target >= 0 && size_t(c.target) < blendShapes.size())
            c.offset = c.driveAxis * (blendShapes[c.target].weight * c.driveRange);
    }
}

// ---------------------------------------------------------------- evaluation
void Rig::applySkin(std::vector<glm::vec3>& v) const {
    if (!hasSkin() || skeleton.bones.empty()) return;
    auto M = skeleton.skinningMatrices();
    for (size_t i = 0; i < v.size(); ++i) {
        const VertexInfluence& inf = skin[i];
        glm::vec4 p(v[i], 1.0f);
        glm::vec3 acc(0.0f); float total = 0.0f;
        for (int k = 0; k < 4; ++k) {
            float w = inf.weights[k];
            if (w <= 0.0f) continue;
            acc += w * glm::vec3(M[inf.bones[k]] * p);
            total += w;
        }
        if (total > 1e-6f) v[i] = acc / total;
    }
}

void Rig::applyBlendShapes(std::vector<glm::vec3>& v) const {
    for (const auto& bs : blendShapes) {
        if (std::abs(bs.weight) < 1e-5f) continue;
        for (size_t i = 0; i < bs.indices.size(); ++i) {
            uint32_t vi = bs.indices[i];
            if (vi < v.size()) v[vi] += bs.weight * bs.deltas[i];
        }
    }
}

void Rig::applyFreeForm(std::vector<glm::vec3>& v) const {
    std::vector<RbfDeformer::Handle> handles;
    for (const auto& c : controlPoints)
        if (c.binding == BindingType::FreeForm && glm::dot(c.offset, c.offset) > 1e-12f)
            handles.push_back({c.restPosition, c.offset, c.radius});
    if (handles.empty()) return;
    RbfDeformer rbf; rbf.setHandles(handles);
    // Evaluate the field on bind-pose positions so the warp is stable under other deformers.
    for (size_t i = 0; i < v.size(); ++i) v[i] += rbf.displacementAt(mesh.positions[i]);
}

void Rig::evaluate(std::vector<glm::vec3>& out) const {
    out = mesh.positions;
    if (skinFirst) { applySkin(out); applyBlendShapes(out); }
    else { applyBlendShapes(out); applySkin(out); }
    applyFreeForm(out);
}

// ---------------------------------------------------------------- default rig
namespace {
float smoothstep(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
/// Radial falloff (1 at centre, 0 at radius) with ellipsoidal scaling.
float falloff(const glm::vec3& p, const glm::vec3& c, const glm::vec3& radii) {
    glm::vec3 d = (p - c) / radii;
    float r = glm::length(d);
    return r >= 1.0f ? 0.0f : 1.0f - smoothstep(0.0f, 1.0f, r);
}
} // namespace

void Rig::buildDefaultFaceRig() {
    const glm::vec3 lo = mesh.boundsMin(), hi = mesh.boundsMax();
    const glm::vec3 size = hi - lo;
    const glm::vec3 centre = 0.5f * (lo + hi);
    const float H = size.y, W = size.x, D = size.z;
    // Landmark estimates in normalised face space (front is +z).
    const float frontZ = hi.z;
    const glm::vec3 mouth(centre.x, lo.y + 0.28f * H, frontZ);
    const glm::vec3 browL(centre.x - 0.20f * W, lo.y + 0.68f * H, frontZ);
    const glm::vec3 browR(centre.x + 0.20f * W, lo.y + 0.68f * H, frontZ);
    const glm::vec3 eyeL(centre.x - 0.20f * W, lo.y + 0.60f * H, frontZ);
    const glm::vec3 eyeR(centre.x + 0.20f * W, lo.y + 0.60f * H, frontZ);
    const glm::vec3 cornerL(centre.x - 0.16f * W, mouth.y, frontZ);
    const glm::vec3 cornerR(centre.x + 0.16f * W, mouth.y, frontZ);

    // --- skeleton: Head (root, at neck) and Jaw (pivot near the ears)
    skeleton = Skeleton{};
    Bone head; head.name = "Head"; head.bindTranslation = glm::vec3(centre.x, lo.y, centre.z);
    skeleton.bones.push_back(head);
    Bone jaw; jaw.name = "Jaw"; jaw.parent = 0;
    jaw.bindTranslation = glm::vec3(0.0f, 0.42f * H, -0.15f * D); // relative to head
    skeleton.bones.push_back(jaw);

    // --- skin weights: jaw influence grows below the jaw pivot, on the front lower face.
    skin.assign(mesh.vertexCount(), VertexInfluence{});
    const float jawY = lo.y + 0.42f * H;
    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        const glm::vec3& p = mesh.positions[i];
        float below = smoothstep(jawY, jawY - 0.22f * H, p.y);              // 0 above pivot -> 1 well below
        float front = smoothstep(centre.z - 0.35f * D, centre.z + 0.15f * D, p.z);
        float upper = 1.0f - smoothstep(mouth.y - 0.04f * H, mouth.y + 0.06f * H, p.y); // upper lip stays on head
        float jw = std::clamp(below * front * std::max(upper, 0.15f), 0.0f, 1.0f);
        skin[i].add(1, jw);
        skin[i].add(0, 1.0f - jw);
        skin[i].normalize();
    }
    // Anatomical parts (when the OBJ has named groups): rigid parts get hard weights so the
    // lower teeth/gums/tongue travel with the jaw and everything else stays on the head.
    const PartInfo parts = detectParts();
    std::vector<char> rigid(mesh.vertexCount(), 0);
    auto hard = [&](int part, int bone) {
        if (part < 0) return;
        for (uint32_t v : mesh.partVertices(part)) { skin[v] = VertexInfluence{}; skin[v].add(bone, 1.0f); skin[v].normalize(); rigid[v] = 1; }
    };
    hard(parts.teethLower, 1); hard(parts.gumsLower, 1); hard(parts.tongue, 1);
    hard(parts.teethUpper, 0); hard(parts.gumsUpper, 0); hard(parts.browL, 0); hard(parts.browR, 0); hard(parts.lashes, 0);
    // Eye bones: one per eyeball part, pivot at the eyeball centre, child of Head. Only the
    // eyeball vertices follow them (lids/lashes stay skinned to the head), which gives look-at.
    if (parts.eyeL >= 0 && parts.eyeR >= 0) {
        auto addEye = [&](const char* name, int part) {
            glm::vec3 elo, ehi; mesh.partBounds(part, elo, ehi);
            Bone e; e.name = name; e.parent = 0; e.bindTranslation = 0.5f * (elo + ehi) - skeleton.bones[0].bindTranslation;
            skeleton.bones.push_back(e);
            hard(part, int(skeleton.bones.size()) - 1);
        };
        addEye(kEyeLBone, parts.eyeL); addEye(kEyeRBone, parts.eyeR);
    } else { hard(parts.eyeL, 0); hard(parts.eyeR, 0); }
    if (parts.any()) {
        // With a real inner mouth the jaw pivot sits at the condyle: level with the ear canal,
        // roughly at the back third of the head.
        glm::vec3 tlo, thi; mesh.partBounds(parts.teethLower >= 0 ? parts.teethLower : parts.face, tlo, thi);
        // pivot: slightly above the lower-teeth top, at ~35% depth from the back of the head
        skeleton.bones[1].bindTranslation = glm::vec3(0.0f, (thi.y + 0.05f * H) - lo.y, (lo.z + 0.35f * D) - centre.z);
        // Re-derive the soft jaw weights from the real pivot: full influence from the chin up to
        // just under the lower lip, fading out towards the pivot height and at the back of the
        // head. Full-bust scans include a neck, which must stay on the head - cut off below the chin.
        const float pivotY = skeleton.bones[1].bindTranslation.y + lo.y;
        const float teethH = std::max(thi.y - tlo.y, 0.01f * H);
        const float chinY = tlo.y - 0.6f * teethH;
        const float lipY = tlo.y + 0.5f * teethH; // upper lip / lower lip parting height
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            if (rigid[i]) continue; // rigid parts already assigned
            const glm::vec3& p = mesh.positions[i];
            float below = smoothstep(lipY + 0.6f * (pivotY - lipY), lipY - 0.3f * teethH, p.y); // 0 mid-cheek -> 1 at the lower lip
            float front = smoothstep(lo.z + 0.30f * D, lo.z + 0.55f * D, p.z);  // ramps in past the ear line
            float keep = smoothstep(chinY - 1.0f * teethH, chinY, p.y);         // neck cutoff
            float jw = std::clamp(below * front * keep, 0.0f, 1.0f);
            skin[i] = VertexInfluence{}; skin[i].add(1, jw); skin[i].add(0, 1.0f - jw); skin[i].normalize();
        }
    }

    // --- procedural blendshapes
    blendShapes.clear();
    auto makeShape = [&](const char* name, auto&& fn) {
        BlendShape bs; bs.name = name;
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            glm::vec3 d = fn(mesh.positions[i]);
            if (glm::dot(d, d) > 1e-14f) { bs.indices.push_back(uint32_t(i)); bs.deltas.push_back(d); }
        }
        blendShapes.push_back(std::move(bs));
    };
    const glm::vec3 mouthR(0.22f * W, 0.12f * H, 0.30f * D);
    const glm::vec3 lipR(0.20f * W, 0.06f * H, 0.25f * D);
    makeShape(shapes::JawOpen, [&](const glm::vec3& p) {
        float f = falloff(p, mouth - glm::vec3(0, 0.06f * H, 0), glm::vec3(0.30f * W, 0.20f * H, 0.35f * D));
        float lower = smoothstep(mouth.y + 0.02f * H, mouth.y - 0.06f * H, p.y);
        return glm::vec3(0.0f, -0.10f * H, -0.02f * D) * f * lower;
    });
    makeShape(shapes::MouthSmile, [&](const glm::vec3& p) {
        float l = falloff(p, cornerL, mouthR), r = falloff(p, cornerR, mouthR);
        return glm::vec3(-0.04f * W, 0.05f * H, 0.01f * D) * l + glm::vec3(0.04f * W, 0.05f * H, 0.01f * D) * r;
    });
    makeShape(shapes::MouthWide, [&](const glm::vec3& p) {
        float l = falloff(p, cornerL, mouthR), r = falloff(p, cornerR, mouthR);
        return glm::vec3(-0.06f * W, 0, -0.01f * D) * l + glm::vec3(0.06f * W, 0, -0.01f * D) * r;
    });
    makeShape(shapes::MouthPucker, [&](const glm::vec3& p) {
        float f = falloff(p, mouth, glm::vec3(0.28f * W, 0.10f * H, 0.30f * D));
        glm::vec3 toCentre = glm::vec3(mouth.x - p.x, 0.0f, 0.0f) * 0.5f;
        return (toCentre + glm::vec3(0, 0, 0.05f * D)) * f;
    });
    makeShape(shapes::MouthFunnel, [&](const glm::vec3& p) {
        float f = falloff(p, mouth, glm::vec3(0.24f * W, 0.12f * H, 0.30f * D));
        float sign = p.y > mouth.y ? 1.0f : -1.0f;
        return (glm::vec3(mouth.x - p.x, 0, 0) * 0.3f + glm::vec3(0, sign * 0.02f * H, 0.04f * D)) * f;
    });
    makeShape(shapes::LipsPress, [&](const glm::vec3& p) {
        float f = falloff(p, mouth, lipR);
        float sign = p.y > mouth.y ? -1.0f : 1.0f;
        return glm::vec3(0, sign * 0.015f * H, -0.01f * D) * f;
    });
    makeShape(shapes::BrowRaise, [&](const glm::vec3& p) {
        glm::vec3 r(0.18f * W, 0.10f * H, 0.30f * D);
        float f = falloff(p, browL, r) + falloff(p, browR, r);
        return glm::vec3(0, 0.04f * H, 0.005f * D) * std::min(f, 1.0f);
    });
    makeShape(shapes::MouthFrown, [&](const glm::vec3& p) {
        float l = falloff(p, cornerL, mouthR), r = falloff(p, cornerR, mouthR);
        return glm::vec3(-0.01f * W, -0.045f * H, 0) * l + glm::vec3(0.01f * W, -0.045f * H, 0) * r;
    });
    makeShape(shapes::BrowDown, [&](const glm::vec3& p) {
        glm::vec3 r(0.18f * W, 0.10f * H, 0.30f * D);
        float l = falloff(p, browL, r), rr = falloff(p, browR, r);
        return glm::vec3(0.015f * W, -0.03f * H, 0) * l + glm::vec3(-0.015f * W, -0.03f * H, 0) * rr; // down and inward
    });
    makeShape(shapes::EyeWide, [&](const glm::vec3& p) {
        glm::vec3 r(0.12f * W, 0.08f * H, 0.25f * D);
        float f = falloff(p, eyeL, r) + falloff(p, eyeR, r);
        float sign = p.y > (eyeL.y) ? 1.0f : -1.0f;
        return glm::vec3(0, sign * 0.015f * H, 0) * std::min(f, 1.0f);
    });
    makeShape(shapes::EyeBlink, [&](const glm::vec3& p) {
        glm::vec3 r(0.12f * W, 0.07f * H, 0.25f * D);
        float f = falloff(p, eyeL, r) + falloff(p, eyeR, r);
        float sign = p.y > (eyeL.y) ? -1.0f : 1.0f;
        return glm::vec3(0, sign * 0.02f * H, -0.01f * D) * std::min(f, 1.0f);
    });

    // --- default control points bound to the shapes / jaw bone
    controlPoints.clear();
    auto surf = [&](const glm::vec3& approx) { return mesh.positions[closestVertex(mesh.positions, approx)]; };
    int cp;
    cp = addControlPoint(surf(mouth - glm::vec3(0, 0.10f * H, 0)), "Chin");        bindToBone(cp, 1);
    cp = addControlPoint(surf(cornerL), "MouthCornerL"); bindToBlendShape(cp, findBlendShape(shapes::MouthSmile), glm::vec3(-0.6f, 0.8f, 0), 0.05f * H);
    cp = addControlPoint(surf(cornerR), "MouthCornerR"); bindToBlendShape(cp, findBlendShape(shapes::MouthSmile), glm::vec3(0.6f, 0.8f, 0), 0.05f * H);
    cp = addControlPoint(surf(mouth), "LipCentre");      bindToBlendShape(cp, findBlendShape(shapes::MouthPucker), glm::vec3(0, 0, 1), 0.05f * D);
    cp = addControlPoint(surf(browL), "BrowL");          bindToBlendShape(cp, findBlendShape(shapes::BrowRaise), glm::vec3(0, 1, 0), 0.04f * H);
    cp = addControlPoint(surf(browR), "BrowR");          bindToBlendShape(cp, findBlendShape(shapes::BrowRaise), glm::vec3(0, 1, 0), 0.04f * H);
    cp = addControlPoint(surf(eyeL), "EyelidL");         bindToBlendShape(cp, findBlendShape(shapes::EyeBlink), glm::vec3(0, -1, 0), 0.02f * H);
    cp = addControlPoint(surf(eyeR), "EyelidR");         bindToBlendShape(cp, findBlendShape(shapes::EyeBlink), glm::vec3(0, -1, 0), 0.02f * H);
}

void Rig::setGaze(float yawDeg, float pitchDeg) {
    glm::quat q = glm::angleAxis(glm::radians(yawDeg), glm::vec3(0, 1, 0)) * glm::angleAxis(glm::radians(-pitchDeg), glm::vec3(1, 0, 0));
    for (const char* n : {kEyeLBone, kEyeRBone}) { int b = skeleton.find(n); if (b >= 0) skeleton.bones[size_t(b)].poseRotation = q; }
}

void Rig::lookAt(const glm::vec3& target) {
    auto W = skeleton.bindWorldMatrices();
    for (const char* n : {kEyeLBone, kEyeRBone}) {
        int b = skeleton.find(n); if (b < 0) continue;
        glm::vec3 centre(W[size_t(b)][3]);
        glm::vec3 d = target - centre; if (glm::dot(d, d) < 1e-12f) continue;
        d = glm::normalize(d);
        float yaw = std::atan2(d.x, d.z), pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
        yaw = std::clamp(yaw, glm::radians(-35.0f), glm::radians(35.0f)); pitch = std::clamp(pitch, glm::radians(-25.0f), glm::radians(25.0f));
        skeleton.bones[size_t(b)].poseRotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(-pitch, glm::vec3(1, 0, 0));
    }
}

Rig::PartInfo Rig::detectParts() const {
    PartInfo p;
    auto find = [&](std::initializer_list<const char*> names) { for (auto n : names) { int i = mesh.findPart(n); if (i >= 0) return i; } return -1; };
    p.face = find({"Face", "face", "Head", "head", "skin"});
    p.browL = find({"EyebrowL", "eyebrow_L", "BrowL", "brow_L"});
    p.browR = find({"EyebrowR", "eyebrow_R", "BrowR", "brow_R"});
    p.eyeL = find({"EyeL", "eye_L", "EyeballL", "LeftEye"});
    p.eyeR = find({"EyeR", "eye_R", "EyeballR", "RightEye"});
    p.teethUpper = find({"TeethUpper", "UpperTeeth", "teeth_upper"});
    p.teethLower = find({"TeethLower", "LowerTeeth", "teeth_lower"});
    p.gumsUpper = find({"GumsUpper", "UpperGums"});
    p.gumsLower = find({"GumsLower", "LowerGums"});
    p.tongue = find({"Tongue", "tongue"});
    p.lashes = find({"Eyelashes", "eyelashes", "Lashes"});
    return p;
}

int Rig::installAuthoredBlendShapes(const std::vector<BlendShape>& authored) {
    // Merge into canonical shapes first (dense accumulate), then append all source shapes.
    const size_t n = mesh.vertexCount();
    std::map<std::string, std::vector<glm::vec3>> canon;
    std::map<std::string, std::vector<std::string>> aliasesOf;
    for (const auto& a : authored) {
        std::string c = canonicalShapeName(a.name);
        if (c.empty()) continue;
        auto& dense = canon[c]; if (dense.empty()) dense.assign(n, glm::vec3(0.0f));
        aliasesOf[c].push_back(a.name);
        for (size_t k = 0; k < a.indices.size(); ++k) if (a.indices[k] < n) dense[a.indices[k]] += a.deltas[k];
    }
    // Shapes that combine L+R halves were summed - that is what we want (symmetric drive).
    // BrowRaise merges inner+outer per side; halve to keep the peak displacement sane.
    if (canon.count(shapes::BrowRaise)) for (auto& d : canon[shapes::BrowRaise]) d *= 0.5f;
    std::vector<BlendShape> shapesOut;
    int covered = 0;
    for (const char* name : shapes::All) {
        BlendShape bs; bs.name = name;
        auto it = canon.find(name);
        if (it != canon.end()) {
            ++covered; bs.aliases = aliasesOf[name];
            for (size_t i = 0; i < n; ++i) if (glm::dot(it->second[i], it->second[i]) > 1e-14f) { bs.indices.push_back(uint32_t(i)); bs.deltas.push_back(it->second[i]); }
        } else {
            int old = findBlendShape(name);
            if (old >= 0) bs = blendShapes[size_t(old)]; // keep the procedural fallback
        }
        shapesOut.push_back(std::move(bs));
    }
    for (const auto& a : authored) { shapesOut.push_back(a); shapesOut.back().weight = 0.0f; }
    blendShapes = std::move(shapesOut);
    // Rebind control points to the new indices (names are stable).
    for (auto& cp : controlPoints) if (cp.binding == BindingType::BlendShape) {
        // The default CPs bind to canonical shapes whose indices are unchanged (same order as shapes::All).
        if (cp.target >= int(blendShapes.size())) cp.binding = BindingType::Unbound;
    }
    return covered;
}

} // namespace fr
