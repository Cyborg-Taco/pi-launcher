/**
 * JNI stub - provides minimal JNI implementation for Minecraft
 * 
 * Minecraft uses JNI to communicate with the Android runtime.
 * This stub provides minimal implementations that work with our bridges.
 * 
 * Pi-specific: No actual Java VM - all calls are stubbed or redirected
 */

#include <jni.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstring>
#include <iostream>

// Global state
static JavaVM* g_jvm = nullptr;
static JNIEnv* g_env = nullptr;
static std::mutex g_class_mutex;
static std::map<std::string, jclass> g_class_cache;

// Forward declare filesystem bridge
extern "C" {
    int android_file_open(const char* path, int mode);
    int android_file_read(int fd, void* buf, int count);
    int android_file_write(int fd, const void* buf, int count);
    int android_file_close(int fd);
}

/**
 * Minimal JNI implementation - these stubs allow the game to compile
 * but many functions just return dummy values
 */

// JNI_GetCreatedJavaVMs is the entry point the game uses
extern "C" {

jint JNI_GetCreatedJavaVMs(JavaVM** vmBuf, jsize bufSize, jsize* nVMs) {
    if (g_jvm && vmBuf && bufSize > 0) {
        vmBuf[0] = g_jvm;
        *nVMs = 1;
        return JNI_OK;
    }
    if (nVMs) *nVMs = 0;
    return JNI_ERR;
}

jint JNI_GetDefaultJavaVMInitArgs(void* vm_args) {
    // Return minimal JNI 1.6 support
    return JNI_OK;
}

jint JNI_CreateJavaVM(JavaVM** p_vm, JNIEnv** p_env, void* vm_args) {
    // We don't actually create a JVM - this is a stub
    // The game will use our stubs instead of real JNI
    
    if (!g_jvm) {
        // Create a minimal fake JVM structure
        static JavaVM fake_jvm;
        g_jvm = &fake_jvm;
    }
    
    if (p_vm) *p_vm = g_jvm;
    if (p_env) *p_env = g_env;
    
    std::cout << "[JNI] Stub JVM created (no actual Java runtime)" << std::endl;
    return JNI_OK;
}

} // extern "C"

/**
 * JNIEnv methods for current thread
 */
static JNINativeInterface g_jni_interface;
static JNIInvokeInterface g_jvm_interface;

static jobject g_native_activity = nullptr;

extern "C" {

// ==================== JNIEnv Functions ====================

jclass JNIEnv_FindClass(JNIEnv* env, const char* name) {
    std::lock_guard<std::mutex> lock(g_class_mutex);
    
    // Check cache
    auto it = g_class_cache.find(name);
    if (it != g_class_cache.end()) {
        return it->second;
    }
    
    // Map common Android class names to stubs
    // In a full implementation, we'd define these classes
    if (strstr(name, "android/app/Activity")) {
        // Return a stub class for Activity
        static jclass activity_class = (jclass)0xDEADBEEF;
        g_class_cache[name] = activity_class;
        return activity_class;
    }
    
    if (strstr(name, "android/content/Context")) {
        static jclass context_class = (jclass)0xCAFEBABE;
        g_class_cache[name] = context_class;
        return context_class;
    }
    
    // For unknown classes, return a generic stub
    static jclass stub_class = (jclass)0xBADCODE;
    g_class_cache[name] = stub_class;
    
    std::cout << "[JNI] FindClass: " << name << " (stubbed)" << std::endl;
    return stub_class;
}

jmethodID JNIEnv_GetMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    // Return a stub method ID
    // In real implementation, would look up from class definition
    std::cout << "[JNI] GetMethodID: " << name << " " << sig << " (stubbed)" << std::endl;
    return (jmethodID)0xF00D;
}

jmethodID JNIEnv_GetStaticMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    std::cout << "[JNI] GetStaticMethodID: " << name << " " << sig << " (stubbed)" << std::endl;
    return (jmethodID)0xBEEF;
}

jobject JNIEnv_CallObjectMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    // Return null for most calls
    return nullptr;
}

jobject JNIEnv_CallStaticObjectMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    return nullptr;
}

void JNIEnv_CallVoidMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    // Stub - no-op
}

void JNIEnv_CallStaticVoidMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    // Stub - no-op
}

jint JNIEnv_CallIntMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    return 0;
}

jboolean JNIEnv_CallBooleanMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    return JNI_FALSE;
}

jlong JNIEnv_CallLongMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    return 0;
}

jfloat JNIEnv_CallFloatMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    return 0.0f;
}

jdouble JNIEnv_CallDoubleMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    return 0.0;
}

jobject JNIEnv_NewObject(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    // Return a stub object
    return (jobject)0xNULL;
}

jobject JNIEnv_NewStringUTF(JNIEnv* env, const char* utf) {
    // Create a minimal string object
    // In a full implementation, would create a proper java.lang.String
    if (!utf) return nullptr;
    
    // Return a stub string representation
    // The actual game code will handle null strings gracefully
    return (jobject)utf;  // Just return the pointer as "handle"
}

