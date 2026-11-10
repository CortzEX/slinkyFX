#pragma once

#include <windows.h>
#include <string>
#include "jdk/jni.h"
#include "jdk/jvmti.h"

namespace SlinkyNative {

enum class MappingType {
    Unknown = 0,
    Named = 101,        // MCP mappings
    Srg = 102,          // SRG mappings (Forge)
    Official = 103,     // Notch/Vanilla/Lunar/Badlion obfuscated mappings
    Intermediary = 104, // Legacy Fabric intermediary names
    Legacy1710 = 105    // 1.7.10 obfuscated/MCP
};

struct MinecraftInfo {
    MappingType mapping = MappingType::Unknown;
    std::string mappingName;
    jclass minecraftClass = nullptr;
    jobject classLoader = nullptr;
};

class MinecraftDetector {
public:
    static bool Detect(MinecraftInfo& outInfo);
};

} // namespace SlinkyNative
