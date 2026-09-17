#include "render/shader.h"
#include <fstream>
#include <sstream>

namespace fr {

Shader::~Shader() { if (program_) glDeleteProgram(program_); }

static std::string withVersion(const std::string& src) {
    // Shader files carry no #version line (any present one is dropped) so the same GLSL
    // compiles on desktop 3.3 core and ES 3.0; the right header is prepended here.
    std::string body = src;
    size_t v = 0;
    while ((v = body.find("#version", v)) != std::string::npos) {
        size_t bol = body.rfind('\n', v); bol = bol == std::string::npos ? 0 : bol + 1;
        bool atLineStart = body.find_first_not_of(" \t", bol) == v;
        if (!atLineStart) { v += 8; continue; }
        auto eol = body.find('\n', v);
        body.erase(bol, eol == std::string::npos ? std::string::npos : eol - bol + 1);
        break;
    }
    return std::string(glslVersionLine()) + body;
}

static GLuint compileStage(GLenum type, const std::string& srcIn, std::string* log) {
    GLuint s = glCreateShader(type);
    std::string src = withVersion(srcIn);
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
