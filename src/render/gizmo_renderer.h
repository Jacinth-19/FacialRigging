#pragma once
#include "render/gl.h"
#include "render/shader.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace fr {

/// Immediate-style line/point batch for control point handles, bones and axis gizmos.
class GizmoRenderer {
public:
    ~GizmoRenderer();
    bool init(const std::string& shaderDir, std::string* log);
    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);
    void point(const glm::vec3& p, const glm::vec4& color);
    void axes(const glm::vec3& origin, float size, float alpha = 1.0f);
    void circle(const glm::vec3& centre, const glm::vec3& normal, float radius, const glm::vec4& color, int segs = 32);
    void flush(const glm::mat4& viewProj, float pointSize, bool depthTest);
private:
    struct V { glm::vec3 p; glm::vec4 c; };
    std::vector<V> lines_, points_;
    Shader shader_;
    GLuint vao_ = 0, vbo_ = 0;
};

} // namespace fr
