#ifndef NEBEL_RENDER_GL_API_H
#define NEBEL_RENDER_GL_API_H

#include <stdbool.h>

#if defined(__EMSCRIPTEN__)
  #include <GLES3/gl3.h>
#elif defined(__APPLE__)
  #define GL_SILENCE_DEPRECATION
  #include <OpenGL/gl3.h>
#else
  #include <SDL3/SDL_opengl.h>
  #include <SDL3/SDL_opengl_glext.h>

  /* Minimal X-macro list of OpenGL 3.3 Core functions loaded via SDL_GL_GetProcAddress */
  #define NEBEL_GL_FUNCS(X) \
      X(PFNGLCREATESHADERPROC,            glCreateShader) \
      X(PFNGLSHADERSOURCEPROC,            glShaderSource) \
      X(PFNGLCOMPILESHADERPROC,           glCompileShader) \
      X(PFNGLGETSHADERIVPROC,             glGetShaderiv) \
      X(PFNGLGETSHADERINFOLOGPROC,        glGetShaderInfoLog) \
      X(PFNGLDELETESHADERPROC,            glDeleteShader) \
      X(PFNGLCREATEPROGRAMPROC,           glCreateProgram) \
      X(PFNGLATTACHSHADERPROC,            glAttachShader) \
      X(PFNGLLINKPROGRAMPROC,             glLinkProgram) \
      X(PFNGLGETPROGRAMIVPROC,            glGetProgramiv) \
      X(PFNGLGETPROGRAMINFOLOGPROC,       glGetProgramInfoLog) \
      X(PFNGLDELETEPROGRAMPROC,           glDeleteProgram) \
      X(PFNGLUSEPROGRAMPROC,              glUseProgram) \
      X(PFNGLGETUNIFORMLOCATIONPROC,      glGetUniformLocation) \
      X(PFNGLUNIFORM1IPROC,               glUniform1i) \
      X(PFNGLUNIFORM1FPROC,               glUniform1f) \
      X(PFNGLUNIFORM2FPROC,               glUniform2f) \
      X(PFNGLUNIFORM3FPROC,               glUniform3f) \
      X(PFNGLUNIFORM4FPROC,               glUniform4f) \
      X(PFNGLGENVERTEXARRAYSPROC,         glGenVertexArrays) \
      X(PFNGLBINDVERTEXARRAYPROC,         glBindVertexArray) \
      X(PFNGLDELETEVERTEXARRAYSPROC,      glDeleteVertexArrays) \
      X(PFNGLGENBUFFERSPROC,              glGenBuffers) \
      X(PFNGLBINDBUFFERPROC,              glBindBuffer) \
      X(PFNGLBUFFERDATAPROC,              glBufferData) \
      X(PFNGLDELETEBUFFERSPROC,           glDeleteBuffers) \
      X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
      X(PFNGLVERTEXATTRIBPOINTERPROC,     glVertexAttribPointer) \
      X(PFNGLACTIVETEXTUREPROC,           glActiveTexture) \
      X(PFNGLGENFRAMEBUFFERSPROC,         glGenFramebuffers) \
      X(PFNGLBINDFRAMEBUFFERPROC,         glBindFramebuffer) \
      X(PFNGLFRAMEBUFFERTEXTURE2DPROC,    glFramebufferTexture2D) \
      X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,  glCheckFramebufferStatus) \
      X(PFNGLDELETEFRAMEBUFFERSPROC,      glDeleteFramebuffers)

  #define NEBEL_GL_DECL(type, name) extern type nebel_##name;
  NEBEL_GL_FUNCS(NEBEL_GL_DECL)
  #undef NEBEL_GL_DECL

  #define glCreateShader            nebel_glCreateShader
  #define glShaderSource            nebel_glShaderSource
  #define glCompileShader           nebel_glCompileShader
  #define glGetShaderiv             nebel_glGetShaderiv
  #define glGetShaderInfoLog        nebel_glGetShaderInfoLog
  #define glDeleteShader            nebel_glDeleteShader
  #define glCreateProgram           nebel_glCreateProgram
  #define glAttachShader            nebel_glAttachShader
  #define glLinkProgram             nebel_glLinkProgram
  #define glGetProgramiv            nebel_glGetProgramiv
  #define glGetProgramInfoLog       nebel_glGetProgramInfoLog
  #define glDeleteProgram           nebel_glDeleteProgram
  #define glUseProgram              nebel_glUseProgram
  #define glGetUniformLocation      nebel_glGetUniformLocation
  #define glUniform1i               nebel_glUniform1i
  #define glUniform1f               nebel_glUniform1f
  #define glUniform2f               nebel_glUniform2f
  #define glUniform3f               nebel_glUniform3f
  #define glUniform4f               nebel_glUniform4f
  #define glGenVertexArrays         nebel_glGenVertexArrays
  #define glBindVertexArray         nebel_glBindVertexArray
  #define glDeleteVertexArrays      nebel_glDeleteVertexArrays
  #define glGenBuffers              nebel_glGenBuffers
  #define glBindBuffer              nebel_glBindBuffer
  #define glBufferData              nebel_glBufferData
  #define glDeleteBuffers           nebel_glDeleteBuffers
  #define glEnableVertexAttribArray nebel_glEnableVertexAttribArray
  #define glVertexAttribPointer     nebel_glVertexAttribPointer
  #define glActiveTexture           nebel_glActiveTexture
  #define glGenFramebuffers         nebel_glGenFramebuffers
  #define glBindFramebuffer         nebel_glBindFramebuffer
  #define glFramebufferTexture2D    nebel_glFramebufferTexture2D
  #define glCheckFramebufferStatus  nebel_glCheckFramebufferStatus
  #define glDeleteFramebuffers      nebel_glDeleteFramebuffers
#endif

bool nebel_gl_load(void);

#endif /* NEBEL_RENDER_GL_API_H */
