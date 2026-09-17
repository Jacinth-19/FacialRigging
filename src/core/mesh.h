#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace fr {

/// Triangle mesh with per-vertex attributes. Indices are triangles (3 per face).
struct Mesh {
    std::string name = "mesh";
    std::vector<glm::vec3> positions;   ///< bind-pose positions
    std::vector<glm::vec3> normals;     ///< per-vertex normals (recomputed on demand)
    std::vector<glm::vec2> uvs;         ///< optional, same size as positions or empty
    std::vector<uint32_t> indices;      ///< triangle list

    size_t vertexCount() const { return positions.size(); }
    size_t triangleCount() const { return indices.size() / 3; }

    /// Recomputes smooth per-vertex normals from the given positions (defaults to bind pose).
    void recomputeNormals();
    static std::vector<glm::vec3> computeNormals(const std::vector<glm::vec3>& pos,
                                                 const std::vector<uint32_t>& idx);

    glm::vec3 boundsMin() const;
    glm::vec3 boundsMax() const;
    /// Uniformly rescales and recentres so the mesh fits in a unit-ish box (useful for arbitrary imports).
    void normalizeToUnit();
};

/// A simple procedural head-like mesh (UV sphere squashed into a face shape) used
/// when no model is supplied, and by tests.
Mesh makeProceduralHead(int rings = 32, int segments = 48);

} // namespace fr
