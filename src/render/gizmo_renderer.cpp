#include "render/gizmo_renderer.h"
#include <glm/gtc/constants.hpp>

namespace fr {

GizmoRenderer::~GizmoRenderer() { if (vao_) glDeleteVertexArrays(1, &vao_); if (vbo_) glDeleteBuffers(1, &vbo_); }

bool GizmoRenderer::init(const std::string& dir, std::string* log) {
    if (!shader_.loadFromFiles(dir + "/gizmo.vert", dir + "/gizmo.frag", log)) return false;
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void*>(sizeof(glm::vec3)));
    glBindVertexArray(0);
    return true;
}

void GizmoRenderer::line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& c) { lines_.push_back({a, c}); lines_.push_back({b, c}); }
void GizmoRenderer::point(const glm::vec3& p, const glm::vec4& c) { points_.push_back({p, c}); }
void GizmoRenderer::axes(const glm::vec3& o, float s, float a) {
    line(o, o + glm::vec3(s, 0, 0), {0.9f, 0.2f, 0.2f, a});
    line(o, o + glm::vec3(0, s, 0), {0.2f, 0.9f, 0.2f, a});
    line(o, o + glm::vec3(0, 0, s), {0.2f, 0.4f, 0.95f, a});
}
void GizmoRenderer::circle(const glm::vec3& c, const glm::vec3& n, float r, const glm::vec4& col, int segs) {
    glm::vec3 nn = glm::normalize(n);
    glm::vec3 u = glm::normalize(glm::cross(nn, std::abs(nn.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    glm::vec3 v = glm::cross(nn, u);
    for (int i = 0; i < segs; ++i) {
        float a0 = glm::two_pi<float>() * i / segs, a1 = glm::two_pi<float>() * (i + 1) / segs;
        line(c + r * (std::cos(a0) * u + std::sin(a0) * v), c + r * (std::cos(a1) * u + std::sin(a1) * v), col);
    }
}

void GizmoRenderer::flush(const glm::mat4& vp, float pointSize, bool depthTest) {
    if (lines_.empty() && points_.empty()) return;
    shader_.use(); shader_.set("u_ViewProj", vp); shader_.set("u_PointSize", pointSize);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (!glIsES()) glEnable(GL_PROGRAM_POINT_SIZE); // always on in ES
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (!lines_.empty()) {
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(lines_.size() * sizeof(V)), lines_.data(), GL_DYNAMIC_DRAW);
        shader_.set("u_Round", 0); glDrawArrays(GL_LINES, 0, GLsizei(lines_.size()));
    }
    if (!points_.empty()) {
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(points_.size() * sizeof(V)), points_.data(), GL_DYNAMIC_DRAW);
        shader_.set("u_Round", 1); glDrawArrays(GL_POINTS, 0, GLsizei(points_.size()));
    }
    glBindVertexArray(0); glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    lines_.clear(); points_.clear();
}

} // namespace fr
