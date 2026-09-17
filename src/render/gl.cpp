#include "render/gl.h"
#include <cstring>
#define FR_DEF(ret, name, ...) PFN_##name name = nullptr;
FR_GL_FUNCS(FR_DEF)
FR_GL_OPTIONAL_FUNCS(FR_DEF)
#undef FR_DEF
namespace fr {
static bool g_isES = false;
bool loadGL(void* (*getProc)(const char*)) {
    bool ok = true;
#define FR_LOAD(ret, name, ...) name = reinterpret_cast<PFN_##name>(getProc(#name)); if (!name) ok = false;
    FR_GL_FUNCS(FR_LOAD)
#undef FR_LOAD
#define FR_LOAD_OPT(ret, name, ...) name = reinterpret_cast<PFN_##name>(getProc(#name));
    FR_GL_OPTIONAL_FUNCS(FR_LOAD_OPT)
#undef FR_LOAD_OPT
    if (glGetString) {
        const char* v = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        g_isES = v && std::strncmp(v, "OpenGL ES", 9) == 0;
    }
    return ok;
}
bool glIsES() { return g_isES; }
const char* glslVersionLine() {
    return g_isES ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
                  : "#version 330 core\n";
}
} // namespace fr
