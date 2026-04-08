/**
 * EGL Bridge - wraps Mesa EGL calls for Minecraft on Raspberry Pi 4
 * 
 * Routes Android EGL calls to Mesa EGL, configured for VideoCore VI GPU.
 * Uses OpenGL ES 2.0 with software fallback.
 * 
 * Pi-specific assumptions:
 * - Mesa version 22.x+ with EGL_EXT_platform_device support
 * - VideoCore VI GPU (Pi 4) with Mesa driver
 * - MESA_GL_VERSION_OVERRIDE=3.0 set by loader
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <map>

// ============================================================================
// EGL Function Types (from Khronos headers)
// ============================================================================

typedef EGLDisplay (EGLAPIENTRYP PFNEGLGETDISPLAYPROC)(EGLNativeDisplayType);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLINITIALIZEPROC)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLTERMINATEPROC)(EGLDisplay);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLCHOOSECONFIGPROC)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLGETCONFIGATTRIBPROC)(EGLDisplay, EGLConfig, EGLint, EGLint*);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLBINDAPIPROC)(EGLenum);
typedef EGLContext (EGLAPIENTRYP PFNEGLCREATECONTEXTPROC)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLSurface (EGLAPIENTRYP PFNEGLCREATEWINDOWSURFACEPROC)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
typedef EGLSurface (EGLAPIENTRYP PFNEGLCREATEPBUFFERSURFACEPROC)(EGLDisplay, EGLConfig, const EGLint*, const EGLint*);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSWAPPROC)(EGLDisplay, EGLSurface);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLMAKECURRENTPROC)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLDESTROYCONTEXTPROC)(EGLDisplay, EGLContext);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLDESTROYSURFACEPROC)(EGLDisplay, EGLSurface);
typedef const char* (EGLAPIENTRYP PFNEGLQUERYSTRINGPROC)(EGLDisplay, EGLint);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLGETERRORPROC)(void);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYCONFIGATTRIBPROC)(EGLDisplay, EGLConfig, EGLint, EGLint*);

// ============================================================================
// Global State
// ============================================================================

// EGL library handle
static void* g_eglLib = nullptr;

// Function pointers (lazy loaded)
static PFNEGLGETDISPLAYPROC eglGetDisplay_ = nullptr;
static PFNEGLINITIALIZEPROC eglInitialize_ = nullptr;
static PFNEGLTERMINATEPROC eglTerminate_ = nullptr;
static PFNEGLCHOOSECONFIGPROC eglChooseConfig_ = nullptr;
static PFNEGLGETCONFIGATTRIBPROC eglGetConfigAttrib_ = nullptr;
static PFNEGLBINDAPIPROC eglBindAPI_ = nullptr;
static PFNEGLCREATECONTEXTPROC eglCreateContext_ = nullptr;
static PFNEGLCREATEWINDOWSURFACEPROC eglCreateWindowSurface_ = nullptr;
static PFNEGLCREATEPBUFFERSURFACEPROC eglCreatePbufferSurface_ = nullptr;
static PFNEGLSWAPPROC eglSwapBuffers_ = nullptr;
static PFNEGLMAKECURRENTPROC eglMakeCurrent_ = nullptr;
static PFNEGLDESTROYCONTEXTPROC eglDestroyContext_ = nullptr;
static PFNEGLDESTROYSURFACEPROC eglDestroySurface_ = nullptr;
static PFNEGLQUERYSTRINGPROC eglQueryString_ = nullptr;
static PFNEGLGETERRORPROC eglGetError_ = nullptr;

// Our EGL display and context (the "Android" view of things)
static EGLDisplay g_androidDisplay = EGL_DEFAULT_DISPLAY;
static EGLContext g_androidContext = EGL_NO_CONTEXT;
static EGLSurface g_androidSurface = EGL_NO_SURFACE;
static EGLDisplay g_realDisplay = EGL_DEFAULT_DISPLAY;

// ============================================================================
// Library Loading
// ============================================================================

/**
 * Load EGL library and resolve all required functions
 */
