#include <EGL/egl.h>
#include <cstdlib>
#include <iostream>

// The EGL wrapper. 
// Uses __attribute__((constructor)) to immediately intercept and adapt properties when dlopened.
// Target device: Raspberry Pi 4 (VideoCore VI)

__attribute__((constructor)) static void init_egl_bridge() {
    // The official Raspberry Pi hardware uses Mesa. Minecraft Bedrock naturally expects
    // standard ES contexts, so forcing an EGL GL Profile override aligns driver contexts
    // with video driver paths.
    std::cout << "[EGL Bridge] Setting MESA_GL_VERSION_OVERRIDE=3.0" << std::endl;
    setenv("MESA_GL_VERSION_OVERRIDE", "3.0", 1);
}

// Since we statically linking `libmcpe_shim.so` with `libEGL`, EGL symbols are forwarded
// organically to the underlying Mesa drivers. If custom EGL mappings are requested by the
// native Android APK, intercept them here via extern overrides.

extern "C" {
    // Demonstrative minimal proxy for an EGL fetch.
    // Minecraft's ES interactions will safely resolve using dl loading chain since
    // 'EGL' library is part of target_link_libraries in CMake.
    EGLDisplay eglGetDisplay_wrap(EGLNativeDisplayType display_id) {
        return eglGetDisplay(display_id);
    }
}
