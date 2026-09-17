#include "core/raycast.h"
#include <limits>

namespace fr {

std::optional<float> intersectTriangle(const Ray& r, const glm::vec3& a, const glm::vec3& b,
                                       const glm::vec3& c, glm::vec3* bary, bool cullBackfaces) {
    const float eps = 1e-7f;
    glm::vec3 e1 = b - a, e2 = c - a;
    glm::vec3 p = glm::cross(r.dir, e2);
    float det = glm::dot(e1, p);
    if (cullBackfaces ? det < eps : std::abs(det) < eps) return std::nullopt;
    float inv = 1.0f / det;
    glm::vec3 tv = r.origin - a;
    float u = glm::dot(tv, p) * inv;
    if (u < 0.0f || u > 1.0f) return std::nullopt;
    glm::vec3 q = glm::cross(tv, e1);
    float v = glm::dot(r.dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return std::nullopt;
    float t = glm::dot(e2, q) * inv;
    if (t < 0.0f) return std::nullopt;
    if (bary) *bary = glm::vec3(1.0f - u - v, u, v);
    return t;
}

std::optional<RayHit> raycastMesh(const Ray& r, const Mesh& mesh,
                                  const std::vector<glm::vec3>* positionsOverride, bool cullBackfaces) {
    const auto& P = positionsOverride ? *positionsOverride : mesh.positions;
    std::optional<RayHit> best;
    float bestT = std::numeric_limits<float>::max();
    const auto& I = mesh.indices;
    for (size_t i = 0; i + 2 < I.size(); i += 3) {
        const glm::vec3 &a = P[I[i]], &b = P[I[i + 1]], &c = P[I[i + 2]];
        glm::vec3 bary;
        auto t = intersectTriangle(r, a, b, c, &bary, cullBackfaces);
        if (t && *t < bestT) {
            bestT = *t;
            RayHit h;
            h.t = *t;
            h.point = r.origin + r.dir * *t;
            h.normal = glm::normalize(glm::cross(b - a, c - a));
            h.triangle = uint32_t(i / 3);
            h.bary = bary;
            best = h;
        }
    }
    return best;
}

Ray screenPointToRay(float px, float py, float w, float h, const glm::mat4& view, const glm::mat4& proj) {
    float x = (2.0f * px) / w - 1.0f;
    float y = 1.0f - (2.0f * py) / h;
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 nearP = invVP * glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 farP = invVP * glm::vec4(x, y, 1.0f, 1.0f);
    nearP /= nearP.w; farP /= farP.w;
    return Ray{glm::vec3(nearP), glm::normalize(glm::vec3(farP - nearP))};
}

uint32_t closestVertex(const std::vector<glm::vec3>& positions, const glm::vec3& p) {
    uint32_t best = 0; float bd = std::numeric_limits<float>::max();
    for (size_t i = 0; i < positions.size(); ++i) {
        float d = glm::dot(positions[i] - p, positions[i] - p);
        if (d < bd) { bd = d; best = uint32_t(i); }
    }
    return best;
}

} // namespace fr
