// application entry point
// injects injected.dll into explorer.exe

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
    // Flag to indicate if we sent the shutdown signal
    static std::atomic_flag g_exitSignalSent;


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

    // Inject the dll into explorer
    bool InjectIntoExplorer()
    {
        // check not already running
        auto exitSignalFilePath = GetBasePath() + L"\\" + ExitSignaFileName;
        if (PathExists(exitSignalFilePath))
        {
            LogLine(L"Error: the exit signal file already exists %s", exitSignalFilePath.c_str());
            return false;
        }

        // cache symbols required by mods
        if (!LookupSymbols())
        {
            return false;
        }

        // get the full path of the binary we want to inject
        // (narrow characters, for passing to LoadLibraryA)
        auto dllPath = GetInjectedFilePath();
        if (dllPath.empty())
        {
            return false;
        }

        // get the id of the first explorer process
        auto explorerProcessId = GetFirstExplorerProcessId();
        if (explorerProcessId == INVALID_PROCESS_ID)
        {
            return false;
        }

        // open the process
        const auto flags = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | SYNCHRONIZE;
        auto processHandle = safe_open_process(flags, explorerProcessId);
        if (processHandle.get() == INVALID_HANDLE_VALUE)
        {
            LogLine(L"Failed to open explorer process");
            return false;
        }

        // allocate memory for the path to our dll to inject
        const auto allocationSize = dllPath.size() * sizeof(wchar_t);
        auto allocation = SafeAlloc(processHandle, allocationSize, PAGE_READWRITE);
        if (allocation.get() == nullptr)
        {
            LogLine(L"Failed to allocate memory in explorer process");
            return false;
        }

        // write the dll to load's path in the allocated memory
        if (!WriteProcessMemory(processHandle.get(), allocation.get(), dllPath.c_str(), allocationSize, nullptr))
        {
            LogLine(L"Failed to write memory in explorer process");
            return false;
        }

        // start (and await) thread in the process which will load the dll (and spawn the modifier thread)
        {
            SafeCreateRemoteThread threadHandle(processHandle.get(), LPTHREAD_START_ROUTINE(LoadLibraryW), allocation.get());
            if (threadHandle.get() == NULL)
            {
                LogLine(L"Failed to create load thread in explorer process");
                return false;
            }
        }

        // ok, successfully injected
        LogLine(L"Successfully injected into explorer process");
        return true;
    }

    // Signal the injected thread to exit
    void SignalExit()
    {
        auto exitSignalFilePath = GetBasePath() + L"\\" + ExitSignaFileName;

        // only signal once
        auto signalSent = g_exitSignalSent.test_and_set(std::memory_order_acquire);
        if (!signalSent)
        {
            // log
            LogLine(L"Signalling injected thread to exit");

            // signal thread to exit: create the signal file
            {
                std::ofstream exitSignalFile(exitSignalFilePath);
            }
        }
    }

    // Wait for the injected thread to exit
    void WaitForExit()
    {
        // wait for the file to be seen by the injected thread (ie deleted)
        LogLine(L"Waiting for injected thread to exit");
        auto exitSignalFilePath = GetBasePath() + L"\\" + ExitSignaFileName;
        while (PathExists(exitSignalFilePath))
        {
            // yield before next call
            // (1 second wait to avoid a tight loop)
            Sleep(1000);
        }

        // yield for the dll to unload
        Sleep(0);

        LogLine(L"Injected thread exited");
    }

    // Windows message handling procedure
    LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
        return 0;
    }
}


// Program entry
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
    // inject our dll into explorer
    if (!InjectIntoExplorer())
    {
        return -1;
    }

    // do while false error catcher
    do
    {
        // create a windows class
        const std::wstring className = L"Taskbar Launcher Class";
        WNDCLASSEX wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WindowProcedure;
        wc.hInstance = hInstance;
        wc.lpszClassName = className.c_str();
        if (!RegisterClassEx(&wc))
        {
            break;
        }

        // create a message only window
        const std::wstring windowName = L"Taskbar Launcher";
        auto hwnd = CreateWindowEx(0, className.c_str(), windowName.c_str(), CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, HWND_MESSAGE, NULL, hInstance, NULL);
        if (hwnd == NULL)
        {
            break;
        }

        // main loop
        MSG msg = { 0 };
        while (GetMessage(&msg, NULL, 0, 0) > 0)
        {
            (void)TranslateMessage(&msg);
            (void)DispatchMessage(&msg);
        }

    } while (false);

    // main loop exited, or failed to start
    // ensure we stop the injected process
    SignalExit();
    WaitForExit();

    return 0;
}
