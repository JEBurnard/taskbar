// Modifiers that alter the behaviour of windows (explorer).
#include "modifier.h"

#include <algorithm>
#include <psapi.h>

#include "MinHook.h"
#include "windhawk_common.h"
#include "taskbar_middle_click.h"
#include "taskbar_grouping.h"


namespace
{
    // Get the base address of the module specified in the current process.
    // Zero indicates error
    uint64_t GetModuleBaseAddress(std::string modulePath)
    {
        // list modules for this process
        DWORD bytesNeeded = 0;
        HMODULE modules[1024] = { 0 };
        HANDLE currentProcess = GetCurrentProcess();
        if (!EnumProcessModules(currentProcess, modules, sizeof(modules), &bytesNeeded))
        {
            return 0;
        }

        // iterate the filled buffer
        auto numModules = bytesNeeded / sizeof(HMODULE);
        for (auto i = 0; i < numModules; ++i)
        {
            // validate module handle
            if (modules[i] == 0)
            {
                continue;
            }

            // get the full path to the module's file
            char pathBuffer[MAX_PATH];
            if (!GetModuleFileNameExA(currentProcess, modules[i], pathBuffer, MAX_PATH))
            {
                continue;
            }
            auto path = std::string(pathBuffer);

            // skip other modules
            std::transform(path.begin(), path.end(), path.begin(), tolower);
            std::transform(modulePath.begin(), modulePath.end(), modulePath.begin(), tolower);
            if (path != modulePath)
            {
                continue;
            }

            // get the info for this module
            MODULEINFO moduleInfo = { 0 };
            if (!GetModuleInformation(currentProcess, modules[i], &moduleInfo, sizeof(moduleInfo)))
            {
                continue;
            }

            // ok got the info we need
            auto baseAddress = moduleInfo.lpBaseOfDll;
            LogLine(L"%S base: %p", modulePath.c_str(), baseAddress);
            return (uint64_t)baseAddress;
        }

        // not found
        return 0;
    }

    // Hook a function
    bool SetFunctionHook(void* targetFunction, void* hookFunction, void** originalFunction)
    {
        MH_STATUS status = MH_CreateHook(targetFunction, hookFunction, originalFunction);
        if (status != MH_OK)
        {
            LogLine(L"Error: MH_CreateHook returned %d", status);
            return false;
        }

        status = MH_EnableHook(targetFunction);
        if (status != MH_OK)
        {
            LogLine(L"Error: MH_QueueEnableHook returned %d", status);
            return false;
        }

        return true;
    }
}

bool HookSymbols(const std::vector<ModuleHook>& moduleHooks, const IResolveSymbols& symbolResolver)
{
    // hook the module symbols
    bool error = false;
    for (const auto& moduleHook : moduleHooks)
    {
        // get the base address of this (already loaded by explorer.exe) module
        auto baseAddress = GetModuleBaseAddress(moduleHook.modulePath);
        if (baseAddress == 0)
        {
            LogLine(L"Error:failed to get base address of: %S!%S", moduleHook.moduleName.c_str());
            error = true;
            break;
        }

        for (const auto& symbolHook : moduleHook.symbolHooks)
        {
            // lookup the symbol address from the cache
            auto symbolAddress = symbolResolver.GetSymbolAddress(moduleHook.moduleName, symbolHook.symbolName);
            if (symbolAddress == nullptr)
            {
                LogLine(L"Error: missing symbol: %S!%S", moduleHook.moduleName.c_str(), symbolHook.symbolName.c_str());
                error = true;
                break;
            }

            // re-base the symbol address
            symbolAddress = (void*)((uint64_t)symbolAddress + baseAddress);

            // is there a hook to apply?
            if (symbolHook.pHookFunction != nullptr)
            {
                // apply the hook
                LogLine(L"Hooking: %p to %p %S!%S", symbolAddress, symbolHook.pHookFunction, moduleHook.moduleName.c_str(), symbolHook.symbolName.c_str());
                try
                {
                    if (!SetFunctionHook(symbolAddress, symbolHook.pHookFunction, symbolHook.pOriginalFunction))
                    {
                        error = true;
                        break;
                    }
                }
                catch (const std::exception& e)
                {
                    LogLine(L"Error: HookSymbols threw exception: %S", e.what());
                    error = true;
                    break;
                }
            }
            else
            {
                // not hooking, just tracking
                LogLine(L"Saving: %p %S!%S", symbolAddress, moduleHook.moduleName.c_str(), symbolHook.symbolName.c_str());
                if (symbolHook.pOriginalFunction == nullptr)
                {
                    LogLine(L"Error: original function is null");
                    error = true;
                    break;
                }
                *symbolHook.pOriginalFunction = symbolAddress;
            }
        }

        if (error)
        {
            break;
        }
    }

    return (!error);
}

std::vector<std::unique_ptr<IExplorerModifier>> Modifiers()
{
    std::vector<std::unique_ptr<IExplorerModifier>> modifiers;
    modifiers.push_back(std::make_unique<TaskbarMiddleClick>());
    modifiers.push_back(std::make_unique<TaskbarGrouping>());

    return modifiers;
}
