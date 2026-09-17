#include "core/mesh.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <limits>

namespace fr {

std::vector<glm::vec3> Mesh::computeNormals(const std::vector<glm::vec3>& pos,
                                            const std::vector<uint32_t>& idx) {
    std::vector<glm::vec3> n(pos.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const glm::vec3& a = pos[idx[i]];
        const glm::vec3& b = pos[idx[i + 1]];
        const glm::vec3& c = pos[idx[i + 2]];
        glm::vec3 fn = glm::cross(b - a, c - a); // area weighted
        n[idx[i]] += fn; n[idx[i + 1]] += fn; n[idx[i + 2]] += fn;
    }
    for (auto& v : n) {
        float len = glm::length(v);
        v = len > 1e-12f ? v / len : glm::vec3(0, 0, 1);
    }
    return n;
}

void Mesh::recomputeNormals() { normals = computeNormals(positions, indices); }

glm::vec3 Mesh::boundsMin() const {
    glm::vec3 m(std::numeric_limits<float>::max());
    for (auto& p : positions) m = glm::min(m, p);
    return positions.empty() ? glm::vec3(0) : m;
}
glm::vec3 Mesh::boundsMax() const {
    glm::vec3 m(std::numeric_limits<float>::lowest());
    for (auto& p : positions) m = glm::max(m, p);
    return positions.empty() ? glm::vec3(0) : m;
}

void Mesh::normalizeToUnit() {
    if (positions.empty()) return;
    glm::vec3 lo = boundsMin(), hi = boundsMax();
    glm::vec3 c = 0.5f * (lo + hi);
    float ext = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    float s = ext > 1e-9f ? 1.0f / ext : 1.0f;
    for (auto& p : positions) p = (p - c) * s;
}

void Mesh::zUpToYUp() {
    for (auto& p : positions) p = glm::vec3(p.x, p.z, -p.y);
    for (auto& n : normals) n = glm::vec3(n.x, n.z, -n.y);
}

bool Mesh::looksZUp() const {
    if (positions.empty()) return false;
    glm::vec3 e = boundsMax() - boundsMin();
    return e.z > 1.25f * e.y && e.z > e.x;
}

Mesh makeProceduralHead(int rings, int segments) {
    Mesh m;
    m.name = "procedural_head";
    rings = std::max(rings, 3);
    segments = std::max(segments, 3);
    for (int r = 0; r <= rings; ++r) {
        float v = float(r) / float(rings);
        float phi = v * glm::pi<float>();
        for (int s = 0; s <= segments; ++s) {
            float u = float(s) / float(segments);
            float theta = u * glm::two_pi<float>();
            glm::vec3 p(std::sin(phi) * std::sin(theta), std::cos(phi), std::sin(phi) * std::cos(theta));
            // Shape into a face: taller than wide, flatter at the back, gentle chin.
            p.x *= 0.42f;
            p.y *= 0.55f;
            p.z *= (p.z > 0.0f ? 0.45f : 0.38f);
            if (p.y < 0.0f) p.x *= (1.0f + 0.35f * p.y); // narrow towards chin
            m.positions.push_back(p);
            m.uvs.emplace_back(u, 1.0f - v);
        }
    }
    int stride = segments + 1;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            uint32_t a = r * stride + s, b = a + 1, c = a + stride, d = c + 1;
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
    }
    m.recomputeNormals();
    return m;
}

} // namespace fr
