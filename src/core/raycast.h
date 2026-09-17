#pragma once
#include "core/mesh.h"
#include <glm/glm.hpp>
#include <optional>

namespace fr {

struct Ray { glm::vec3 origin; glm::vec3 dir; }; // dir need not be normalised

struct RayHit {
    float t = 0.0f;          ///< distance along ray (in units of |dir|)
    glm::vec3 point{0.0f};   ///< world-space hit position
    glm::vec3 normal{0.0f};  ///< geometric face normal
    uint32_t triangle = 0;   ///< triangle index (indices[3*triangle..])
    glm::vec3 bary{0.0f};    ///< barycentric weights for the 3 triangle vertices
};

/// Möller–Trumbore ray/triangle intersection. Returns t>=0 if hit.
std::optional<float> intersectTriangle(const Ray& r, const glm::vec3& a, const glm::vec3& b,
                                       const glm::vec3& c, glm::vec3* bary = nullptr, bool cullBackfaces = false);

/// Closest hit against a mesh using the provided (possibly deformed) positions.
std::optional<RayHit> raycastMesh(const Ray& r, const Mesh& mesh,
                                  const std::vector<glm::vec3>* positionsOverride = nullptr,
                                  bool cullBackfaces = true);

/// Builds a world-space ray from a pixel position in a viewport of size (w,h).
Ray screenPointToRay(float px, float py, float w, float h,
                     const glm::mat4& view, const glm::mat4& proj);

/// Finds the mesh vertex closest to a point (brute force; fine for face meshes).
uint32_t closestVertex(const std::vector<glm::vec3>& positions, const glm::vec3& p);

} // namespace fr
