#pragma once
#include "render/gl.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace fr {

class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete; Shader& operator=(const Shader&) = delete;
    bool compile(const std::string& vertexSrc, const std::string& fragmentSrc, std::string* log = nullptr);
    bool loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath, std::string* log = nullptr);
    void use() const { glUseProgram(program_); }
    GLuint id() const { return program_; }
    GLint loc(const char* name) const;
    void set(const char* n, int v) const { glUniform1i(loc(n), v); }
    void set(const char* n, float v) const { glUniform1f(loc(n), v); }
    void set(const char* n, const glm::vec3& v) const { glUniform3fv(loc(n), 1, &v.x); }
    void set(const char* n, const glm::vec4& v) const { glUniform4fv(loc(n), 1, &v.x); }
    void set(const char* n, const glm::mat4& m) const { glUniformMatrix4fv(loc(n), 1, GL_FALSE, &m[0][0]); }
    void set(const char* n, const std::vector<glm::mat4>& m) const { if (!m.empty()) glUniformMatrix4fv(loc(n), GLsizei(m.size()), GL_FALSE, &m[0][0][0]); }
    void set(const char* n, const std::vector<float>& f) const { if (!f.empty()) glUniform1fv(loc(n), GLsizei(f.size()), f.data()); }
private:
    GLuint program_ = 0;
};

} // namespace fr
