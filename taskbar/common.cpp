// Common code from WindHawk
// Extracted from 
// - https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/shared/logger_base.cpp
// - https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-grouping.wh.cp
// - https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-click.wh.cpp
// - https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/mod.h
// - https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/mod.cpp
// - https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/mods_api_internal.h
// Licence GPL-3.0-only

#include "common.h"

#include <Windows.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <functional>
#include <filesystem>

#include "functions.h"
#include "MinHook.h"


namespace 
{
    void VLogLine(PCWSTR format, va_list args)
    {
        WCHAR buffer[1025];
        int len = _vsnwprintf_s(buffer, _TRUNCATE, format, args);
        if (len == -1)
        {
            // Truncation occurred.
            len = _countof(buffer) - 1;
        }

        while (--len >= 0 && buffer[len] == L'\n') 
        {
            // Skip all newlines at the end.
        }

        // Leave only a single trailing newline.
        if (buffer[len + 1] == L'\n' && buffer[len + 2] == L'\n') 
        {
            buffer[len + 2] = L'\0';
        }

        OutputDebugStringW(buffer);
    }

    VS_FIXEDFILEINFO* GetModuleVersionInfo(HMODULE hModule, UINT* puPtrLen) 
    {
        void* pFixedFileInfo = nullptr;
        UINT uPtrLen = 0;

        HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
        if (hResource) 
        {
            HGLOBAL hGlobal = LoadResource(hModule, hResource);
            if (hGlobal) 
            {
                void* pData = LockResource(hGlobal);
                if (pData != nullptr) 
                {
                    if (!VerQueryValue(pData, L"\\", &pFixedFileInfo, &uPtrLen) || uPtrLen == 0) {
                        pFixedFileInfo = nullptr;
                        uPtrLen = 0;
                    }
                }
            }
        }

        if (puPtrLen) 
        {
            *puPtrLen = uPtrLen;
        }

        return (VS_FIXEDFILEINFO*)pFixedFileInfo;
    }

    bool SetFunctionHook(void* targetFunction, void* hookFunction, void** originalFunction)
    {
        ULONG_PTR hookIdent = 1; // not needed in this implementation, todo: revert to vanilla minhook
        MH_STATUS status = MH_CreateHookEx(hookIdent, targetFunction, hookFunction, originalFunction);
        if (status != MH_OK) 
        {
            wprintf(L"Error: MH_CreateHookEx returned %d", status);
            return false;
        }

        status = MH_QueueEnableHookEx(hookIdent, targetFunction);
        if (status != MH_OK) 
        {
            wprintf(L"Error: MH_QueueEnableHookEx returned %d", status);
            return false;
        }

        return true;
    }
}

void LogLine(PCWSTR format, ...) 
{
    va_list args;
    va_start(args, format);
    VLogLine(format, args);
    va_end(args);
}

WinVersion GetExplorerVersion()
{
    VS_FIXEDFILEINFO* fixedFileInfo = GetModuleVersionInfo(nullptr, nullptr);
    if (fixedFileInfo == nullptr) 
    {
        return WinVersion::Unsupported;
    }

    WORD major = HIWORD(fixedFileInfo->dwFileVersionMS);
    WORD minor = LOWORD(fixedFileInfo->dwFileVersionMS);
    WORD build = HIWORD(fixedFileInfo->dwFileVersionLS);
    WORD qfe = LOWORD(fixedFileInfo->dwFileVersionLS);

    wprintf(L"Version: %u.%u.%u.%u", major, minor, build, qfe);

    switch (major) {
    case 10:
        if (build < 22000) 
        {
            return WinVersion::Win10;
        }
        else if (build < 26100) 
        {
            return WinVersion::Win11;
        }
        else 
        {
            return WinVersion::Win11_24H2;
        }
        break;
    }

    return WinVersion::Unsupported;
}

bool HookSymbols(HMODULE module, std::vector<SYMBOL_HOOK>& symbolHooks)
{
    if (!module) 
    {
        wprintf(L"Module handle is null");
        return false;
    }

    if (symbolHooks.size() == 0)
    {
        return true;
    }

    // find the debug symbols for this module
    GUID pdbGuid;
    DWORD pdbAge;
    if (!Functions::ModuleGetPDBInfo(module, &pdbGuid, &pdbAge))
    {
        wprintf(L"Failed to get module debug information");
        return false;
    }

    // todo: get the symbol file from a symbol server or disk? ...
    // 

    try 
    {
        for (auto symbolHook : symbolHooks)
        {
            auto symbolAddress = nullptr;
            // todo: need to find function address from debug symbols
            // (as we are hooking non-exported functions)

            if (!SetFunctionHook(symbolAddress, symbolHook.pHookFunction, symbolHook.pOriginalFunction))
            {
                return false;
            }
        }
  
        return true;
    }
    catch (const std::exception& e) 
    {
        wprintf(L"Error: HookSymbols threw exception: %hs", e.what());
    }

    return false;
}
