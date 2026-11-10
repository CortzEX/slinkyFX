#pragma once

#include <windows.h>
#include <string>

namespace Slinky {

class Injector {
public:
    static bool ExtractBundledDll(std::wstring& outDllPath, std::wstring& outLogPath);
    static bool InjectDll(DWORD pid, const std::wstring& dllPath);
    static bool MonitorStartup(DWORD pid, const std::wstring& logPath);
};

} // namespace Slinky
