// Common functionality
// Licence GPL-3.0-only and Unlicence

#include "common.h"

#include <Windows.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <functional>
#include <filesystem>
#include <DbgHelp.h>

#include "MinHook.h"


namespace 
{
    // Log a debug line
    // Based on https://github.com/ramensoftware/windhawk-mods/blob/main/src/windhawk/shared/logger_base.cpp
    // Licence GPL-3.0-only
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

    // Get the version of a library
    // Based on https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-click.wh.cpp
    // Licence GPL-3.0-only
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

    // Hook a function
    // Based on:
    // - https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/mod.h
    // - https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/mod.cpp
    // Licence GPL-3.0-only
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

bool HookSymbols(std::string& moduleName, std::vector<SYMBOL_HOOK>& symbolHooks)
{
    if (symbolHooks.size() == 0)
    {
        return true;
    }

    // true if all functions hooked successfully
    bool ok = false;

    // items that may require cleanup
    HANDLE hProcess = INVALID_HANDLE_VALUE;

    // while false error catching loop
    do
    {
        // enable debug logs for symbol resolving
        SymSetOptions(SYMOPT_DEBUG);

        // initalise symbol resolver
        HANDLE hCurrentProcess = GetCurrentProcess();
        if (!DuplicateHandle(hCurrentProcess, hCurrentProcess, hCurrentProcess, &hProcess, 0, FALSE, DUPLICATE_SAME_ACCESS))
        {
            LogLine(L"Error: DuplicateHandle returned error: %d", GetLastError());
            break;
        }
        if (!SymInitialize(hCurrentProcess, NULL, FALSE))
        {
            LogLine(L"Error: SymInitialize returned error: %d", GetLastError());
            break;
        }

        // load symbols from microsoft symbol server (caches in the "sym" folder in the working directory)
        // needs SymSrv.dll and SrcSrv.dll to be in the same directory as (the dyamically loaded) DbgHelp.dll
        std::string symbolServer = "srv*https://msdl.microsoft.com/download/symbols";
        if (!SymSetSearchPath(hCurrentProcess, symbolServer.c_str()))
        {
            LogLine(L"Error: SymSetSearchPath returned error: %d", GetLastError());
            break;
        }

        // load symbols for the module
        if (!SymLoadModuleEx(hCurrentProcess, NULL, moduleName.c_str(), NULL, 0, 0, NULL, 0))
        {
            LogLine(L"Error: SymLoadModuleEx returned error: %d", GetLastError());
            break;
        }

        try
        {
            for (auto symbolHook : symbolHooks)
            {
                // create store for symbol lookup
                struct CFullSymbol : SYMBOL_INFO {
                    CHAR nameBuffer[MAX_SYM_NAME];
                } symbol;
                ZeroMemory(&symbol, sizeof(symbol));
                symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol.MaxNameLen = MAX_SYM_NAME;

                // log
                LogLine(L"Finding symbol: %S", symbolHook.symbol.c_str());
                if (!symbolHook.symbol.starts_with(moduleName))
                {
                    LogLine(L"Error: symbol %S does not start with the module name %S", symbolHook.symbol.c_str(), moduleName.c_str());
                    break;
                }

                // lookup the symbol
                if (!SymFromName(hCurrentProcess, symbolHook.symbol.c_str(), &symbol))
                {
                    LogLine(L"Error: SymFromName returned error: %d", GetLastError());
                    break;
                }

                // set the hook
                auto symbolAddress = (void*)symbol.Address;
                if (!SetFunctionHook(symbolAddress, symbolHook.pHookFunction, symbolHook.pOriginalFunction))
                {
                    break;
                }
            }

            // all hooked successfully
            LogLine(L"All symbols hooked successfully");
            ok = true;
        }
        catch (const std::exception& e)
        {
            LogLine(L"Error: HookSymbols threw exception: %hs", e.what());
        }
    } while (false);

    // cleaup
    if (hProcess != INVALID_HANDLE_VALUE)
    {
        (void)SymCleanup(hProcess);
        (void)CloseHandle(hProcess);
    }

    return ok;
}

void WaitForExitSignal()
{
    // loop untill signalled: check for named pipe to exist
    const std::wstring pipeName = L"\\\\.\\pipe\\takbar-close-thread-pipe";

    while (true)
    {
        // query first matching file
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(pipeName.c_str(), &findData);

        // found? (not error nor found)
        if (hFind != INVALID_HANDLE_VALUE)
        {
            // found, exit loop
            FindClose(hFind);
            break;
        }

        // yield before next call
        // (1 second wait to avoid a tight loop)
        Sleep(1000);
    }
}
