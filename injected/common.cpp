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

    // Functions dynamicially loaded from DbgHelp dll
    // (dynamically loaded, so the correct version can be loaded, which needs to be in the same directory
    // as srcsrv.dll and symsrv.dll to use a symbol server to get the debug symbols)
    // Unlicence
    class DbgHlp
    {
    public:
        // default constructor - all handles and pointers are null
        DbgHlp() = default;

        // destructor - free 
        ~DbgHlp()
        {
            cleanup();
        }

        // dynamically load the dbghelp library (from the current directory)
        // calls LoadLibrary which is "unsafe" to call in DllMain, but works...
        bool load()
        {
            // get the current directory
            wchar_t currentDirectoryBuffer[MAX_PATH];
            if (!GetCurrentDirectoryW(MAX_PATH, currentDirectoryBuffer))
            {
                LogLine(L"Failed to get current directory: %d", GetLastError());
                cleanup();
                return false;
            }
            auto currentDirectory = std::wstring(currentDirectoryBuffer);

            auto modulePath = currentDirectory + L"\\DbgHelp.dll";
            moduleHandle = LoadLibraryW(modulePath.c_str());
            if (moduleHandle == NULL)
            {
                LogLine(L"Failed to load %s: %d", modulePath.c_str(), GetLastError());
                cleanup();
                return false;
            }

            SymSetOptions = (SymSetOptions_t)GetProcAddress(moduleHandle, "SymSetOptions");
            if (SymSetOptions == NULL)
            {
                LogLine(L"Failed to load dbghelp.dll!SymSetOptions: %d", GetLastError());
                cleanup();
                return false;
            }

            SymInitialize = (SymInitialize_t)GetProcAddress(moduleHandle, "SymInitialize");
            if (SymInitialize == NULL)
            {
                LogLine(L"Failed to load dbghelp.dll!SymInitialize: %d", GetLastError());
                cleanup();
                return false;
            }

            SymSetSearchPath = (SymSetSearchPath_t)GetProcAddress(moduleHandle, "SymSetSearchPath");
            if (SymSetSearchPath == NULL)
            {
                LogLine(L"Failed to load dbghelp.dll!SymSetSearchPath: %d", GetLastError());
                cleanup();
                return false;
            }

            SymLoadModuleEx = (SymLoadModuleEx_t)GetProcAddress(moduleHandle, "SymLoadModuleEx");
            if (SymLoadModuleEx == NULL)
            {
                LogLine(L"Failed to load dbghelp.dll!SymLoadModuleEx: %d", GetLastError());
                cleanup();
                return false;
            }

            SymFromName = (SymFromName_t)GetProcAddress(moduleHandle, "SymFromName");
            if (SymFromName == NULL)
            {
                LogLine(L"Failed to load dbghelp.dll!SymFromName: %d", GetLastError());
                cleanup();
                return false;
            }

            SymCleanup = (SymCleanup_t)GetProcAddress(moduleHandle, "SymCleanup");
            if (SymCleanup == NULL)
            {
                LogLine(L"Failed to load dbghelp.dll!SymCleanup: %d", GetLastError());
                cleanup();
                return false;
            }

            return true;
        }

    private:
        void cleanup()
        {
            if (moduleHandle != NULL)
            {
                FreeLibrary(moduleHandle);
            }
            SymSetOptions = NULL;
            SymInitialize = NULL;
            SymSetSearchPath = NULL;
            SymLoadModuleEx = NULL;
            SymFromName = NULL;
            SymCleanup = NULL;
        }

        // module handle
        HINSTANCE moduleHandle = NULL;

    public:
        // function pointers
        typedef DWORD(__stdcall* SymSetOptions_t)(DWORD SymOptions);
        SymSetOptions_t SymSetOptions = NULL;

        typedef BOOL(__stdcall* SymInitialize_t)(HANDLE hProcess, PCSTR UserSearchPath, BOOL fInvadeProcess);
        SymInitialize_t SymInitialize = NULL;

        typedef BOOL(__stdcall* SymSetSearchPath_t)(HANDLE hProcess, PCSTR SearchPath);
        SymSetSearchPath_t SymSetSearchPath = NULL;

        typedef DWORD64(__stdcall* SymLoadModuleEx_t)(HANDLE hProcess, HANDLE hFile, PCSTR ImageName, PCSTR ModuleName, DWORD64 BaseOfDll, DWORD DllSize,PMODLOAD_DATA Data, DWORD Flags);
        SymLoadModuleEx_t SymLoadModuleEx = NULL;

        typedef BOOL(__stdcall* SymFromName_t)(HANDLE hProcess, PCSTR Name, PSYMBOL_INFO Symbol);
        SymFromName_t SymFromName = NULL;

        typedef BOOL(__stdcall* SymCleanup_t)(HANDLE hProcess);
        SymCleanup_t SymCleanup = NULL;
    };
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

bool HookSymbols(std::string& modulePath, std::string& moduleName, std::vector<SYMBOL_HOOK>& symbolHooks)
{
    if (symbolHooks.size() == 0)
    {
        return true;
    }

    // dynamically load dbghelp
    DbgHlp dbg;
    if (!dbg.load())
    {
        return false;
    }

    // true if all functions hooked successfully
    bool ok = false;

    // items that may require cleanup
    HANDLE hProcess = INVALID_HANDLE_VALUE;

    // while false error catching loop
    do
    {
        // enable debug logs for symbol resolving
        if (!dbg.SymSetOptions(SYMOPT_DEBUG))
        {
            LogLine(L"Error: SymSetOptions returned error: %d", GetLastError());
            break;
        }

        // initalise symbol resolver
        HANDLE hCurrentProcess = GetCurrentProcess();
        if (!DuplicateHandle(hCurrentProcess, hCurrentProcess, hCurrentProcess, &hProcess, 0, FALSE, DUPLICATE_SAME_ACCESS))
        {
            LogLine(L"Error: DuplicateHandle returned error: %d", GetLastError());
            break;
        }
        if (!dbg.SymInitialize(hCurrentProcess, NULL, FALSE))
        {
            LogLine(L"Error: SymInitialize returned error: %d", GetLastError());
            break;
        }
        // load symbols from microsoft symbol server (caches in the "sym" folder in the working directory)
        // needs SymSrv.dll and SrcSrv.dll to be in the same directory as (the dyamically loaded) DbgHelp.dll
        std::string symbolServer = "srv*https://msdl.microsoft.com/download/symbols";
        if (!dbg.SymSetSearchPath(hCurrentProcess, symbolServer.c_str()))
        {
            LogLine(L"Error: SymSetSearchPath returned error: %d", GetLastError());
            break;
        }

        // load symbols for the module
        if (!dbg.SymLoadModuleEx(hCurrentProcess, NULL, modulePath.c_str(), moduleName.c_str(), 0, 0, NULL, 0))
        {
            LogLine(L"Error: SymLoadModuleEx returned error: %d", GetLastError());
            break;
        }

        try
        {
            auto symbolsHookedSuccessfully = 0;
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
                if (!dbg.SymFromName(hCurrentProcess, symbolHook.symbol.c_str(), &symbol))
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

                symbolsHookedSuccessfully++;
            }

            // all hooked successfully?
            if (symbolsHookedSuccessfully == symbolHooks.size())
            {
                LogLine(L"All symbols hooked successfully");
                ok = true;
            }
        }
        catch (const std::exception& e)
        {
            LogLine(L"Error: HookSymbols threw exception: %hs", e.what());
        }
    } while (false);

    // cleaup
    if (hProcess != INVALID_HANDLE_VALUE)
    {
        (void)dbg.SymCleanup(hProcess);
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