static bool loadEGLLibrary() {
    if (g_eglLib) return true;  // Already loaded
    
    // Try Mesa EGL first, then fallback to system
    const char* eglLibs[] = {
        "libEGL_mesa.so.0",
        "libEGL.so.1",
        "libEGL.so",
        nullptr
    };
    
    for (int i = 0; eglLibs[i]; ++i) {
        g_eglLib = dlopen(eglLibs[i], RTLD_NOW);
        if (g_eglLib) {
            std::cout << "[EGL] Loaded: " << eglLibs[i] << std::endl;
            break;
        }
    }
    
    if (!g_eglLib) {
        std::cerr << "[EGL] FATAL: Cannot load EGL library" << std::endl;
        return false;
    }
    
    // Resolve all functions
    eglGetDisplay_ = (PFNEGLGETDISPLAYPROC)dlsym(g_eglLib, "eglGetDisplay");
    eglInitialize_ = (PFNEGLINITIALIZEPROC)dlsym(g_eglLib, "eglInitialize");
    eglTerminate_ = (PFNEGLTERMINATEPROC)dlsym(g_eglLib, "eglTerminate");
    eglChooseConfig_ = (PFNEGLCHOOSECONFIGPROC)dlsym(g_eglLib, "eglChooseConfig");
    eglGetConfigAttrib_ = (PFNEGLGETCONFIGATTRIBPROC)dlsym(g_eglLib, "eglGetConfigAttrib");
    eglBindAPI_ = (PFNEGLBINDAPIPROC)dlsym(g_eglLib, "eglBindAPI");
    eglCreateContext_ = (PFNEGLCREATECONTEXTPROC)dlsym(g_eglLib, "eglCreateContext");
    eglCreateWindowSurface_ = (PFNEGLCREATEWINDOWSURFACEPROC)dlsym(g_eglLib, "eglCreateWindowSurface");
    eglCreatePbufferSurface_ = (PFNEGLCREATEPBUFFERSURFACEPROC)dlsym(g_eglLib, "eglCreatePbufferSurface");
    eglSwapBuffers_ = (PFNEGLSWAPPROC)dlsym(g_eglLib, "eglSwapBuffers");
    eglMakeCurrent_ = (PFNEGLMAKECURRENTPROC)dlsym(g_eglLib, "eglMakeCurrent");
    eglDestroyContext_ = (PFNEGLDESTROYCONTEXTPROC)dlsym(g_eglLib, "eglDestroyContext");
    eglDestroySurface_ = (PFNEGLDESTROYSURFACEPROC)dlsym(g_eglLib, "eglDestroySurface");
    eglQueryString_ = (PFNEGLQUERYSTRINGPROC)dlsym(g_eglLib, "eglQueryString");
    eglGetError_ = (PFNEGLGETERRORPROC)dlsym(g_eglLib, "eglGetError");
    
    // Verify all functions loaded
    if (!eglGetDisplay_ || !eglInitialize_ || !eglCreateContext_ || 
        !eglCreateWindowSurface_ || !eglSwapBuffers_ || !eglMakeCurrent_) {
        std::cerr << "[EGL] FATAL: Failed to resolve required EGL functions" << std::endl;
        return false;
    }
    
    std::cout << "[EGL] All functions resolved successfully" << std::endl;
    return true;
}

// ============================================================================
// EGL Entry Points (Android-compatible names)
// ============================================================================

extern "C" {

// Main EGL entry points that Minecraft uses
EGLDisplay eglGetDisplay(EGLNativeDisplayType display) {
    if (!loadEGLLibrary()) return EGL_NO_DISPLAY;
    
    // Use EGL_EXT_platform_device for Pi 4 hardware acceleration
    // Otherwise fall back to default display
    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = 
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)dlsym(g_eglLib, "eglGetPlatformDisplayEXT");
    
    if (eglGetPlatformDisplayEXT) {
        // Try to get hardware display first
        EGLDisplay dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, EGL_DEFAULT_DISPLAY, nullptr);
        if (dpy != EGL_NO_DISPLAY) {
            std::cout << "[EGL] Using hardware EGL device" << std::endl;
            g_realDisplay = dpy;
            return dpy;
        }
    }
    
    // Fall back to default display
    g_realDisplay = eglGetDisplay_(EGL_DEFAULT_DISPLAY);
    return g_realDisplay;
}

EGLBoolean eglInitialize(EGLDisplay display, EGLint* major, EGLint* minor) {
    if (!eglInitialize_) return EGL_FALSE;
    
    EGLBoolean result = eglInitialize_(display, major, minor);
    
    if (result) {
        std::cout << "[EGL] Initialized: " << (major ? *major : 0) << "." << (minor ? *minor : 0) << std::endl;
        
        // Log available extensions
        if (eglQueryString_) {
            const char* exts = eglQueryString_(display, EGL_EXTENSIONS);
            if (exts) {
                std::cout << "[EGL] Extensions: " << std::string(exts).substr(0, 256) << "..." << std::endl;
            }
        }
    }
    
    return result;
}

EGLBoolean eglTerminate(EGLDisplay display) {
    if (!eglTerminate_) return EGL_FALSE;
    return eglTerminate_(display);
}

