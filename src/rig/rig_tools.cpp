#include "rig/rig_tools.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace fr {

// ------------------------------------------------------------------ adjacency / mirror
void VertexAdjacency::build(const Mesh& mesh) {
    const size_t n = mesh.vertexCount();
    neighbours.assign(n, {});
    // seam-joined groups: vertices that share a source vertex are treated as one for adjacency
    std::vector<std::vector<uint32_t>> group;
    std::vector<uint32_t> groupOf(n);
    if (mesh.sourceVertex.size() == n) {
        std::unordered_map<uint32_t, uint32_t> id;
        for (size_t i = 0; i < n; ++i) {
            auto it = id.find(mesh.sourceVertex[i]);
            if (it == id.end()) { it = id.emplace(mesh.sourceVertex[i], uint32_t(group.size())).first; group.push_back({}); }
            groupOf[i] = it->second; group[it->second].push_back(uint32_t(i));
        }
    } else { group.resize(n); for (size_t i = 0; i < n; ++i) { groupOf[i] = uint32_t(i); group[i] = {uint32_t(i)}; } }
    auto link = [&](uint32_t a, uint32_t b) { if (a == b) return; for (uint32_t x : group[groupOf[a]]) for (uint32_t y : group[groupOf[b]]) { neighbours[x].push_back(y); neighbours[y].push_back(x); } };
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        uint32_t a = mesh.indices[t], b = mesh.indices[t + 1], c = mesh.indices[t + 2];
        if (a >= n || b >= n || c >= n) continue;
        link(a, b); link(b, c); link(c, a);
    }
    // seam twins are neighbours of each other too (so smoothing keeps them equal)
    for (const auto& g : group) if (g.size() > 1) for (uint32_t x : g) for (uint32_t y : g) if (x != y) neighbours[x].push_back(y);
    for (auto& nb : neighbours) { std::sort(nb.begin(), nb.end()); nb.erase(std::unique(nb.begin(), nb.end()), nb.end()); }
}

void MirrorMap::build(const Mesh& mesh, float toleranceFraction) {
    const size_t n = mesh.vertexCount();
    partner.assign(n, -1);
    if (n == 0) return;
    glm::vec3 lo = mesh.boundsMin(), hi = mesh.boundsMax();
    tolerance = std::max(1e-6f, (hi.y - lo.y) * toleranceFraction);
    // hash grid on cell = tolerance
    const float cell = tolerance;
    auto key = [&](const glm::vec3& p) { return std::make_tuple(int(std::floor(p.x / cell)), int(std::floor(p.y / cell)), int(std::floor(p.z / cell))); };
    std::map<std::tuple<int, int, int>, std::vector<uint32_t>> grid;
    for (size_t i = 0; i < n; ++i) grid[key(mesh.positions[i])].push_back(uint32_t(i));
    const float tol2 = tolerance * tolerance;
    for (size_t i = 0; i < n; ++i) {
        glm::vec3 m = mesh.positions[i] * glm::vec3(-1, 1, 1);
        auto [kx, ky, kz] = key(m);
        int best = -1; float bestD = tol2;
        for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = -1; dz <= 1; ++dz) {
            auto it = grid.find(std::make_tuple(kx + dx, ky + dy, kz + dz)); if (it == grid.end()) continue;
            for (uint32_t j : it->second) { float d = glm::dot(mesh.positions[j] - m, mesh.positions[j] - m); if (d < bestD) { bestD = d; best = int(j); } }
        }
        partner[i] = best;
    }
}

// ------------------------------------------------------------------ weights
float boneWeightOf(const VertexInfluence& inf, int bone) {
    float w = 0.0f; for (int k = 0; k < 4; ++k) if (inf.bones[k] == bone) w += inf.weights[k]; return w;
}

