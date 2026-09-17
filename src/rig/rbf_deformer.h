#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace fr {

/// Radial-basis-function space warp driven by a set of handle displacements.
/// Solves  sum_j w_j * phi(|c_i - c_j|) = d_i  for the weights w_j (one per handle),
/// then evaluates the displacement field at arbitrary points. A compact-support
/// Wendland kernel keeps the influence local (radius per handle).
class RbfDeformer {
public:
    struct Handle { glm::vec3 center; glm::vec3 displacement; float radius; };

    void setHandles(const std::vector<Handle>& handles);
    bool empty() const { return handles_.empty(); }
    glm::vec3 displacementAt(const glm::vec3& p) const;
    /// Adds the displacement field to every position in-place.
    void apply(std::vector<glm::vec3>& positions) const;

    static float kernel(float r, float radius); ///< Wendland C2, 1 at r=0, 0 at r>=radius

private:
    std::vector<Handle> handles_;
    std::vector<glm::vec3> weights_;
    void solve();
};

} // namespace fr
