// Compiles Dear ImGui's OpenGL3 backend against the app's own GL function table so it runs on
// both desktop GL 3.3 and OpenGL ES 3.0 (EGL/SwiftShader/Mesa) contexts without system GL headers.
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM 1
#include "render/imgui_gl_loader.h"
#include <backends/imgui_impl_opengl3.cpp>
