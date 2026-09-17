#include "render/shader.h"
#include <fstream>
#include <sstream>

namespace fr {

Shader::~Shader() { if (program_) glDeleteProgram(program_); }

static GLuint compileStage(GLenum type, const std::string& src, std::string* log) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::string buf(size_t(len > 1 ? len : 1), '\0');
        glGetShaderInfoLog(s, len, nullptr, buf.data());
        if (log) *log += (type == GL_VERTEX_SHADER ? "vertex: " : "fragment: ") + buf + "\n";
        glDeleteShader(s); return 0;
    }
    return s;
}

bool Shader::compile(const std::string& vs, const std::string& fs, std::string* log) {
    GLuint v = compileStage(GL_VERTEX_SHADER, vs, log); if (!v) return false;
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs, log); if (!f) { glDeleteShader(v); return false; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::string buf(size_t(len > 1 ? len : 1), '\0');
        glGetProgramInfoLog(p, len, nullptr, buf.data());
        if (log) *log += "link: " + buf + "\n";
        glDeleteProgram(p); return false;
    }
    if (program_) glDeleteProgram(program_);
    program_ = p;
    return true;
}

bool Shader::loadFromFiles(const std::string& vp, const std::string& fp, std::string* log) {
    auto read = [&](const std::string& p, std::string& out) { std::ifstream f(p); if (!f) { if (log) *log += "cannot read " + p + "\n"; return false; } std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true; };
    std::string v, f;
    if (!read(vp, v) || !read(fp, f)) return false;
    return compile(v, f, log);
}

GLint Shader::loc(const char* name) const { return glGetUniformLocation(program_, name); }

} // namespace fr