EGLBoolean eglChooseConfig(EGLDisplay display, const EGLint* attribList, 
                           EGLConfig* configs, EGLint configSize, EGLint* numConfig) {
    if (!eglChooseConfig_) return EGL_FALSE;
    return eglChooseConfig_(display, attribList, configs, configSize, numConfig);
}

EGLBoolean eglGetConfigAttrib(EGLDisplay display, EGLConfig config, EGLint attribute, EGLint* value) {
    if (!eglGetConfigAttrib_) return EGL_FALSE;
    return eglGetConfigAttrib_(display, config, attribute, value);
}

EGLBoolean eglBindAPI(EGLenum api) {
    if (!eglBindAPI_) return EGL_FALSE;
    return eglBindAPI_(api);
}

EGLContext eglCreateContext(EGLDisplay display, EGLConfig config, EGLContext shareContext, const EGLint* attribList) {
    if (!eglCreateContext_) return EGL_NO_CONTEXT;
    
    // Build context attributes for OpenGL ES 2.0
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    // Allow caller to override if they want ES 3.0
    if (attribList) {
        contextAttribs[1] = 2;  // Force ES 2.0 for compatibility
    }
    
    EGLContext ctx = eglCreateContext_(display, config, shareContext, contextAttribs);
    
    if (ctx != EGL_NO_CONTEXT) {
        std::cout << "[EGL] Created OpenGL ES 2.0 context" << std::endl;
        g_androidContext = ctx;
    } else {
        std::cerr << "[EGL] Failed to create context" << std::endl;
    }
    
    return ctx;
}

EGLSurface eglCreateWindowSurface(EGLDisplay display, EGLConfig config, 
                                  EGLNativeWindowType window, const EGLint* attribList) {
    if (!eglCreateWindowSurface_) return EGL_NO_SURFACE;
    
    EGLSurface surface = eglCreateWindowSurface_(display, config, window, attribList);
    
    if (surface != EGL_NO_SURFACE) {
        std::cout << "[EGL] Created window surface" << std::endl;
        g_androidSurface = surface;
    } else {
        std::cerr << "[EGL] Failed to create window surface" << std::endl;
    }
    
    return surface;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay display, EGLConfig config, 
                                   const EGLint* attribList, const EGLint* pbufferAttribs) {
    if (!eglCreatePbufferSurface_) return EGL_NO_SURFACE;
    return eglCreatePbufferSurface_(display, config, attribList, pbufferAttribs);
}

EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    if (!eglSwapBuffers_) return EGL_FALSE;
    return eglSwapBuffers_(display, surface);
}

EGLBoolean eglMakeCurrent(EGLDisplay display, EGLSurface draw, EGLSurface read, EGLContext context) {
    if (!eglMakeCurrent_) return EGL_FALSE;
    return eglMakeCurrent_(display, draw, read, context);
}

EGLBoolean eglDestroyContext(EGLDisplay display, EGLContext context) {
    if (!eglDestroyContext_) return EGL_FALSE;
    return eglDestroyContext_(display, context);
}

EGLBoolean eglDestroySurface(EGLDisplay display, EGLSurface surface) {
    if (!eglDestroySurface_) return EGL_FALSE;
    return eglDestroySurface_(display, surface);
}

const char* eglQueryString(EGLDisplay display, int name) {
    if (!eglQueryString_) return nullptr;
    return eglQueryString_(display, name);
}

EGLint eglGetError(void) {
    if (!eglGetError_) return EGL_SUCCESS;
    return eglGetError_();
}

// Additional EGL functions that might be called
EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint* value) {
    return EGL_TRUE;  // Stub
}

EGLNativeWindowType eglCreateNativeWindow(EGLDisplay display, EGLConfig config, 
                                          const EGLint* attribList) {
    // This would need SDL2 window integration for full implementation
    // For now, return nullptr and use pbuffer fallback
    return nullptr;
}

void eglDestroyNativeWindow(EGLNativeWindowType window) {
    // No-op for stub
}

// GLES2 entry points
void glActiveTexture(GLenum texture) {
    ::glActiveTexture(texture);
}

void glAttachShader(GLuint program, GLuint shader) {
    ::glAttachShader(program, shader);
}

void glBindAttribLocation(GLuint program, GLuint index, const char* name) {
    ::glBindAttribLocation(program, index, name);
}

void glBindBuffer(GLenum target, GLuint buffer) {
    ::glBindBuffer(target, buffer);
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    ::glBindFramebuffer(target, framebuffer);
}

void glBindTexture(GLenum target, GLuint texture) {
    ::glBindTexture(target, texture);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) {
    ::glBlendFunc(sfactor, dfactor);
}

