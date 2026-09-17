#pragma once
// Tiny self-contained OpenGL 3.3 core loader (subset used by the app), so the tree has no
// generated GLAD dependency. Function pointers are resolved through glfwGetProcAddress.
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
  X(void, glPolygonMode, GLenum, GLenum) \
  X(void, glPolygonOffset, GLfloat, GLfloat) \
  X(void, glLineWidth, GLfloat) \
  X(void, glPointSize, GLfloat) \
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
  X(void, glTexBuffer, GLenum, GLenum, GLuint) \
  X(void, glTexParameteri, GLenum, GLenum, GLint)

#define FR_DECL(ret, name, ...) typedef ret (FR_APIENTRY *PFN_##name)(__VA_ARGS__); extern PFN_##name name;
FR_GL_FUNCS(FR_DECL)
#undef FR_DECL

namespace fr {
/// Resolves all functions; returns false if any core function is missing.
bool loadGL(void* (*getProc)(const char*));
} // namespace fr