int mirrorBone(const Skeleton& skeleton, int bone) {
    if (bone < 0 || size_t(bone) >= skeleton.bones.size()) return bone;
    const std::string& n = skeleton.bones[size_t(bone)].name;
    auto swapped = [&](const std::string& s) -> std::string {
        if (s.size() >= 2 && s.compare(s.size() - 2, 2, "_L") == 0) return s.substr(0, s.size() - 2) + "_R";
        if (s.size() >= 2 && s.compare(s.size() - 2, 2, "_R") == 0) return s.substr(0, s.size() - 2) + "_L";
        if (s.size() >= 2 && s.back() == 'L') return s.substr(0, s.size() - 1) + "R";
        if (s.size() >= 2 && s.back() == 'R') return s.substr(0, s.size() - 1) + "L";
        return "";
    }(n);
    if (swapped.empty()) return bone;
    int other = skeleton.find(swapped);
    return other >= 0 ? other : bone;
}

namespace {
/// Sets the weight of `bone` on a vertex to `target`, scaling the other influences so the sum stays 1.
void setBoneWeight(VertexInfluence& inf, int bone, float target) {
    target = std::clamp(target, 0.0f, 1.0f);
    float cur = boneWeightOf(inf, bone);
    float others = 0.0f; for (int k = 0; k < 4; ++k) if (inf.bones[k] != bone) others += inf.weights[k];
    // collapse duplicate slots of `bone` into one
    int slot = -1;
    for (int k = 0; k < 4; ++k) if (inf.bones[k] == bone) { if (slot < 0) slot = k; else inf.weights[k] = 0.0f; }
    if (slot < 0) {
        // take the smallest slot
        slot = 0; for (int k = 1; k < 4; ++k) if (inf.weights[k] < inf.weights[slot]) slot = k;
        others -= inf.weights[slot]; inf.bones[slot] = bone; inf.weights[slot] = 0.0f; cur = 0.0f;
    }
    float scale = others > 1e-9f ? (1.0f - target) / others : 0.0f;
    for (int k = 0; k < 4; ++k) if (k != slot && inf.bones[k] != bone) inf.weights[k] *= scale;
    inf.weights[slot] = target;
    if (others <= 1e-9f && target < 1.0f) {
        // no other influence to give the remainder to: keep the vertex fully on `bone`'s nearest
        // alternative (bone 0 = root) so the sum stays 1
        int alt = 0; for (int k = 0; k < 4; ++k) if (k != slot) { alt = k; break; }
        inf.bones[alt] = inf.bones[alt] == bone ? 0 : inf.bones[alt]; inf.weights[alt] = 1.0f - target;
    }
    (void)cur;
}
} // namespace

int paintWeights(Rig& rig, const WeightBrush& brush, const glm::vec3& centre, const glm::vec3& viewDir,
                 const VertexAdjacency* adjacency, const MirrorMap* mirror) {
    if (!rig.hasSkin() || brush.bone < 0 || size_t(brush.bone) >= rig.skeleton.bones.size()) return 0;
    const Mesh& m = rig.mesh; const size_t n = m.vertexCount();
    const float r2 = brush.radius * brush.radius;
    const bool useNormal = brush.frontFacingOnly && glm::dot(viewDir, viewDir) > 1e-9f && m.normals.size() == n;
    auto dab = [&](const glm::vec3& c, int bone) {
        int changed = 0;
        std::vector<float> before; if (brush.mode == WeightBrushMode::Smooth && adjacency) { before.resize(n); for (size_t i = 0; i < n; ++i) before[i] = boneWeightOf(rig.skin[i], bone); }
        for (size_t i = 0; i < n; ++i) {
            glm::vec3 d = m.positions[i] - c; float d2 = glm::dot(d, d); if (d2 > r2) continue;
            if (useNormal && glm::dot(m.normals[i], viewDir) > 0.15f) continue;   // back-facing
            float t = std::sqrt(d2) / brush.radius;
            float fall = brush.falloff <= 0.0f ? 1.0f : (1.0f - t * t * (3.0f - 2.0f * t)) * brush.falloff + (1.0f - brush.falloff);
            float s = std::clamp(brush.strength * fall, 0.0f, 1.0f);
            float w = boneWeightOf(rig.skin[i], bone), target = w;
            switch (brush.mode) {
            case WeightBrushMode::Add: target = w + s; break;
            case WeightBrushMode::Subtract: target = w - s; break;
            case WeightBrushMode::Replace: target = w + (brush.value - w) * s; break;
            case WeightBrushMode::Smooth: {
                if (!adjacency || adjacency->neighbours[i].empty()) break;
                float sum = 0; for (uint32_t j : adjacency->neighbours[i]) sum += before[j];
                float avg = sum / float(adjacency->neighbours[i].size());
                target = w + (avg - w) * s; break; }
            }
            target = std::clamp(target, 0.0f, 1.0f);
            if (std::abs(target - w) < 1e-6f) continue;
            setBoneWeight(rig.skin[i], bone, target); ++changed;
        }
        return changed;
    };
    int changed = dab(centre, brush.bone);
    if (brush.symmetric && std::abs(centre.x) > 1e-5f) changed += dab(centre * glm::vec3(-1, 1, 1), mirrorBone(rig.skeleton, brush.bone));
    (void)mirror;
    return changed;
}

