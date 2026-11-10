#include "MinecraftDetector.hpp"
#include "JvmManager.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace SlinkyNative {

namespace {

// Minecraft 1.8.9 is visible to us under one of three CLASS names:
//   net/minecraft/client/Minecraft : Forge (SRG members), MCP/dev (named members)
//   ave                            : Vanilla, OptiFine, Lunar, Badlion (notch / obfuscated)
//   net/minecraft/class_1600       : Legacy Fabric intermediary members
//
// 1.7.10 uses bao (notch) or net/minecraft/client/Minecraft (MCP).
// What is found here is only reported, not acted on: the C++ engine is the
// client for every one of these, and it works the naming scheme out again
// for itself.
const char* const kNamedClassSig        = "Lnet/minecraft/client/Minecraft;";
const char* const kOfficialClassSig     = "Lave;";
const char* const kIntermediaryClassSig = "Lnet/minecraft/class_1600;";
const char* const kLegacy1710ClassSig   = "Lbao;";
const jint kAccStatic = 0x0008;

void FreeJvmti(jvmtiEnv* jvmti, char* p) {
    if (p) jvmti->Deallocate(reinterpret_cast<unsigned char*>(p));
}

// Names of all static fields whose type is the class itself (that is the Minecraft singleton).
std::vector<std::string> SelfTypedStaticFields(jvmtiEnv* jvmti, jclass cls, const char* selfSig) {
    std::vector<std::string> names;
    jint count = 0;
    jfieldID* fields = nullptr;
    if (jvmti->GetClassFields(cls, &count, &fields) != JVMTI_ERROR_NONE || !fields) return names;
    for (jint i = 0; i < count; ++i) {
        jint mods = 0;
        if (jvmti->GetFieldModifiers(cls, fields[i], &mods) != JVMTI_ERROR_NONE) continue;
        if (!(mods & kAccStatic)) continue;
        char* name = nullptr;
        char* sig = nullptr;
        char* generic = nullptr;
        if (jvmti->GetFieldName(cls, fields[i], &name, &sig, &generic) == JVMTI_ERROR_NONE) {
            if (name && sig && std::strcmp(sig, selfSig) == 0) names.push_back(name);
        }
        FreeJvmti(jvmti, name);
        FreeJvmti(jvmti, sig);
        FreeJvmti(jvmti, generic);
    }
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(fields));
    return names;
}

bool HasStaticField(JNIEnv* env, jclass cls, const char* name, const char* sig) {
    jfieldID f = env->GetStaticFieldID(cls, name, sig);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return f != nullptr;
}

bool HasStaticMethod(JNIEnv* env, jclass cls, const char* name, const char* sig) {
    jmethodID m = env->GetStaticMethodID(cls, name, sig);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return m != nullptr;
}

struct Verdict {
    MappingType mapping = MappingType::Unknown;
    std::string fieldName;   // static field holding the Minecraft instance
};

// Which naming scheme a Minecraft candidate class carries.
enum CandidateScheme {
    SchemeNamed = 0,
    SchemeOfficial = 1,
    SchemeIntermediary = 2,
    SchemeLegacy1710 = 3,
};

const char* SelfSigFor(int scheme) {
    switch (scheme) {
    case SchemeOfficial:     return kOfficialClassSig;
    case SchemeIntermediary: return kIntermediaryClassSig;
    case SchemeLegacy1710:   return kLegacy1710ClassSig;
    default:                 return kNamedClassSig;
    }
}

const char* DisplayNameFor(int scheme) {
    switch (scheme) {
    case SchemeOfficial:     return "ave";
    case SchemeIntermediary: return "net.minecraft.class_1600";
    case SchemeLegacy1710:   return "bao";
    default:                 return "net.minecraft.client.Minecraft";
    }
}

