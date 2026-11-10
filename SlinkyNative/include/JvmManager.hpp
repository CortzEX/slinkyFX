#pragma once

#include <windows.h>
#include "jdk/jni.h"
#include "jdk/jvmti.h"
#include <vector>
#include <mutex>

namespace SlinkyNative {

class JvmManager {
public:
    static bool Initialize();
    static void Shutdown();

    static JavaVM* GetJavaVM();
    static JNIEnv* GetEnv();
    static jvmtiEnv* GetJvmti();

    // Hook state management for getClassBytes and retransformClass.
    // Global refs are used because the ClassFileLoadHook callback may be
    // invoked on a JVM thread different from the caller.
    static void SetCaptureTarget(JNIEnv* env, jclass targetClass);
    static std::vector<unsigned char> GetCapturedBytes();
    static void ClearCapturedBytes(JNIEnv* env);

    static void SetReplacementData(JNIEnv* env, jclass targetClass, const unsigned char* data, jint length);
    static bool WasReplacementApplied();
    static void ClearReplacementData(JNIEnv* env);

private:
    static void JNICALL OnClassFileLoadHook(
        jvmtiEnv* jvmti_env,
        JNIEnv* jni_env,
        jclass class_being_redefined,
        jobject loader,
        const char* name,
        jobject protection_domain,
        jint class_data_len,
        const unsigned char* class_data,
        jint* new_class_data_len,
        unsigned char** new_class_data);

    static JavaVM* s_JavaVM;
    static JNIEnv* s_JniEnv;
    static jvmtiEnv* s_JvmtiEnv;

    static std::mutex s_HookMutex;
    static jclass s_CaptureTarget;
    static std::vector<unsigned char> s_CapturedBytes;

    static jclass s_ReplacementTarget;
    static const unsigned char* s_ReplacementData;
    static jint s_ReplacementLength;
    static bool s_ReplacementApplied;
};

} // namespace SlinkyNative