int mirrorWeights(Rig& rig, bool fromPositiveX, const MirrorMap& mirror) {
    if (!rig.hasSkin() || mirror.partner.size() != rig.mesh.vertexCount()) return 0;
    std::vector<int> boneMap(rig.skeleton.bones.size()); for (size_t b = 0; b < boneMap.size(); ++b) boneMap[b] = mirrorBone(rig.skeleton, int(b));
    std::vector<VertexInfluence> src = rig.skin; int written = 0;
    for (size_t i = 0; i < src.size(); ++i) {
        float x = rig.mesh.positions[i].x;
        bool isSource = fromPositiveX ? x > 0 : x < 0;
        if (!isSource) continue;
        int j = mirror.partner[i]; if (j < 0 || size_t(j) == i) continue;
        VertexInfluence inf = src[i];
        for (int k = 0; k < 4; ++k) inf.bones[k] = boneMap[size_t(std::clamp(inf.bones[k], 0, int(boneMap.size()) - 1))];
        rig.skin[size_t(j)] = inf; ++written;
    }
    return written;
}

void cleanWeights(Rig& rig, float epsilon) {
    for (auto& inf : rig.skin) { for (int k = 0; k < 4; ++k) if (inf.weights[k] < epsilon) inf.weights[k] = 0.0f; inf.normalize(); if (inf.weights.x + inf.weights.y + inf.weights.z + inf.weights.w < 0.5f) { inf.bones = glm::ivec4(0); inf.weights = glm::vec4(1, 0, 0, 0); } }
}

// ------------------------------------------------------------------ bake
namespace {
std::vector<glm::vec3> evaluateSelected(const Rig& rig, bool skin, bool shapes, bool freeForm) {
    Rig tmp = rig;
    if (!skin) tmp.skeleton.resetPose();
    if (!shapes) for (auto& bs : tmp.blendShapes) bs.weight = 0.0f;
    if (!freeForm) for (auto& c : tmp.controlPoints) if (c.binding == BindingType::FreeForm) c.offset = glm::vec3(0);
    return tmp.evaluate();
}
BlendShape sparsify(const std::string& name, const std::vector<glm::vec3>& delta, float minDelta) {
    BlendShape bs; bs.name = name;
    for (size_t i = 0; i < delta.size(); ++i) if (glm::length(delta[i]) >= minDelta) { bs.indices.push_back(uint32_t(i)); bs.deltas.push_back(delta[i]); }
    return bs;
}
} // namespace