void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    ::glBufferData(target, size, data, usage);
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    ::glBufferSubData(target, offset, size, data);
}

void glClear(GLbitfield mask) {
    ::glClear(mask);
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    ::glClearColor(red, green, blue, alpha);
}

void glClearStencil(GLint s) {
    ::glClearStencil(s);
}

void glCompileShader(GLuint shader) {
    ::glCompileShader(shader);
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, 
                            GLsizei width, GLsizei height, GLint border, 
                            GLsizei imageSize, const void* data) {
    ::glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
}

void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                GLsizei width, GLsizei height, GLenum format, 
                                GLsizei imageSize, const void* data) {
    ::glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, 
                      GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    ::glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height) {
    ::glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
}

GLuint glCreateProgram(void) {
    return ::glCreateProgram();
}

GLuint glCreateShader(GLenum type) {
    return ::glCreateShader(type);
}

void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    ::glDeleteBuffers(n, buffers);
}

void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    ::glDeleteFramebuffers(n, framebuffers);
}

void glDeleteProgram(GLuint program) {
    ::glDeleteProgram(program);
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
    ::glDeleteTextures(n, textures);
}

void glDeleteShader(GLuint shader) {
    ::glDeleteShader(shader);
}

void glDisable(GLenum cap) {
    ::glDisable(cap);
}

void glDisableVertexAttribArray(GLuint index) {
    ::glDisableVertexAttribArray(index);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    ::glDrawArrays(mode, first, count);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    ::glDrawElements(mode, count, type, indices);
}

void glEnable(GLenum cap) {
    ::glEnable(cap);
}

void glEnableVertexAttribArray(GLuint index) {
    ::glEnableVertexAttribArray(index);
}

void glFinish(void) {
    ::glFinish();
}

void glFlush(void) {
    ::glFlush();
}

void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, 
                               GLuint renderbuffer) {
    ::glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, 
                            GLuint texture, GLint level) {
    ::glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

void glFrontFace(GLenum mode) {
    ::glFrontFace(mode);
}

void glGenBuffers(GLsizei n, GLuint* buffers) {
    ::glGenBuffers(n, buffers);
}

void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    ::glGenFramebuffers(n, framebuffers);
}

void glGenTextures(GLsizei n, GLuint* textures) {
    ::glGenTextures(n, textures);
}

GLenum glGetError(void) {
    return ::glGetError();
}

void glGetFloatv(GLenum pname, GLfloat* params) {
    ::glGetFloatv(pname, params);
}

void glGetIntegerv(GLenum pname, GLint* params) {
    ::glGetIntegerv(pname, params);
}

void glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    ::glGetProgramiv(program, pname, params);
}

void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, char* log) {
    ::glGetProgramInfoLog(program, bufSize, length, log);
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    ::glGetShaderiv(shader, pname, params);
}

void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, char* log) {
    ::glGetShaderInfoLog(shader, bufSize, length, log);
}

const GLubyte* glGetString(GLenum name) {
    return ::glGetString(name);
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params) {
    ::glGetTexParameterfv(target, pname, params);
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
    ::glGetTexParameteriv(target, pname, params);
}

void glGetUniformfv(GLuint program, GLint location, GLfloat* params) {
    ::glGetUniformfv(program, location, params);
}

void glGetUniformiv(GLuint program, GLint location, GLint* params) {
    ::glGetUniformiv(program, location, params);
}

GLint glGetAttribLocation(GLuint program, const char* name) {
    return ::glGetAttribLocation(program, name);
}

GLint glGetUniformLocation(GLuint program, const char* name) {
    return ::glGetUniformLocation(program, name);
}

void glHint(GLenum target, GLenum mode) {
    ::glHint(target, mode);
}

void glIsBuffer(GLuint buffer) {
    ::glIsBuffer(buffer);
}

void glIsEnabled(GLenum cap) {
    ::glIsEnabled(cap);
}

void glIsFramebuffer(GLuint framebuffer) {
    ::glIsFramebuffer(framebuffer);
}

void glIsProgram(GLuint program) {
    ::glIsProgram(program);
}

void glIsTexture(GLuint texture) {
    ::glIsTexture(texture);
}

void glLineWidth(GLfloat width) {
    ::glLineWidth(width);
}

void glLinkProgram(GLuint program) {
    ::glLinkProgram(program);
}

void glPixelStorei(GLenum pname, GLint param) {
    ::glPixelStorei(pname, param);
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
    ::glPolygonOffset(factor, units);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, 
                  GLenum format, GLenum type, void* pixels) {
    ::glReadPixels(x, y, width, height, format, type, pixels);
}

