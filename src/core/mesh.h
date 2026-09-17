#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace fr {

/// A named contiguous range of triangles (OBJ `g`/`o` group) - e.g. "EyebrowL", "TeethLower".
struct MeshPart {
    std::string name;
    uint32_t firstIndex = 0;   ///< offset into Mesh::indices
    uint32_t indexCount = 0;   ///< multiple of 3
};

/// Triangle mesh with per-vertex attributes. Indices are triangles (3 per face).
struct Mesh {
    std::string name = "mesh";
    std::vector<MeshPart> parts;        ///< optional; empty means a single unnamed part
    std::vector<uint32_t> sourceVertex; ///< optional: OBJ `v` index each vertex came from (vertices are split on uv/normal seams)
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
    int findPart(const std::string& name) const;
    /// Vertex indices used by a part (unique, sorted); all vertices when part < 0.
    std::vector<uint32_t> partVertices(int part) const;
    /// Bounds of a part (or the whole mesh when part < 0).
    void partBounds(int part, glm::vec3& lo, glm::vec3& hi) const;
    /// Uniformly rescales and recentres so the mesh fits in a unit-ish box (useful for arbitrary imports).
    void normalizeToUnit();
    /// Rotates a Z-up mesh (common for scanned/archaeological data) into the app's Y-up frame.
    void zUpToYUp();
    /// Heuristic: true when the mesh looks Z-up (Z extent clearly dominates Y).
    bool looksZUp() const;
};

/// A simple procedural head-like mesh (UV sphere squashed into a face shape) used
/// when no model is supplied, and by tests.
Mesh makeProceduralHead(int rings = 32, int segments = 48);

} // namespace fr
