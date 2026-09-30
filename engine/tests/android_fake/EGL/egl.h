#ifndef OG_FAKE_EGL_H
#define OG_FAKE_EGL_H

#include <stdint.h>

typedef void *EGLDisplay;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLConfig;
typedef void *EGLNativeWindowType;

typedef int32_t EGLint;

#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_DEFAULT_DISPLAY ((void *)0)
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 0x0004
#define EGL_BLUE_SIZE 0x2022
#define EGL_GREEN_SIZE 0x2023
#define EGL_RED_SIZE 0x2024
#define EGL_ALPHA_SIZE 0x2021
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_CONTEXT_CLIENT_VERSION 0x3098

EGLDisplay eglGetDisplay(void *dpy);
int eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
int eglChooseConfig(EGLDisplay dpy, const EGLint *attribs, EGLConfig *cfg,
                    int size, EGLint *num);
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig cfg,
                                  EGLNativeWindowType win, const EGLint *attrs);
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share,
                            const EGLint *attrs);
int eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                   EGLContext ctx);
int eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
int eglDestroySurface(EGLDisplay dpy, EGLSurface surf);
int eglTerminate(EGLDisplay dpy);
int eglSwapBuffers(EGLDisplay dpy, EGLSurface surf);

#endif
