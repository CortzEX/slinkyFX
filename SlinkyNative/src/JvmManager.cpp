#include "JvmManager.hpp"
#include "Logger.hpp"

typedef jint(JNICALL* pfn_JNI_GetCreatedJavaVMs)(JavaVM** vmBuf, jsize bufLen, jsize* nVMs);

namespace SlinkyNative {

JavaVM* JvmManager::s_JavaVM = nullptr;
JNIEnv* JvmManager::s_JniEnv = nullptr;
jvmtiEnv* JvmManager::s_JvmtiEnv = nullptr;

std::mutex JvmManager::s_HookMutex;
jclass JvmManager::s_CaptureTarget = nullptr;
std::vector<unsigned char> JvmManager::s_CapturedBytes;

jclass JvmManager::s_ReplacementTarget = nullptr;
const unsigned char* JvmManager::s_ReplacementData = nullptr;
jint JvmManager::s_ReplacementLength = 0;
bool JvmManager::s_ReplacementApplied = false;

bool JvmManager::Initialize() {
    Logger::Log(L"slinky-native loaded into PID %lu", GetCurrentProcessId());

    HMODULE hJvm = nullptr;
    for (int i = 0; i < 60; ++i) {
        hJvm = GetModuleHandleW(L"jvm.dll");
        if (hJvm) break;
        Sleep(100);
    }

    if (!hJvm) {
        Logger::Log(L"jvm.dll / JNI_GetCreatedJavaVMs not found; is this a Java process?");
        return false;
    }

    auto pJNI_GetCreatedJavaVMs = (pfn_JNI_GetCreatedJavaVMs)GetProcAddress(hJvm, "JNI_GetCreatedJavaVMs");
    if (!pJNI_GetCreatedJavaVMs) {
        Logger::Log(L"JNI_GetCreatedJavaVMs export not found in jvm.dll");
        return false;
    }

    jsize nVMs = 0;
    jint res = 0;
    for (int i = 0; i < 60; ++i) {
        res = pJNI_GetCreatedJavaVMs(&s_JavaVM, 1, &nVMs);
        if (res == JNI_OK && nVMs > 0 && s_JavaVM) break;
        Sleep(100);
    }

    if (res != JNI_OK || nVMs == 0 || !s_JavaVM) {
        Logger::Log(L"could not attach to the JVM");
        return false;
    }

    // Attach current thread
    res = s_JavaVM->AttachCurrentThread((void**)&s_JniEnv, nullptr);
    if (res != JNI_OK || !s_JniEnv) {
        Logger::Log(L"could not attach to the JVM");
        return false;
    }

    // Get JVMTI 1.2
    res = s_JavaVM->GetEnv((void**)&s_JvmtiEnv, JVMTI_VERSION_1_2);
    if (res != JNI_OK || !s_JvmtiEnv) {
        Logger::Log(L"JVMTI 1.2 is unavailable");
        return false;
    }

    // Add capabilities
    jvmtiCapabilities potentialCaps = { 0 };
    jvmtiError err = s_JvmtiEnv->GetPotentialCapabilities(&potentialCaps);
    if (err != JVMTI_ERROR_NONE) {
        Logger::Log(L"GetPotentialCapabilities failed: %d", err);
        return false;
    }

    if (!potentialCaps.can_retransform_classes) {
        Logger::Log(L"JVM cannot retransform classes");
        return false;
    }

    jvmtiCapabilities neededCaps = { 0 };
    neededCaps.can_retransform_classes = 1;
    neededCaps.can_retransform_any_class = potentialCaps.can_retransform_any_class;
    neededCaps.can_get_bytecodes = potentialCaps.can_get_bytecodes;
    neededCaps.can_generate_all_class_hook_events = potentialCaps.can_generate_all_class_hook_events;

    err = s_JvmtiEnv->AddCapabilities(&neededCaps);
    if (err != JVMTI_ERROR_NONE) {
        Logger::Log(L"AddCapabilities failed: %d", err);
        return false;
    }

    // Set callbacks
    jvmtiEventCallbacks callbacks = { 0 };
    callbacks.ClassFileLoadHook = &OnClassFileLoadHook;
    err = s_JvmtiEnv->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if (err != JVMTI_ERROR_NONE) {
        Logger::Log(L"SetEventCallbacks failed: %d", err);
        return false;
    }

    return true;
}

void JvmManager::Shutdown() {
    if (s_JvmtiEnv) {
        // Disable class file load hook event
        s_JvmtiEnv->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
    }

    // Use the current thread's JNIEnv so global refs are deleted safely even
    // if Shutdown is called from a thread other than the worker thread.
    JNIEnv* env = nullptr;
    if (s_JavaVM) {
        s_JavaVM->GetEnv((void**)&env, JNI_VERSION_1_2);
    }
    if (env) {
        ClearCapturedBytes(env);
        ClearReplacementData(env);
    } else {
        std::lock_guard<std::mutex> lock(s_HookMutex);
        s_CaptureTarget = nullptr;
        s_ReplacementTarget = nullptr;
        s_CapturedBytes.clear();
        s_ReplacementData = nullptr;
        s_ReplacementLength = 0;
        s_ReplacementApplied = false;
    }

    s_JvmtiEnv = nullptr;

    // Detach only the thread that originally attached (avoid JNI error from
    // calling DetachCurrentThread on an unattached or different thread).
    if (s_JavaVM && env == s_JniEnv) {
        s_JavaVM->DetachCurrentThread();
    }
    s_JavaVM = nullptr;
    s_JniEnv = nullptr;
}

JavaVM* JvmManager::GetJavaVM() { return s_JavaVM; }
JNIEnv* JvmManager::GetEnv() { return s_JniEnv; }
jvmtiEnv* JvmManager::GetJvmti() { return s_JvmtiEnv; }

void JvmManager::SetCaptureTarget(JNIEnv* env, jclass targetClass) {
    std::lock_guard<std::mutex> lock(s_HookMutex);
    if (s_CaptureTarget) {
        if (env) env->DeleteGlobalRef(s_CaptureTarget);
        s_CaptureTarget = nullptr;
    }
    if (targetClass && env) {
        s_CaptureTarget = (jclass)env->NewGlobalRef(targetClass);
    }
    s_CapturedBytes.clear();
}

std::vector<unsigned char> JvmManager::GetCapturedBytes() {
    std::lock_guard<std::mutex> lock(s_HookMutex);
    return s_CapturedBytes;
}

void JvmManager::ClearCapturedBytes(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(s_HookMutex);
    if (s_CaptureTarget) {
        if (env) env->DeleteGlobalRef(s_CaptureTarget);
        s_CaptureTarget = nullptr;
    }
    s_CapturedBytes.clear();
}

void JvmManager::SetReplacementData(JNIEnv* env, jclass targetClass, const unsigned char* data, jint length) {
    std::lock_guard<std::mutex> lock(s_HookMutex);
    if (s_ReplacementTarget) {
        if (env) env->DeleteGlobalRef(s_ReplacementTarget);
        s_ReplacementTarget = nullptr;
    }
    if (targetClass && env) {
        s_ReplacementTarget = (jclass)env->NewGlobalRef(targetClass);
    }
    s_ReplacementData = data;
    s_ReplacementLength = length;
    s_ReplacementApplied = false;
}

bool JvmManager::WasReplacementApplied() {
    std::lock_guard<std::mutex> lock(s_HookMutex);
    return s_ReplacementApplied;
}

void JvmManager::ClearReplacementData(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(s_HookMutex);
    if (s_ReplacementTarget) {
        if (env) env->DeleteGlobalRef(s_ReplacementTarget);
        s_ReplacementTarget = nullptr;
    }
    s_ReplacementData = nullptr;
    s_ReplacementLength = 0;
    s_ReplacementApplied = false;
}

void JNICALL JvmManager::OnClassFileLoadHook(
    jvmtiEnv* jvmti_env,
    JNIEnv* jni_env,
    jclass class_being_redefined,
    jobject loader,
    const char* name,
    jobject protection_domain,
    jint class_data_len,
    const unsigned char* class_data,
    jint* new_class_data_len,
    unsigned char** new_class_data)
{
    std::lock_guard<std::mutex> lock(s_HookMutex);

    // If capturing bytecode for getClassBytes
    if (s_CaptureTarget && class_being_redefined && jni_env->IsSameObject(s_CaptureTarget, class_being_redefined)) {
        if (class_data && class_data_len > 0) {
            s_CapturedBytes.assign(class_data, class_data + class_data_len);
        }
    }

    // If applying replacement bytecode for retransformClass
    if (s_ReplacementTarget && class_being_redefined && jni_env->IsSameObject(s_ReplacementTarget, class_being_redefined)) {
        if (s_ReplacementData && s_ReplacementLength > 0) {
            unsigned char* jvmMemory = nullptr;
            jvmtiError err = jvmti_env->Allocate(s_ReplacementLength, &jvmMemory);
            if (err == JVMTI_ERROR_NONE && jvmMemory) {
                memcpy(jvmMemory, s_ReplacementData, s_ReplacementLength);
                *new_class_data_len = s_ReplacementLength;
                *new_class_data = jvmMemory;
                s_ReplacementApplied = true;
            }
        }
    }
}

} // namespace SlinkyNative
