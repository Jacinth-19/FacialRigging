#pragma once
// Tiny self-contained OpenGL loader (subset used by the app + Dear ImGui backend), so the tree
// has no generated GLAD dependency. Function pointers are resolved through glfwGetProcAddress.
// Works for both desktop OpenGL 3.3 core and OpenGL ES 3.0 (EGL / SwiftShader / Mesa) contexts;
// functions that do not exist on ES (glPolygonMode, glTexBuffer, ...) are optional and stay null.
#include <cstddef>
#include <cstdint>

typedef unsigned int GLenum; typedef unsigned char GLboolean; typedef unsigned int GLbitfield;
typedef int GLint; typedef int GLsizei; typedef unsigned int GLuint; typedef float GLfloat; typedef float GLclampf;
typedef char GLchar; typedef std::ptrdiff_t GLsizeiptr; typedef std::ptrdiff_t GLintptr; typedef void GLvoid; typedef unsigned char GLubyte;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_INT 0x1404
#define GL_RGBA 0x1908
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_FRONT_AND_BACK 0x0408
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_LINE_SMOOTH 0x0B20
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_POLYGON_OFFSET_LINE 0x2A02
#define GL_MULTISAMPLE 0x809D
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_TEXTURE_BUFFER 0x8C2A
#define GL_RGB32F 0x8815
#define GL_PROGRAM_POINT_SIZE 0x8642
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_UNSIGNED_SHORT 0x1403
#define GL_RGB 0x1907
#define GL_SCISSOR_TEST 0x0C11
#define GL_STENCIL_TEST 0x0B90
#define GL_FUNC_ADD 0x8006
#define GL_ONE 1
#define GL_ZERO 0
#define GL_BACK 0x0405
#define GL_FRONT 0x0404
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_STREAM_DRAW 0x88E0
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#define GL_NUM_EXTENSIONS 0x821D
#define GL_EXTENSIONS 0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_SAMPLER_BINDING 0x8919
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_VIEWPORT 0x0BA2
#define GL_SCISSOR_BOX 0x0C10
#define GL_BLEND_SRC_RGB 0x80C9
#define GL_BLEND_DST_RGB 0x80C8
#define GL_BLEND_SRC_ALPHA 0x80CB
#define GL_BLEND_DST_ALPHA 0x80CA
#define GL_BLEND_EQUATION_RGB 0x8009
#define GL_BLEND_EQUATION_ALPHA 0x883D
#define GL_POLYGON_MODE 0x0B40
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#define GL_VERTEX_ATTRIB_ARRAY_SIZE 0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE 0x8624
#define GL_VERTEX_ATTRIB_ARRAY_TYPE 0x8625
#define GL_VERTEX_ATTRIB_ARRAY_NORMALIZED 0x886A
#define GL_VERTEX_ATTRIB_ARRAY_POINTER 0x8645
#define GL_CONTEXT_PROFILE_MASK 0x9126
#define GL_CONTEXT_COMPATIBILITY_PROFILE_BIT 0x00000002
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_NO_ERROR 0
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_RGBA8 0x8058
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_MAX_SAMPLES 0x8D57
typedef double GLdouble; typedef short GLshort; typedef unsigned short GLushort;

#ifdef _WIN32
#define FR_APIENTRY __stdcall
#else
#define FR_APIENTRY
#endif