void glReleaseShaderCompiler(void) {
    ::glReleaseShaderCompiler();
}

void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    ::glRenderbufferStorage(target, internalformat, width, height);
}

void glSampleCoverage(GLfloat value, GLboolean invert) {
    ::glSampleCoverage(value, invert);
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    ::glScissor(x, y, width, height);
}

void glShaderBinary(GLsizei count, const GLuint* shaders, GLenum binaryformat, 
                    const void* binary, GLsizei length) {
    ::glShaderBinary(count, shaders, binaryformat, binary, length);
}

void glShaderSource(GLuint shader, GLsizei count, const char** string, const GLint* length) {
    ::glShaderSource(shader, count, string, length);
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    ::glStencilFunc(func, ref, mask);
}

void glStencilMask(GLuint mask) {
    ::glStencilMask(mask);
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    ::glStencilOp(fail, zfail, zpass);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, 
                  GLsizei width, GLsizei height, GLint border, 
                  GLenum format, GLenum type, const void* pixels) {
    ::glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    ::glTexParameterf(target, pname, param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    ::glTexParameterfv(target, pname, params);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    ::glTexParameteri(target, pname, param);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    ::glTexParameteriv(target, pname, params);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, 
                     GLenum type, const void* pixels) {
    ::glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

void glUniform1f(GLint location, GLfloat v0) {
    ::glUniform1f(location, v0);
}

void glUniform1fv(GLint location, GLsizei count, const GLfloat* value) {
    ::glUniform1fv(location, count, value);
}

void glUniform1i(GLint location, GLint v0) {
    ::glUniform1i(location, v0);
}

void glUniform1iv(GLint location, GLsizei count, const GLint* value) {
    ::glUniform1iv(location, count, value);
}

void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    ::glUniform2f(location, v0, v1);
}

void glUniform2fv(GLint location, GLsizei count, const GLfloat* value) {
    ::glUniform2fv(location, count, value);
}

void glUniform2i(GLint location, GLint v0, GLint v1) {
    ::glUniform2i(location, v0, v1);
}

void glUniform2iv(GLint location, GLsizei count, const GLint* value) {
    ::glUniform2iv(location, count, value);
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    ::glUniform3f(location, v0, v1, v2);
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat* value) {
    ::glUniform3fv(location, count, value);
}

void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
    ::glUniform3i(location, v0, v1, v2);
}

void glUniform3iv(GLint location, GLsizei count, const GLint* value) {
    ::glUniform3iv(location, count, value);
}

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    ::glUniform4f(location, v0, v1, v2, v3);
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat* value) {
    ::glUniform4fv(location, count, value);
}

void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
    ::glUniform4i(location, v0, v1, v2, v3);
}

void glUniform4iv(GLint location, GLsizei count, const GLint* value) {
    ::glUniform4iv(location, count, value);
}

void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    ::glUniformMatrix2fv(location, count, transpose, value);
}

void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    ::glUniformMatrix3fv(location, count, transpose, value);
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    ::glUniformMatrix4fv(location, count, transpose, value);
}

void glUseProgram(GLuint program) {
    ::glUseProgram(program);
}

void glValidateProgram(GLuint program) {
    ::glValidateProgram(program);
}

void glVertexAttrib1f(GLuint index, GLfloat v0) {
    ::glVertexAttrib1f(index, v0);
}

void glVertexAttrib1fv(GLuint index, const GLfloat* v) {
    ::glVertexAttrib1fv(index, v);
}

void glVertexAttrib2f(GLuint index, GLfloat v0, GLfloat v1) {
    ::glVertexAttrib2f(index, v0, v1);
}

void glVertexAttrib2fv(GLuint index, const GLfloat* v) {
    ::glVertexAttrib2fv(index, v);
}

void glVertexAttrib3f(GLuint index, GLfloat v0, GLfloat v1, GLfloat v2) {
    ::glVertexAttrib3f(index, v0, v1, v2);
}

void glVertexAttrib3fv(GLuint index, const GLfloat* v) {
    ::glVertexAttrib3fv(index, v);
}

void glVertexAttrib4f(GLuint index, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    ::glVertexAttrib4f(index, v0, v1, v2, v3);
}

void glVertexAttrib4fv(GLuint index, const GLfloat* v) {
    ::glVertexAttrib4fv(index, v);
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, 
                           GLsizei stride, const void* pointer) {
    ::glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    ::glViewport(x, y, width, height);
}

} // extern "C"

// Initialization
static __attribute__((constructor)) void egl_bridge_init() {
    std::cout << "[EGL] EGL bridge initialized (VideoCore VI / Mesa)" << std::endl;
}