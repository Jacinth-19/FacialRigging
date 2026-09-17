#include "render/gl.h"
#define FR_DEF(ret, name, ...) PFN_##name name = nullptr;
FR_GL_FUNCS(FR_DEF)
#undef FR_DEF
namespace fr {
bool loadGL(void* (*getProc)(const char*)) {
    bool ok = true;
#define FR_LOAD(ret, name, ...) name = reinterpret_cast<PFN_##name>(getProc(#name)); if (!name) ok = false;
    FR_GL_FUNCS(FR_LOAD)
#undef FR_LOAD
    return ok;
}
} // namespace fr
