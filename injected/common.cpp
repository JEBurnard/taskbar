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
#include <DbgHelp.h>

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
        if (len >= 2 && len < 1023 && buffer[len + 1] == L'\n' && buffer[len + 2] == L'\n') 
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
            LogLine(L"Error: MH_CreateHookEx returned %d", status);
            return false;
        }

        status = MH_QueueEnableHookEx(hookIdent, targetFunction);
        if (status != MH_OK) 
        {
            LogLine(L"Error: MH_QueueEnableHookEx returned %d", status);
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

    LogLine(L"Version: %u.%u.%u.%u", major, minor, build, qfe);

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
        LogLine(L"Module handle is null");
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
        LogLine(L"Failed to get module debug information");
        return false;
    }

    // todo: get the symbol file from a symbol server or disk? ...
    // ...
    
    // todo: cleanup / RAII

    // initalise symbol resolver
    //SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    //HANDLE hProcess = INVALID_HANDLE_VALUE;
    HANDLE hCurrentProcess = GetCurrentProcess();
    /*if (!DuplicateHandle(hCurrentProcess, hCurrentProcess, hCurrentProcess, &hProcess, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        LogLine(L"DuplicateHandle returned error: %d", GetLastError());
        return FALSE;
    }*/
    //if (!SymInitialize(hProcess, NULL, TRUE))
    if (!SymInitialize(hCurrentProcess, NULL, FALSE))
    {
        LogLine(L"SymInitialize returned error: %d", GetLastError());
        return FALSE;
    }

    // todo: pass in instead
    std::string moduleName = "taskbar.dll";

    // load symbols for the module
    //if (!SymLoadModuleEx(hProcess, NULL, moduleName.c_str(), NULL, 0, 0, NULL, 0))
    if (!SymLoadModuleEx(hCurrentProcess, NULL, moduleName.c_str(), NULL, 0, 0, NULL, 0))
    {
        LogLine(L"SymLoadModuleEx returned error: %d", GetLastError());
        return false;
    }

    try 
    {
        for (auto symbolHook : symbolHooks)
        {
            // lookup this symbol
            /*ULONG64 buffer[(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(WCHAR) + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
            PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
            pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            pSymbol->MaxNameLen = MAX_SYM_NAME;*/
            struct CFullSymbol : SYMBOL_INFO {
                CHAR szRestOfName[512];
            } symbol;
            ZeroMemory(&symbol, sizeof(symbol));
            symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol.MaxNameLen = sizeof(symbol.szRestOfName) / sizeof(symbol.szRestOfName[0]);

            // todo: need module name and symbol
            // eg "taskbar.dll!<symbol>
            LogLine(L"Finding symbol: %S", symbolHook.symbol.c_str());

            // todo: this does not work.. = error 126 = cannot find symbol
            // = need to change the symbol strings to match what is in taskbar.dll.pdb
            //if (!SymFromName(hProcess, symbolHook.symbol.c_str(), pSymbol))
            if (!SymFromName(hCurrentProcess, symbolHook.symbol.c_str(), &symbol))
            {
                LogLine(L"SymFromName returned error: %d", GetLastError());
                return false;
            }

            //auto symbolAddress = (void*)pSymbol->Address;
            auto symbolAddress = (void*)symbol.Address;
            if (!SetFunctionHook(symbolAddress, symbolHook.pHookFunction, symbolHook.pOriginalFunction))
            {
                return false;
            }
        }
  
        return true;
    }
    catch (const std::exception& e) 
    {
        LogLine(L"Error: HookSymbols threw exception: %hs", e.what());
    }

    return false;
}
