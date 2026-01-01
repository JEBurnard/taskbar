// Shared code

#include "shared.h"

#include <iostream>
#include <algorithm>
#include <vector>
#include <functional>
#include <filesystem>
#include <tlhelp32.h>
#include <DbgHelp.h>

#include "windhawk_common.h"


namespace
{
    // Hook symbols from a single module
    // todo: refactor to lookup addresses from file
    bool HookModuleSymbols(const ModuleHook moduleHook)
    {
        // nothing to do
        if (moduleHook.symbolHooks.size() == 0)
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
            if (!SymLoadModuleEx(hCurrentProcess, NULL, moduleHook.modulePath.c_str(), moduleHook.moduleName.c_str(), 0, 0, NULL, 0))
            {
                LogLine(L"Error: SymLoadModuleEx returned error: %d", GetLastError());
                break;
            }

            try
            {
                auto symbolsHookedSuccessfully = 0;
                for (auto symbolHook : moduleHook.symbolHooks)
                {
                    // create store for symbol lookup
                    struct CFullSymbol : SYMBOL_INFO {
                        CHAR nameBuffer[MAX_SYM_NAME];
                    } symbol;
                    ZeroMemory(&symbol, sizeof(symbol));
                    symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
                    symbol.MaxNameLen = MAX_SYM_NAME;

                    // prefix symbol name with module name
                    auto symbolName = moduleHook.moduleName + "!" + symbolHook.symbolName;

                    // log
                    LogLine(L"Finding symbol: %S", symbolName.c_str());

                    // lookup the symbol
                    if (!SymFromName(hCurrentProcess, symbolName.c_str(), &symbol))
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
                if (symbolsHookedSuccessfully == moduleHook.symbolHooks.size())
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
            (void)SymCleanup(hProcess);
            (void)CloseHandle(hProcess);
        }

        return ok;
    }
}


SafeHandle safe_create_snapshot()
{
    auto handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    return SafeHandle(handle, CloseHandle);
}

SafeHandle safe_open_process(DWORD flags, DWORD processId)
{
    auto handle = OpenProcess(flags, FALSE, processId);
    return SafeHandle(handle, CloseHandle);
}

SafeHandle safe_open_pipe(const std::wstring& name)
{
    auto handle = CreateNamedPipe(name.c_str(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE, 1, 0, 0, 0, NULL);
    return SafeHandle(handle, CloseHandle);
}

SafeAlloc::SafeAlloc(SafeHandle process, SIZE_T size, DWORD protection)
    : _process(process)
    , _allocation(nullptr)
{
    _allocation = VirtualAllocEx(process.get(), nullptr, size, MEM_RESERVE | MEM_COMMIT, protection);
}

SafeAlloc::~SafeAlloc()
{
    if (_allocation != nullptr)
    {
        if (VirtualFreeEx(_process.get(), _allocation, NULL, MEM_RELEASE) != 0)
        {
            _allocation = nullptr;
        }
        else
        {
            std::cout << "Failed to free allocation" << std::endl;
        }
    }
}

HANDLE SafeAlloc::get() const
{
    return _allocation;
}


bool LookupSymbols(const std::vector<ModuleHook>& moduleHooks)
{
    // todo
    return false;
}

bool HookSymbols(const std::vector<ModuleHook>& moduleHooks)
{
    // todo
    return false;
}
