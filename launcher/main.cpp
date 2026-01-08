// application entry point
// injects injected.dll into explorer.exe

#include <iostream>
#include <fstream>
#include <string>
#include <Windows.h>
#include <tlhelp32.h>

#include "shared.h"
#include "windhawk_common.h"
#include "symbol_resolver.h"
#include "modifier.h"


namespace
{
    // An invalid process id
    // (process ids are divisible by 4, so MAXDWORD is not possible)
    const DWORD INVALID_PROCESS_ID = MAXDWORD;

    // Get the root directory path (of this executable)
    std::wstring GetBasePath()
    {
        auto execuablePath = GetExecuablePath();
        auto executableDir = GetBaseName(execuablePath);
        return executableDir;
    }

    // Get the full path to the dll file to inject
    // Returns empty string on error
    std::wstring GetInjectedFilePath()
    {
        auto executableDir = GetBasePath();

        auto dllPath = executableDir + L"\\injected.dll";
        if (!PathExists(dllPath))
        {
            LogLine(L"DLL to inject does not exist in launcher directory: %s", dllPath.c_str());
            return std::wstring();
        }

        return dllPath;
    }

    // Get the id of the first "explorer.exe" process
    // Returns INVALID_PROCESS_ID if not found or error
    DWORD GetFirstExplorerProcessId()
    {
        // enumerate processes
        // https://learn.microsoft.com/en-us/windows/win32/toolhelp/taking-a-snapshot-and-viewing-processes

        // take a snapshot of all processes in the system
        auto snapshot = safe_create_snapshot();
        if (snapshot.get() == INVALID_HANDLE_VALUE)
        {
            LogLine(L"Failed to enumerate processes");
            return INVALID_PROCESS_ID;
        }

        // setup process information store
        PROCESSENTRY32 process = { 0 };
        process.dwSize = sizeof(PROCESSENTRY32);

        // query first process
        if (!Process32First(snapshot.get(), &process))
        {
            LogLine(L"Failed to enumerate first process");
            return INVALID_PROCESS_ID;
        }

        // iterate remaining processes, until we find the first explorer process
        DWORD explorerProcessId = INVALID_PROCESS_ID;
        do
        {
            // is this an explorer process?
            // (expect only one in a stable system)
            const std::wstring explorer(L"explorer.exe");
            if (explorer.compare(0, MAX_PATH, process.szExeFile) == 0)
            {
                explorerProcessId = process.th32ProcessID;
                break;
            }

            // setup for next query
            process.dwSize = sizeof(PROCESSENTRY32);
        } while (Process32Next(snapshot.get(), &process));

        // did we find explorer.exe?
        if (explorerProcessId == INVALID_PROCESS_ID)
        {
            // failed to find
            // because we could not enumerate all processes?
            DWORD dwError = GetLastError();
            if (dwError == ERROR_NO_MORE_FILES)
            {
                LogLine(L"Failed to enumerate all processes");
            }

            LogLine(L"Failed to find explorer.exe");
        }

        return explorerProcessId;
    }

    // Cache the symbol address required by the mods.
    bool LookupSymbols()
    {
        // lookup the symbols from the symbol server
        SymbolResolver symbolResolver;
        auto modifiers = Modifiers();
        for (auto& modifier : modifiers)
        {
            if (!symbolResolver.LoadSymbolsFromServer(modifier->GetHooks()))
            {
                return false;
            }
        }

        // cache to disk (for use in injected.dll, which cannot use the symbol server)
        auto execuablePath = GetExecuablePath();
        auto executableDir = GetBaseName(execuablePath);
        auto symbolCacheFilePath = executableDir + L"\\" + SymbolCacheFileName;
        if (!symbolResolver.SaveSymbolsToFile(symbolCacheFilePath))
        {
            return false;
        }

        // ok if reach here
        LogLine(L"All symbols looked up successfully");
        return true;
    }

    // Signal the injected thread to exit
    void SignalExitInjectedThread()
    {
        // log
        LogLine(L"Signalling injected thread to exit");

        // signal thread to exit: create the signal file
        auto exitSignalFilePath = GetBasePath() + L"\\" + ExitSignaFileName;
        {
            std::ofstream exitSignalFile(exitSignalFilePath);
        }

        // wait for pipe to be seen by shutdown (ie deleted)
        LogLine(L"Waiting for injected thread to exit");
        while (PathExists(exitSignalFilePath))
        {
            // yield before next call
            // (1 second wait to avoid a tight loop)
            Sleep(1000);
        }

        // yield for the dll to unload
        Sleep(0);
    }

    // Handler for console control methods
    BOOL ControlHandler(DWORD dwControlEvent)
    {
        // handle shutdown, logoff, ctrl+c and close events
        if (dwControlEvent == CTRL_SHUTDOWN_EVENT || dwControlEvent == CTRL_LOGOFF_EVENT || dwControlEvent == CTRL_C_EVENT || dwControlEvent == CTRL_CLOSE_EVENT)
        {
            SignalExitInjectedThread();
        }
    
        return true;
    }
}


int main()
{
    // check not already running
    auto exitSignalFilePath = GetBasePath() + L"\\" + ExitSignaFileName;
    if (PathExists(exitSignalFilePath))
    {
        LogLine(L"Error: the exit signal file already exists %s", exitSignalFilePath.c_str());
        return -1;
    }

    // cache symbols required by mods
    if (!LookupSymbols())
    {
        return -1;
    }

    // get the full path of the binary we want to inject
    // (narrow characters, for passing to LoadLibraryA)
    auto dllPath = GetInjectedFilePath();
    if (dllPath.empty())
    {
        return -1;
    }

    // get the id of the first explorer process
    auto explorerProcessId = GetFirstExplorerProcessId();
    if (explorerProcessId == INVALID_PROCESS_ID)
    {
        return -1;
    }

    // open the process
    const auto flags = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | SYNCHRONIZE;
    auto processHandle = safe_open_process(flags, explorerProcessId);
    if (processHandle.get() == INVALID_HANDLE_VALUE)
    {
        LogLine(L"Failed to open explorer process");
        return -1;
    }

    // allocate memory for the path to our dll to inject
    const auto allocationSize = dllPath.size() * sizeof(wchar_t);
    auto allocation = SafeAlloc(processHandle, allocationSize, PAGE_READWRITE);
    if (allocation.get() == nullptr)
    {
        LogLine(L"Failed to allocate memory in explorer process");
        return -1;
    }

    // write the dll to load's path in the allocated memory
    if (!WriteProcessMemory(processHandle.get(), allocation.get(), dllPath.c_str(), allocationSize, nullptr))
    {
        LogLine(L"Failed to write memory in explorer process");
        return -1;
    }

    // start (and await) thread in the process which will load the dll (and spawn the modifier thread)
    {
        SafeCreateRemoteThread threadHandle(processHandle.get(), LPTHREAD_START_ROUTINE(LoadLibraryW), allocation.get());
        if (threadHandle.get() == NULL)
        {
            std::cout << "Failed to create load thread in process " << explorerProcessId << std::endl;
            return -1;
        }
    }

    // ok, successfully injected
    LogLine(L"Successfully injected into explorer process");

    // handle shutdown
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)ControlHandler, TRUE);

    // need to keep running (so our RAII objects stay open)
    std::cout << "Press enter to quit:";
    (void)std::cin.get();

    // user signalled, trigger the injected thread to exit
    SignalExitInjectedThread();

    return 0;
}