bool Contains(const std::vector<std::string>& v, const char* s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

Verdict Classify(JNIEnv* env, jvmtiEnv* jvmti, jclass cls, int scheme) {
    Verdict v;
    const char* selfSig = SelfSigFor(scheme);
    std::vector<std::string> statics = SelfTypedStaticFields(jvmti, cls, selfSig);

    if (scheme == SchemeOfficial) {
        // notch names: ave.S is the singleton in 1.8.9. Accept any lone self-typed static as well.
        if (Contains(statics, "S") || HasStaticField(env, cls, "S", selfSig)) {
            v.mapping = MappingType::Official;
            v.fieldName = "S";
        } else if (statics.size() == 1) {
            v.mapping = MappingType::Official;
            v.fieldName = statics[0];
        }
        return v;
    }

    if (scheme == SchemeIntermediary) {
        // Legacy Fabric intermediary names: the Minecraft singleton is field_3774.
        // Accept any lone self-typed static and confirm through the method_2965 accessor.
        if (Contains(statics, "field_3774") || HasStaticField(env, cls, "field_3774", selfSig)) {
            v.mapping = MappingType::Intermediary;
            v.fieldName = "field_3774";
        } else if (statics.size() == 1) {
            if (HasStaticMethod(env, cls, "method_2965", "()Lnet/minecraft/class_1600;")) {
                v.mapping = MappingType::Intermediary;
                v.fieldName = statics[0];
            }
        }
        return v;
    }

    if (scheme == SchemeLegacy1710) {
        // 1.7.10 obfuscated (bao). Accept the known singleton or a lone self-typed static.
        if (Contains(statics, "S") || HasStaticField(env, cls, "S", selfSig)) {
            v.mapping = MappingType::Legacy1710;
            v.fieldName = "S";
        } else if (Contains(statics, "field_71432_P") || HasStaticField(env, cls, "field_71432_P", selfSig)) {
            v.mapping = MappingType::Legacy1710;
            v.fieldName = "field_71432_P";
        } else if (statics.size() == 1) {
            v.mapping = MappingType::Legacy1710;
            v.fieldName = statics[0];
        }
        return v;
    }

    if (Contains(statics, "theMinecraft") || HasStaticField(env, cls, "theMinecraft", selfSig)) {
        v.mapping = MappingType::Named;
        v.fieldName = "theMinecraft";
    } else if (Contains(statics, "field_71432_P") || HasStaticField(env, cls, "field_71432_P", selfSig)) {
        v.mapping = MappingType::Srg;
        v.fieldName = "field_71432_P";
    } else if (statics.size() == 1) {
        // Unknown field name: decide by the accessor method instead.
        if (HasStaticMethod(env, cls, "func_71410_x", "()Lnet/minecraft/client/Minecraft;")) {
            v.mapping = MappingType::Srg;
            v.fieldName = statics[0];
        } else if (HasStaticMethod(env, cls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;")) {
            v.mapping = MappingType::Named;
            v.fieldName = statics[0];
        }
    }
    return v;
}

jobject ReadInstance(JNIEnv* env, jclass cls, const std::string& field, const char* selfSig) {
    jfieldID fid = env->GetStaticFieldID(cls, field.c_str(), selfSig);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!fid) return nullptr;
    jobject instance = env->GetStaticObjectField(cls, fid);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return instance;
}

std::string ClassNameOf(JNIEnv* env, jobject obj) {
    if (!obj) return "null";
    std::string result = "?";
    jclass cls = env->GetObjectClass(obj);
    jclass classClass = env->FindClass("java/lang/Class");
    if (cls && classClass) {
        jmethodID getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
        if (getName) {
            jstring name = static_cast<jstring>(env->CallObjectMethod(cls, getName));
            if (name) {
                const char* chars = env->GetStringUTFChars(name, nullptr);
                if (chars) {
                    result = chars;
                    env->ReleaseStringUTFChars(name, chars);
                }
                env->DeleteLocalRef(name);
            }
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (classClass) env->DeleteLocalRef(classClass);
    if (cls) env->DeleteLocalRef(cls);
    return result;
}

struct Candidate {
    jint index;
    int scheme;   // CandidateScheme
};

void ReleaseClasses(JNIEnv* env, jvmtiEnv* jvmti, jclass* classes, jint count) {
    for (jint i = 0; i < count; ++i) env->DeleteLocalRef(classes[i]);
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(classes));
}

} // namespace

bool MinecraftDetector::Detect(MinecraftInfo& outInfo) {
    JNIEnv* env = JvmManager::GetEnv();
    jvmtiEnv* jvmti = JvmManager::GetJvmti();
    if (!env || !jvmti) return false;

    // Wait up to 2 minutes for Minecraft 1.8.9 to finish starting. Launchers such as Lunar and
    // Badlion load the Minecraft class long before the game object exists, so we wait for the
    // singleton to be non-null as well (Bootstrap.start needs a live Minecraft instance).
    const ULONGLONG startTime = GetTickCount64();
    const ULONGLONG timeoutMs = 120000;
    const ULONGLONG unknownGiveUpMs = 25000;
    ULONGLONG unknownSince = 0;
    bool announcedWait = false;
    std::set<std::string> reported;

    while (GetTickCount64() - startTime < timeoutMs) {
        jint classCount = 0;
        jclass* classes = nullptr;

        if (jvmti->GetLoadedClasses(&classCount, &classes) == JVMTI_ERROR_NONE && classes) {
            std::vector<Candidate> candidates;
            for (jint i = 0; i < classCount; ++i) {
                char* signature = nullptr;
                char* generic = nullptr;
                if (jvmti->GetClassSignature(classes[i], &signature, &generic) == JVMTI_ERROR_NONE && signature) {
                    if (std::strcmp(signature, kNamedClassSig) == 0) {
                        candidates.push_back({ i, SchemeNamed });
                    } else if (std::strcmp(signature, kOfficialClassSig) == 0) {
                        candidates.push_back({ i, SchemeOfficial });
                    } else if (std::strcmp(signature, kIntermediaryClassSig) == 0) {
                        candidates.push_back({ i, SchemeIntermediary });
                    } else if (std::strcmp(signature, kLegacy1710ClassSig) == 0) {
                        candidates.push_back({ i, SchemeLegacy1710 });
                    }
                }
                FreeJvmti(jvmti, signature);
                FreeJvmti(jvmti, generic);
            }

            bool sawUnknown = false;
            for (const Candidate& c : candidates) {
                jclass cls = classes[c.index];
                const char* selfSig = SelfSigFor(c.scheme);
                Verdict verdict = Classify(env, jvmti, cls, c.scheme);
                const char* className = DisplayNameFor(c.scheme);

                if (verdict.mapping == MappingType::Unknown) {
                    sawUnknown = true;
                    if (reported.insert(std::string("unknown:") + className).second) {
                        std::vector<std::string> statics = SelfTypedStaticFields(jvmti, cls, selfSig);
                        Logger::Log(L"%hs is loaded but its member names are not recognised yet (%d self-typed static fields)",
                            className, (int)statics.size());
                        for (const std::string& s : statics) Logger::Log(L"    static field: %hs", s.c_str());
                    }
                    continue;
                }

                jobject instance = ReadInstance(env, cls, verdict.fieldName, selfSig);
                if (!instance) {
                    if (!announcedWait) {
                        Logger::Log(L"%hs is loaded (%hs names) but the game object is not created yet; waiting",
                            className, verdict.mapping == MappingType::Srg ? "srg" :
                            (verdict.mapping == MappingType::Official ? "official" :
                            (verdict.mapping == MappingType::Intermediary ? "intermediary" : "named")));
                        announcedWait = true;
                    }
                    continue;
                }
                env->DeleteLocalRef(instance);

                outInfo.mapping = verdict.mapping;
                switch (verdict.mapping) {
                case MappingType::Named:        outInfo.mappingName = "named";        break;
                case MappingType::Srg:          outInfo.mappingName = "srg";          break;
                case MappingType::Official:     outInfo.mappingName = "official";     break;
                case MappingType::Intermediary: outInfo.mappingName = "intermediary"; break;
                case MappingType::Legacy1710:   outInfo.mappingName = "legacy1710";   break;
                default:                        outInfo.mappingName = "official";     break;
                }
                outInfo.minecraftClass = static_cast<jclass>(env->NewGlobalRef(cls));

                jobject loader = nullptr;
                jvmti->GetClassLoader(cls, &loader);
                outInfo.classLoader = loader ? env->NewGlobalRef(loader) : nullptr;

                Logger::Log(L"detected Minecraft with %hs names (class %hs, singleton %hs, loader %hs)",
                    outInfo.mappingName.c_str(), className, verdict.fieldName.c_str(),
                    ClassNameOf(env, outInfo.classLoader).c_str());

                ReleaseClasses(env, jvmti, classes, classCount);
                return true;
            }

            ReleaseClasses(env, jvmti, classes, classCount);

            if (sawUnknown) {
                if (unknownSince == 0) unknownSince = GetTickCount64();
                if (GetTickCount64() - unknownSince > unknownGiveUpMs) {
                    Logger::Log(L"Minecraft classes were found but use a naming scheme this build does not support");
                    Logger::Log(L"(send this log to the developer; supported: Forge/SRG, Vanilla/OptiFine/Lunar/Badlion/notch, MCP, Legacy Fabric intermediary, 1.7.10)");
                    return false;
                }
            } else {
                unknownSince = 0;
            }
        }

        Sleep(500);
    }

    Logger::Log(L"Minecraft did not finish starting within 2 minutes");
    return false;
}

} // namespace SlinkyNative
