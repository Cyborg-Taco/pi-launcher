#include <jni.h>
#include <iostream>

// Stub implementations of native Android activity framework and hardware managers.

extern "C" {
    // === Core Application and Looper Stubs ===
    void* ANativeActivity_onCreate() { return nullptr; }
    void* ALooper_prepare(int) { return nullptr; }
    void* ALooper_addFd(void*, int, int, int, void*, void*) { return nullptr; }
    void* ALooper_pollAll(int, int*, int*, void**) { return nullptr; }
    
    // === Asset Manager Stubs ===
    void* AAssetManager_fromJava(JNIEnv*, jobject) { return nullptr; }
    void* AAssetManager_open(void*, const char*, int) { return nullptr; }
    void* AAsset_read(void*, void*, size_t) { return nullptr; }
    void  AAsset_close(void*) { }
    long  AAsset_getLength(void*) { return 0; }
    
    // === Input and Sensor Stubs ===
    void* AInputQueue_attachLooper(void*, void*, int, void*, void*) { return nullptr; }
    void  AInputQueue_detachLooper(void*) { }
    int   AInputQueue_getEvent(void*, void**) { return -1; }
    void* ASensorManager_getInstance() { return nullptr; }
    void* ASensorManager_getDefaultSensor(void*, int) { return nullptr; }

    // === Bare-bones JNI Stubs ===
    jclass FindClass(JNIEnv* env, const char* name) { 
        // We let it glide through missing class requests safely.
        return nullptr; 
    }
    
    void CallVoidMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) { }
    
    jstring NewStringUTF(JNIEnv* env, const char* bytes) { 
        return (jstring)nullptr; 
    }
    
    jint GetEnv(JavaVM* vm, void** env, jint version) { 
        // Handover a dummy pointer reference assuming it won't be harshly dereferenced 
        *env = nullptr; 
        return JNI_OK; 
    }
}