int bakePoseAsBlendShape(Rig& rig, const BakeShapeOptions& opts, const MirrorMap* mirror) {
    const size_t n = rig.mesh.vertexCount(); if (n == 0) return -1;
    std::vector<glm::vec3> full = evaluateSelected(rig, opts.includeSkin, opts.includeBlendShapes, opts.includeFreeForm);
    std::vector<glm::vec3> base;
    if (opts.subtractExisting) base = evaluateSelected(rig, opts.includeSkin, opts.includeBlendShapes, false);   // existing shapes' contribution
    else base = rig.mesh.positions;
    std::vector<glm::vec3> delta(n);
    for (size_t i = 0; i < n; ++i) delta[i] = full[i] - base[i];
    // when subtractExisting is on but no free-form handle moved, the residual is zero: fall back to the
    // total deformation so "bake current pose" still gives the user something.
    bool any = false; for (const auto& d : delta) if (glm::dot(d, d) >= opts.minDelta * opts.minDelta) { any = true; break; }
    if (!any && opts.subtractExisting && opts.fallbackToTotal) { for (size_t i = 0; i < n; ++i) delta[i] = full[i] - rig.mesh.positions[i]; for (const auto& d : delta) if (glm::dot(d, d) >= opts.minDelta * opts.minDelta) { any = true; break; } }
    if (!any) return -1;
    int first = -1;
    if (opts.mirrorToOtherSide && mirror && mirror->partner.size() == n) {
        // split: left = x>0 part, right = mirrored copy of it
        std::vector<glm::vec3> left(n, glm::vec3(0)), right(n, glm::vec3(0));
        for (size_t i = 0; i < n; ++i) if (rig.mesh.positions[i].x >= 0.0f) left[i] = delta[i];
        for (size_t i = 0; i < n; ++i) { int j = mirror->partner[i]; if (j >= 0 && rig.mesh.positions[i].x >= 0.0f) right[size_t(j)] = left[i] * glm::vec3(-1, 1, 1); }
        BlendShape l = sparsify(opts.name + "_L", left, opts.minDelta), r = sparsify(opts.name + "_R", right, opts.minDelta);
        first = int(rig.blendShapes.size()); rig.blendShapes.push_back(std::move(l)); rig.blendShapes.push_back(std::move(r));
    } else {
        first = int(rig.blendShapes.size()); rig.blendShapes.push_back(sparsify(opts.name, delta, opts.minDelta));
    }
    if (opts.includeFreeForm) for (auto& c : rig.controlPoints) if (c.binding == BindingType::FreeForm) c.offset = glm::vec3(0);
    return first;
}

// ------------------------------------------------------------------ combination shapes
int bakeCorrectiveShape(Rig& rig, const std::string& driverA, const std::string& driverB,
                        const std::string& name, const MirrorMap* mirror) {
    int a = rig.findBlendShape(driverA), b = rig.findBlendShape(driverB);
    if (a < 0 || b < 0 || a == b) return -1;
    // an older corrective of the same name must not be part of the "existing" baseline
    int old = rig.findBlendShape(name);
    if (old >= 0) rig.blendShapes[size_t(old)].weight = 0.0f;
    BakeShapeOptions o; o.name = name; o.includeBlendShapes = true; o.includeFreeForm = true; o.includeSkin = false; o.subtractExisting = true; o.fallbackToTotal = false;
    int idx = bakePoseAsBlendShape(rig, o, mirror);
    if (idx < 0) return -1;
    float wa = rig.blendWeight(driverA), wb = rig.blendWeight(driverB);
    CombinationShape c{name, driverA, driverB, 1.0f, false};
    float atNow = Rig::combinationWeight(c, wa, wb);
    if (atNow > 1e-4f && atNow < 1.0f) for (auto& d : rig.blendShapes[size_t(idx)].deltas) d /= atNow;
    if (old >= 0) {   // replace in place, drop the appended duplicate
        rig.blendShapes[size_t(old)].indices = std::move(rig.blendShapes[size_t(idx)].indices);
        rig.blendShapes[size_t(old)].deltas = std::move(rig.blendShapes[size_t(idx)].deltas);
        rig.blendShapes.pop_back(); idx = old;
    }
    int existing = rig.findCombination(name);
    if (existing >= 0) rig.combinations[size_t(existing)] = c; else rig.combinations.push_back(c);
    rig.applyCombinations();
    return idx;
}

} // namespace fr
