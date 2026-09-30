#ifndef OG_FAKE_GLES2_H
#define OG_FAKE_GLES2_H

#include <stdint.h>

typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE0 0x84C0
#define GL_FLOAT 0x1406
#define GL_TRIANGLE_STRIP 0x0005
#define GL_FALSE 0
#define GL_TRUE 1

GLuint glCreateShader(GLenum type);
void glShaderSource(GLuint s, int n, const char *const *src, const int *len);
void glCompileShader(GLuint s);
void glGetShaderiv(GLuint s, GLenum pname, GLint *out);
void glGetShaderInfoLog(GLuint s, GLsizei n, GLsizei *len, char *log);
GLuint glCreateProgram(void);
void glAttachShader(GLuint p, GLuint s);
void glLinkProgram(GLuint p);
void glGenTextures(int n, GLuint *out);
void glBindTexture(GLenum t, GLuint tex);
void glTexParameteri(GLenum t, GLenum pname, GLint param);
void glTexImage2D(GLenum t, GLint level, GLint fmt, int w, int h, int b,
                  GLenum fmt2, GLenum type, const void *px);
void glTexSubImage2D(GLenum t, GLint level, int x, int y, int w, int h,
                     GLenum fmt, GLenum type, const void *px);
void glUseProgram(GLuint p);
GLint glGetUniformLocation(GLuint p, const char *name);
GLint glGetAttribLocation(GLuint p, const char *name);
void glUniform1i(GLint loc, GLint v);
void glActiveTexture(GLenum t);
void glVertexAttribPointer(GLuint i, int size, GLenum type, GLboolean n,
                           int stride, const void *ptr);
void glEnableVertexAttribArray(GLuint i);
void glDrawArrays(GLenum mode, GLint first, GLint count);
void glViewport(int x, int y, int w, int h);

#endif
