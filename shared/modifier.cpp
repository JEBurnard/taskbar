// Modifiers that alter the behaviour of windows (explorer).
#include "modifier.h"

#include "windhawk_common.h"
#include "taskbar_middle_click.h"


bool HookSymbols(const std::vector<ModuleHook>& moduleHooks, const IResolveSymbols& symbolResolver)
{
    // hook the module symbols
    bool error = false;
    for (const auto& moduleHook : moduleHooks)
    {
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

            // apply the hook
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

    return modifiers;
}
