// application entry point
// injects injected.dll into explorer.exe

#include <iostream>
#include <fstream>
#include <string>
#include <Windows.h>
#include <tlhelp32.h>

#include "shared.h"
#include "symbol_resolver.h"
#include "modifier.h"


namespace
{
    // An invalid process id
    // (process ids are divisible by 4, so MAXDWORD is not possible)
    const DWORD INVALID_PROCESS_ID = MAXDWORD;

    // Get the root directory path (of this executable)
    std::wstring GeBasePath()
    {
        auto execuablePath = GetExecuablePath();
        auto executableDir = GetBaseName(execuablePath);
        return executableDir;
    }

    // Get the full path to the dll file to inject
    // Returns empty string on error
    std::wstring GetInjectedFilePath()
    {
        auto executableDir = GeBasePath();

        auto dllPath = executableDir + L"\\injected.dll";
        if (!PathExists(dllPath))
        {
            std::wcout << "DLL to inject does not exist in launcher directory: " << dllPath << std::endl;
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
            std::cout << "Failed to enumerate processes" << std::endl;
            return INVALID_PROCESS_ID;
        }

        // setup process information store
        PROCESSENTRY32 process = { 0 };
        process.dwSize = sizeof(PROCESSENTRY32);

        // query first process
        if (!Process32First(snapshot.get(), &process))
        {
            std::cout << "Failed to enumerate first process" << std::endl;
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
                std::cout << "Failed to enumerate all processes" << std::endl;
            }

            std::cout << "Failed to find explorer.exe" << std::endl;
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
        std::cout << "All symbols looked up successfully" << std::endl;
        return true;
    }
}


int main()
{
    // check not already running
    auto exitSignalFilePath = GeBasePath() + L"\\" + ExitSignaFileName;
    if (PathExists(exitSignalFilePath))
    {
        std::wcout << "Error: the exit signal file already exists " << exitSignalFilePath << std::endl;
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
        std::cout << "Failed to open explorer process " << explorerProcessId << std::endl;
        return -1;
    }

    // allocate memory for the path to our dll to inject
    const auto allocationSize = dllPath.size() * sizeof(wchar_t);
    auto allocation = SafeAlloc(processHandle, allocationSize, PAGE_READWRITE);
    if (allocation.get() == nullptr)
    {
        std::cout << "Failed to allocate memory in process " << explorerProcessId << std::endl;
        return -1;
    }

    // write the dll to load's path in the allocated memory
    if (!WriteProcessMemory(processHandle.get(), allocation.get(), dllPath.c_str(), allocationSize, nullptr))
    {
        std::cout << "Failed to write memory in process " << explorerProcessId << std::endl;
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
    std::cout << "Successfully injected into process " << explorerProcessId << std::endl;

    // need to keep running
    std::cout << "Press enter to quit:";
    (void)std::cin.get();

    // user closed
    std::cout << "Signalling injected thread to exit" << std::endl;

    // signal thread to exit: create the signal file
    {
        std::ofstream exitSignalFile(exitSignalFilePath);
    }

    // wait for pipe to be seen by shutdown (ie deleted)
    std::cout << "Waiting for injected thread to exit" << std::endl;
    while (PathExists(exitSignalFilePath))
    {
        // yield before next call
        // (1 second wait to avoid a tight loop)
        Sleep(1000);
    }

    // yield for the dll to unload
    Sleep(0);

    std::cout << "Done" << std::endl;
    return 0;
}