#define FR_GL_FUNCS(X) \
  X(void, glClear, GLbitfield) \
  X(void, glClearColor, GLfloat, GLfloat, GLfloat, GLfloat) \
  X(void, glEnable, GLenum) \
  X(void, glDisable, GLenum) \
  X(void, glDepthFunc, GLenum) \
  X(void, glBlendFunc, GLenum, GLenum) \
  X(void, glViewport, GLint, GLint, GLsizei, GLsizei) \
  X(void, glPolygonOffset, GLfloat, GLfloat) \
  X(void, glLineWidth, GLfloat) \
  X(const GLubyte*, glGetString, GLenum) \
  X(GLenum, glGetError, void) \
  X(void, glGenBuffers, GLsizei, GLuint*) \
  X(void, glDeleteBuffers, GLsizei, const GLuint*) \
  X(void, glBindBuffer, GLenum, GLuint) \
  X(void, glBufferData, GLenum, GLsizeiptr, const void*, GLenum) \
  X(void, glBufferSubData, GLenum, GLintptr, GLsizeiptr, const void*) \
  X(void, glGenVertexArrays, GLsizei, GLuint*) \
  X(void, glDeleteVertexArrays, GLsizei, const GLuint*) \
  X(void, glBindVertexArray, GLuint) \
  X(void, glEnableVertexAttribArray, GLuint) \
  X(void, glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) \
  X(void, glVertexAttribIPointer, GLuint, GLint, GLenum, GLsizei, const void*) \
  X(void, glDrawElements, GLenum, GLsizei, GLenum, const void*) \
  X(void, glDrawArrays, GLenum, GLint, GLsizei) \
  X(GLuint, glCreateShader, GLenum) \
  X(void, glDeleteShader, GLuint) \
  X(void, glShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*) \
  X(void, glCompileShader, GLuint) \
  X(void, glGetShaderiv, GLuint, GLenum, GLint*) \
  X(void, glGetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) \
  X(GLuint, glCreateProgram, void) \
  X(void, glDeleteProgram, GLuint) \
  X(void, glAttachShader, GLuint, GLuint) \
  X(void, glLinkProgram, GLuint) \
  X(void, glGetProgramiv, GLuint, GLenum, GLint*) \
  X(void, glGetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) \
  X(void, glUseProgram, GLuint) \
  X(GLint, glGetUniformLocation, GLuint, const GLchar*) \
  X(void, glUniform1i, GLint, GLint) \
  X(void, glUniform1f, GLint, GLfloat) \
  X(void, glUniform1fv, GLint, GLsizei, const GLfloat*) \
  X(void, glUniform3fv, GLint, GLsizei, const GLfloat*) \
  X(void, glUniform4fv, GLint, GLsizei, const GLfloat*) \
  X(void, glUniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*) \
  X(void, glGenTextures, GLsizei, GLuint*) \
  X(void, glDeleteTextures, GLsizei, const GLuint*) \
  X(void, glBindTexture, GLenum, GLuint) \
  X(void, glActiveTexture, GLenum) \
  X(void, glTexParameteri, GLenum, GLenum, GLint) \
  X(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
  X(void, glPixelStorei, GLenum, GLint) \
  X(void, glReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) \
  X(void, glFinish, void) \
  X(void, glGetIntegerv, GLenum, GLint*) \
  X(const GLubyte*, glGetStringi, GLenum, GLuint) \
  X(GLboolean, glIsEnabled, GLenum) \
  X(GLboolean, glIsProgram, GLuint) \
  X(void, glScissor, GLint, GLint, GLsizei, GLsizei) \
  X(void, glBlendEquation, GLenum) \
  X(void, glBlendEquationSeparate, GLenum, GLenum) \
  X(void, glBlendFuncSeparate, GLenum, GLenum, GLenum, GLenum) \
  X(void, glDetachShader, GLuint, GLuint) \
  X(void, glDisableVertexAttribArray, GLuint) \
  X(GLint, glGetAttribLocation, GLuint, const GLchar*) \
  X(void, glGetVertexAttribiv, GLuint, GLenum, GLint*) \
  X(void, glGetVertexAttribPointerv, GLuint, GLenum, void**) \
  X(void, glGenFramebuffers, GLsizei, GLuint*) \
  X(void, glDeleteFramebuffers, GLsizei, const GLuint*) \
  X(void, glBindFramebuffer, GLenum, GLuint) \
  X(void, glFramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint) \
  X(void, glFramebufferRenderbuffer, GLenum, GLenum, GLenum, GLuint) \
  X(void, glGenRenderbuffers, GLsizei, GLuint*) \
  X(void, glDeleteRenderbuffers, GLsizei, const GLuint*) \
  X(void, glBindRenderbuffer, GLenum, GLuint) \
  X(void, glRenderbufferStorage, GLenum, GLenum, GLsizei, GLsizei) \
  X(GLenum, glCheckFramebufferStatus, GLenum) \
  X(void, glRenderbufferStorageMultisample, GLenum, GLsizei, GLenum, GLsizei, GLsizei) \
  X(void, glBlitFramebuffer, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)

// Desktop-GL-only (or extension) entry points: loaded when available, otherwise null.
#define FR_GL_OPTIONAL_FUNCS(X) \
  X(void, glBindSampler, GLuint, GLuint) \
  X(void, glPolygonMode, GLenum, GLenum) \
  X(void, glTexBuffer, GLenum, GLenum, GLuint) \
  X(void, glPointSize, GLfloat)

#define FR_DECL(ret, name, ...) typedef ret (FR_APIENTRY *PFN_##name)(__VA_ARGS__); extern PFN_##name name;
FR_GL_FUNCS(FR_DECL)
FR_GL_OPTIONAL_FUNCS(FR_DECL)
#undef FR_DECL

namespace fr {
/// Resolves all functions; returns false if any core function is missing.
bool loadGL(void* (*getProc)(const char*));
/// True when the current context is OpenGL ES (set by loadGL from GL_VERSION).
bool glIsES();
/// GLSL version line (+ ES precision qualifiers) to prepend to shaders for the current context.
const char* glslVersionLine();
} // namespace fr
