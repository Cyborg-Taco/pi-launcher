/**
 * Android Activity stub - provides dummy ANativeActivity for Minecraft
 * 
 * This stub implements the ANativeActivity interface that Minecraft expects,
 * but routes everything through our bridges (EGL, audio, filesystem).
 * 
 * Pi-specific: Assumes Mesa EGL with VideoCore VI GPU
 */

#include <jni.h>
#include <android/native_activity.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <iostream>

// Global state
static ANativeActivity* g_activity = nullptr;
static ALooper* g_looper = nullptr;

// Forward declarations
extern "C" {
    void android_audio_init(int sampleRate, int channels);
    void android_audio_play(const int16_t* samples, int numSamples);
    int android_file_open(const char* path, int mode);
    int android_file_read(int fd, void* buf, int count);
    int android_file_write(int fd, const void* buf, int count);
    int android_file_close(int fd);
}

/**
 * Get the cached JNI environment for current thread
 */
static JavaVM* g_jvm = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_GetCreatedJavaVMs(JavaVM** vmBuf, jsize bufSize, jsize* nVMs) {
    if (vmBuf && bufSize > 0 && g_jvm) {
        vmBuf[0] = g_jvm;
        *nVMs = 1;
        return JNI_OK;
    }
    *nVMs = 0;
    return JNI_ERR;
}

/**
 * Native activity callbacks - stub implementations
 */
static void onStart(ANativeActivity* activity) {
    std::cout << "[Shim] ANativeActivity onStart" << std::endl;
}

static void onResume(ANativeActivity* activity) {
    std::cout << "[Shim] ANativeActivity onResume" << std::endl;
}

static void onPause(ANativeActivity* activity) {
    std::cout << "[Shim] ANativeActivity onPause" << std::endl;
}

static void onStop(ANativeActivity* activity) {
    std::cout << "[Shim] ANativeActivity onStop" << std::endl;
}

static void onDestroy(ANativeActivity* activity) {
    std::cout << "[Shim] ANativeActivity onDestroy" << std::endl;
}

static void onWindowFocusChanged(ANativeActivity* activity, int focused) {
    // Not critical for headless/server operation
}

static void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window) {
    std::cout << "[Shim] Native window created" << std::endl;
}

static void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window) {
    std::cout << "[Shim] Native window destroyed" << std::endl;
}

static void onNativeWindowResized(ANativeActivity* activity, ANativeWindow* window) {
    // Handle resize if needed
}

static void onNativeWindowRedrawNeeded(ANativeActivity* activity, ANativeWindow* window) {
    // Trigger redraw
}

static void onInputQueueCreated(ANativeActivity* activity, AInputQueue* queue) {
    std::cout << "[Shim] Input queue created" << std::endl;
}

static void onInputQueueDestroyed(ANativeActivity* activity, AInputQueue* queue) {
    std::cout << "[Shim] Input queue destroyed" << std::endl;
}

static void* game_thread_start(void* param) {
    // This would normally start the game loop
    // For now, just return
    return nullptr;
}

/**
 * Main entry point - called when native activity is created
 * This is the primary entry point that Minecraft calls
 */
extern "C" {

void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    std::cout << "[Shim] ANativeActivity_onCreate called" << std::endl;
    
    g_activity = activity;
    
    // Store JNI environment
    activity->env->GetJavaVM(&g_jvm);
    
    // Set up callbacks
    activity->callbacks->onStart = onStart;
    activity->callbacks->onResume = onResume;
    activity->callbacks->onPause = onPause;
    activity->callbacks->onStop = onStop;
    activity->callbacks->onDestroy = onDestroy;
    activity->callbacks->onWindowFocusChanged = onWindowFocusChanged;
    activity->callbacks->onNativeWindowCreated = onNativeWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = onNativeWindowDestroyed;
    activity->callbacks->onNativeWindowResized = onNativeWindowResized;
    activity->callbacks->onNativeWindowRedrawNeeded = onNativeWindowRedrawNeeded;
    activity->callbacks->onInputQueueCreated = onInputQueueCreated;
    activity->callbacks->onInputQueueDestroyed = onInputQueueDestroyed;
    
    // Set internal paths - remap Android paths to Linux paths
    // These must match what the filesystem bridge expects
    const char* home = getenv("HOME");
    if (!home) home = "/root";
    
    static char internalDataPath[512];
    static char externalDataPath[512];
    static char packageName[128] = "com.mojang.minecraftpe";
    
    snprintf(internalDataPath, sizeof(internalDataPath), "%s/.local/share/mcpe-launcher", home);
    snprintf(externalDataPath, sizeof(externalDataPath), "%s/.local/share/mcpe-launcher", home);
    
    activity->internalDataPath = internalDataPath;
    activity->externalDataPath = externalDataPath;
    activity->packageName = packageName;
    activity->sdkVersion = 21;  // Android 5.0 minimum
    activity->vm = g_jvm;
    
    std::cout << "[Shim] Activity configured with paths:" << std::endl;
    std::cout << "  internal: " << activity->internalDataPath << std::endl;
    std::cout << "  external: " << activity->externalDataPath << std::endl;
    
    // Create looper for events (minimal implementation)
    // In a full implementation, this would handle input events
    g_looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    
    // The game would typically start its main loop here
    // For this stub, we just configure and return - the real game code
    // will be loaded by the runtime loader and will call back
}

} // extern "C"

