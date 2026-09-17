#include "rig/rbf_deformer.h"
#include <cmath>

namespace fr {

float RbfDeformer::kernel(float r, float radius) {
    if (radius <= 0.0f) return 0.0f;
    float q = r / radius;
    if (q >= 1.0f) return 0.0f;
    float t = 1.0f - q;
    float t2 = t * t;
    return t2 * t2 * (4.0f * q + 1.0f);
}

void RbfDeformer::setHandles(const std::vector<Handle>& handles) {
    handles_ = handles;
    solve();
}

// Gaussian elimination with partial pivoting on the small (n x n) kernel matrix, 3 RHS.
void RbfDeformer::solve() {
    size_t n = handles_.size();
    weights_.assign(n, glm::vec3(0.0f));
    if (n == 0) return;
    std::vector<float> A(n * n);
    std::vector<glm::vec3> b(n);
    for (size_t i = 0; i < n; ++i) {
        b[i] = handles_[i].displacement;
        for (size_t j = 0; j < n; ++j) {
            float r = glm::length(handles_[i].center - handles_[j].center);
            A[i * n + j] = kernel(r, handles_[j].radius) + (i == j ? 1e-6f : 0.0f);
        }
    }
    for (size_t col = 0; col < n; ++col) {
        size_t piv = col;
        for (size_t r = col + 1; r < n; ++r)
            if (std::abs(A[r * n + col]) > std::abs(A[piv * n + col])) piv = r;
        if (piv != col) {
            for (size_t k = 0; k < n; ++k) std::swap(A[col * n + k], A[piv * n + k]);
            std::swap(b[col], b[piv]);
        }
        float d = A[col * n + col];
        if (std::abs(d) < 1e-12f) continue;
        for (size_t r = col + 1; r < n; ++r) {
            float f = A[r * n + col] / d;
            if (f == 0.0f) continue;
            for (size_t k = col; k < n; ++k) A[r * n + k] -= f * A[col * n + k];
            b[r] -= f * b[col];
        }
    }
    for (size_t ii = n; ii-- > 0;) {
        glm::vec3 s = b[ii];
        for (size_t k = ii + 1; k < n; ++k) s -= A[ii * n + k] * weights_[k];
        float d = A[ii * n + ii];
        weights_[ii] = std::abs(d) < 1e-12f ? glm::vec3(0.0f) : s / d;
    }
}

glm::vec3 RbfDeformer::displacementAt(const glm::vec3& p) const {
    glm::vec3 d(0.0f);
    for (size_t j = 0; j < handles_.size(); ++j) {
        float k = kernel(glm::length(p - handles_[j].center), handles_[j].radius);
        if (k > 0.0f) d += weights_[j] * k;
    }
    return d;
}

void RbfDeformer::apply(std::vector<glm::vec3>& positions) const {
    if (handles_.empty()) return;
    for (auto& p : positions) p += displacementAt(p);
}

} // namespace fr
