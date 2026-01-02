// The dll that will be injected into the explorer.exe process.
// Hooks functions in the process to customise explorer behaviour.

#include <Windows.h>
#include <string>
#include <algorithm>
#include <DbgHelp.h>

#include "MinHook.h"
#include "shared.h"
#include "symbol_resolver.h"
#include "windhawk_common.h"
#include "modifier.h"


namespace
{
    void WaitForExitSignal()
    {
        // loop until signalled: check for named pipe to exist
        while (!PathExists(ExitSignalPipeName))
        {
            // yield before next call
            // (1 second wait to avoid a tight loop)
            Sleep(1000);
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE moduleHandle, DWORD reason, LPVOID reserved)
{
    LogLine(L"injected.dll: DllMain called, reason: %d", reason);

    // always return false, so this dll is automatically unloaded on return
    const BOOL ret = FALSE;

    // only execute on dll attach (load library)
    if (reason != DLL_PROCESS_ATTACH)
    {
        return ret;
    }

    // disable thread attach/detach calls
    DisableThreadLibraryCalls(moduleHandle);

    // get the process name that is calling us
    auto processPath = GetExecuablePath();

    // we only hook (a single thread) in explorer
    std::transform(processPath.begin(), processPath.end(), processPath.begin(), tolower);
    auto isExplorer = processPath.ends_with(L"explorer.exe");
    if (!isExplorer)
    {
        return ret;
    }

    // get the full directory of this module
    auto modulePath = GetModulePath(moduleHandle);
    auto moduleDir = GetBaseName(modulePath);

    // load the symbol address from file
    SymbolResolver symbolResolver;
    auto symbolCacheFilePath = moduleDir + L"\\" + SymbolCacheFileName;
    if (!symbolResolver.LoadSymbolsFromFile(symbolCacheFilePath))
    {
        return ret;
    }

    // initialise minhook
    LogLine(L"Initialising minhook");
    if (MH_Initialize() != MH_OK)
    {
        LogLine(L"Failed to initialise minhook");
        return ret;
    }

    // setup mods
    bool modsSetupOk = true;
    auto modifiers = Modifiers();
    for (auto& modifier : modifiers)
    {
        modsSetupOk &= modifier->Setup(symbolResolver);
    }
    
    // all setup ok?
    if (modsSetupOk)
    {
        // keep running until signalled
        LogLine(L"Waiting for exit signal");
        WaitForExitSignal();
    }

    // clean up
    LogLine(L"Un-initialising minhook");
    if (MH_Uninitialize() != MH_OK)
    {
        LogLine(L"Failed to clean up minhook");
    }

    return ret;
}
