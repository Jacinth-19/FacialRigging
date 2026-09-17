#pragma once
// Custom loader shim for Dear ImGui's OpenGL3 backend (IMGUI_IMPL_OPENGL_LOADER_CUSTOM):
// reuses the app's own function-pointer table so ImGui works on desktop GL 3.3 and OpenGL ES
// 3.0 alike (no libGL.so / GLES3 headers needed on the build machine). GL_VERSION_3_1/3_2 are
// deliberately left undefined so the backend does not use primitive restart / base-vertex draws.
#include "render/gl.h"
#define GL_VERSION_3_3 1
