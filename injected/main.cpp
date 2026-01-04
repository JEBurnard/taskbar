// The dll that will be injected into the explorer.exe process.
// Hooks functions in the process to customise explorer behaviour.

#include <Windows.h>
#include <string>
#include <algorithm>
#include <atomic>
#include <DbgHelp.h>

#include "MinHook.h"
#include "shared.h"
#include "symbol_resolver.h"
#include "windhawk_common.h"
#include "modifier.h"


namespace
{
    // This module handle (if loaded)
    static HINSTANCE g_moduleHandle = nullptr;

    // The worker thread (if started)
    static HANDLE g_threadHandle = nullptr;

    // Flag to indicate this module is being unloaded
    static std::atomic<bool> g_unloading = false;


    // Wait for the exit signal
    void WaitForExitSignal()
    {
        // loop until signalled: check for named pipe to exist
        while (!g_unloading.load() && !PathExists(ExitSignalPipeName))
        {
            // yield before next call
            // (1 second wait to avoid a tight loop)
            Sleep(1000);
        }
    }

    // The main loop - run the modifiers and wait for exit
    void DoWork()
    {
        // get the process name that is calling us
        auto processPath = GetExecuablePath();

        // we only hook (a single thread) in explorer
        std::transform(processPath.begin(), processPath.end(), processPath.begin(), tolower);
        auto isExplorer = processPath.ends_with(L"explorer.exe");
        if (!isExplorer)
        {
            return;
        }

        // get the full directory of this module
        auto modulePath = GetModulePath(g_moduleHandle);
        auto moduleDir = GetBaseName(modulePath);

        // load the symbol address from file
        SymbolResolver symbolResolver;
        auto symbolCacheFilePath = moduleDir + L"\\" + SymbolCacheFileName;
        if (!symbolResolver.LoadSymbolsFromFile(symbolCacheFilePath))
        {
            return;
        }

        // initialise minhook
        LogLine(L"Initialising minhook");
        if (MH_Initialize() != MH_OK)
        {
            LogLine(L"Failed to initialise minhook");
            return;
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
        LogLine(L"Uninitialising minhook");
        if (MH_Uninitialize() != MH_OK)
        {
            LogLine(L"Failed to clean up minhook");
        }

        // unload ourself?
        if (!g_unloading.load() && g_moduleHandle != NULL)
        {
            LogLine(L"injected.dll: unloading self");
            FreeLibraryAndExitThread(g_moduleHandle, 0);
        }

        return;
    }

    // Thread function that wraps the main loop
    DWORD ThreadFunction(LPVOID lpThreadParameter)
    {
        LogLine(L"injected.dll: worker thread started");
        DoWork();
        LogLine(L"injected.dll: worker thread exited");

        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE moduleHandle, DWORD reason, LPVOID reserved)
{
    LogLine(L"injected.dll: DllMain called, reason: %d", reason);

    // disable thread attach/detach calls
    DisableThreadLibraryCalls(moduleHandle);

    if (reason == DLL_PROCESS_ATTACH)
    {
        // attach (ie load library called)
        // start our thread (if not already started)
        if (g_threadHandle == nullptr)
        {
            LogLine(L"injected.dll: starting thread");
            g_moduleHandle = moduleHandle;
            g_threadHandle = CreateThread(NULL, 0, ThreadFunction, NULL, 0, NULL);
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // detach (ie free library called)
        // cause our thread to exit
        g_unloading.store(true);
    }

    LogLine(L"injected.dll: DllMain exited");
    return TRUE;
}