const char* JNIEnv_GetStringUTFChars(JNIEnv* env, jstring string, jboolean* isCopy) {
    if (!string) return nullptr;
    // In our stub, string is actually the char* pointer
    return (const char*)string;
}

void JNIEnv_ReleaseStringUTFChars(JNIEnv* env, jstring string, const char* utf) {
    // No-op for our stub
}

jint JNIEnv_GetArrayLength(JNIEnv* env, jobjectArray array) {
    return 0;
}

jobjectArray JNIEnv_NewObjectArray(JNIEnv* env, jsize len, jclass elementClass, jobject initialElement) {
    return nullptr;
}

jobjectArray JNIEnv_NewByteArray(JNIEnv* env, jsize len) {
    return nullptr;
}

jbyte* JNIEnv_GetByteArrayElements(JNIEnv* env, jbyteArray array, jboolean* isCopy) {
    return nullptr;
}

void JNIEnv_ReleaseByteArrayElements(JNIEnv* env, jbyteArray array, jbyte* elems, jint mode) {
    // No-op
}

jint JNIEnv_SetIntField(JNIEnv* env, jobject obj, jfieldID fieldID, jint value) {
    return 0;
}

jint JNIEnv_GetIntField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    return 0;
}

jobject JNIEnv_GetObjectField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    return nullptr;
}

void JNIEnv_SetObjectField(JNIEnv* env, jobject obj, jfieldID fieldID, jobject value) {
    // No-op
}

jfieldID JNIEnv_GetFieldID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    std::cout << "[JNI] GetFieldID: " << name << " " << sig << " (stubbed)" << std::endl;
    return (jfieldID)0xFEED;
}

jfieldID JNIEnv_GetStaticFieldID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    return (jfieldID)0xDEAD;
}

jint JNIEnv_ThrowNew(JNIEnv* env, jclass clazz, const char* message) {
    std::cerr << "[JNI] Throw: " << message << std::endl;
    return 0;
}

jthrowable JNIEnv_ExceptionOccurred(JNIEnv* env) {
    return nullptr;
}

void JNIEnv_ExceptionClear(JNIEnv* env) {
    // No-op
}

void JNIEnv_DeleteLocalRef(JNIEnv* env, jobject obj) {
    // No-op for stub objects
}

void JNIEnv_DeleteGlobalRef(JNIEnv* env, jobject obj) {
    // No-op
}

jobject JNIEnv_NewGlobalRef(JNIEnv* env, jobject obj) {
    return obj;
}

jboolean JNIEnv_IsSameObject(JNIEnv* env, jobject obj1, jobject obj2) {
    return obj1 == obj2 ? JNI_TRUE : JNI_FALSE;
}

jint JNIEnv_PushLocalFrame(JNIEnv* env, jint capacity) {
    return JNI_OK;
}

jint JNIEnv_PopLocalFrame(JNIEnv* env, jobject result) {
    return JNI_OK;
}

jobject JNIEnv_NewDirectByteBuffer(JNIEnv* env, void* address, jlong capacity) {
    return nullptr;
}

void* JNIEnv_GetDirectBufferAddress(JNIEnv* env, jobject buf) {
    return nullptr;
}

jlong JNIEnv_GetDirectBufferCapacity(JNIEnv* env, jobject buf) {
    return -1;
}

// ==================== JavaVM Functions ====================

jint JavaVM_GetVersion(JavaVM* vm) {
    return JNI_VERSION_1_6;
}

jint JavaVM_DestroyJavaVM(JavaVM* vm) {
    return JNI_ERR;  // Cannot destroy stub VM
}

jint JavaVM_AttachCurrentThread(JavaVM* vm, void** p_env, void* args) {
    if (!g_env) {
        // Create minimal stub environment
        static JNIEnv stub_env;
        stub_env.functions = &g_jni_interface;
        g_env = &stub_env;
    }
    *p_env = g_env;
    return JNI_OK;
}

jint JavaVM_DetachCurrentThread(JavaVM* vm) {
    return JNI_OK;
}

jint JavaVM_GetEnv(JavaVM* vm, void** p_env, jint version) {
    if (!g_env) {
        static JNIEnv stub_env;
        stub_env.functions = &g_jni_interface;
        g_env = &stub_env;
    }
    *p_env = g_env;
    return JNI_OK;
}

jint JavaVM_AttachCurrentThreadAsDaemon(JavaVM* vm, void** p_env, void* args) {
    return JavaVM_AttachCurrentThread(vm, p_env, args);
}

} // extern "C"

/**
 * Initialize JNI stub - called from activity stub
 */
void jni_stub_init() {
    std::cout << "[JNI] JNI stub initialized" << std::endl;
    
    // Initialize the function tables
    // This would need full function pointer initialization for production
    memset(&g_jni_interface, 0, sizeof(g_jni_interface));
    memset(&g_jvm_interface, 0, sizeof(g_jvm_interface));
    
    // Create stub JVM if not exists
    if (!g_jvm) {
        JNI_CreateJavaVM(&g_jvm, &g_env, nullptr);
    }
}