/**
 * Asset Manager stub - simplified for file access
 * 
 * Note: For full asset support, we'd need to extract APK resources
 * This stub provides basic file access for the game to function
 */

// Allocator for asset manager (uses system malloc)
static void* asset_alloc(void* ctx, size_t size) {
    return malloc(size);
}

static void asset_free(void* ctx, void* ptr) {
    free(ptr);
}

extern "C" {

AAssetManager* AAssetManager_fromJava(JNIEnv* env, jobject assetManager) {
    // Return null - we don't have Java asset manager
    return nullptr;
}

AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode) {
    // Redirect to filesystem bridge
    int fd = android_file_open(filename, mode);
    if (fd < 0) return nullptr;
    
    AAsset* asset = new AAsset();
    asset->fd = fd;
    asset->offset = 0;
    return asset;
}

void AAsset_close(AAsset* asset) {
    if (asset) {
        android_file_close(asset->fd);
        delete asset;
    }
}

int AAsset_read(AAsset* asset, void* buf, size_t count) {
    return android_file_read(asset->fd, buf, count);
}

off_t AAsset_seek(AAsset* asset, off_t offset, int whence) {
    // Basic seek implementation would go here
    // For now, just return current position
    return 0;
}

const void* AAsset_getBuffer(AAsset* asset) {
    return nullptr;  // Not supported in this stub
}

off_t AAsset_getLength(AAsset* asset) {
    return 0;  // Would need actual implementation
}

off_t AAsset_getRemainingLength(AAsset* asset) {
    return 0;
}

} // extern "C"

/**
 * Input queue stub - minimal implementation
 */
extern "C" {

void AInputQueue_attachLooper(AInputQueue* queue, ALooper* looper, int ident, ALooper_callbackFunc callback, void* cbData) {
    // Not implemented - we don't handle input in this stub
}

void AInputQueue_detachLooper(AInputQueue* queue) {
    // Not implemented
}

int AInputQueue_getEvent(AInputQueue* queue, AInputEvent** outEvent) {
    // Return no events
    return 0;
}

void AInputQueue_preDispatchEvent(AInputQueue* queue, AInputEvent* event) {
    // Not implemented
}

void AInputQueue_finishEvent(AInputQueue* queue, AInputEvent* event, int handled) {
    // Not implemented
}

} // extern "C"

/**
 * Sensor stub - not used on Pi
 */
extern "C" {

ASensorManager* ASensorManager_getInstance() {
    return nullptr;
}

const ASensor* ASensorManager_getDefaultSensor(ASensorManager* manager, int type) {
    return nullptr;
}

int ASensorEventQueue_registerSensor(ASensorEventQueue* queue, const ASensor* sensor, int32_t samplingPeriod, int64_t maxBatchReportLatency) {
    return -1;
}

int ASensorEventQueue_enableSensor(ASensorEventQueue* queue, const ASensor* sensor) {
    return -1;
}

int ASensorEventQueue_disableSensor(ASensorEventQueue* queue, const ASensor* sensor) {
    return -1;
}

} // extern "C"

/**
 * Looper stub - minimal implementation for event handling
 */
extern "C" {

ALooper* ALooper_prepare(int opts) {
    static ALooper looper;
    // Basic initialization
    looper.fd = -1;
    looper.identifier = 0;
    return &looper;
}

int ALooper_addFd(ALooper* looper, int fd, int ident, int events, ALooper_callbackFunc callback, void* data) {
    return -1;  // Not implemented
}

int ALooper_removeFd(ALooper* looper, int fd) {
    return -1;  // Not implemented
}

int ALooper_pollOnce(int timeoutMillis, int* outFd, int* outEvents, void** outData) {
    // Return no events
    return ALOOPER_POLL_TIMEOUT;
}

int ALooper_wake(ALooper* looper) {
    return 0;
}

} // extern "C"