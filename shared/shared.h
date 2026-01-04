// Shared code
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <Windows.h>


// Filewho's presence indicates the injected thread should exit
const std::wstring ExitSignaFileName = L".shutdown.flag";

// File name to store/retrieve the symbol cache
// (relative to the executable/module binary directory)
const std::wstring SymbolCacheFileName = L"symbols.dat";


// RAII wrapper for handles
typedef std::shared_ptr<void> SafeHandle;

// Call CreateToolhelp32Snapshot and return a safe handle
SafeHandle safe_create_snapshot();

// Call OpenProcess and return a safe handle
SafeHandle safe_open_process(DWORD flags, DWORD processId);


// RAII wrapper for memory allocations in other processes
class SafeAlloc
{
public:
    SafeAlloc(SafeHandle process, SIZE_T size, DWORD protection);
    virtual ~SafeAlloc();

    SafeAlloc(const SafeAlloc&) = delete;
    SafeAlloc& operator=(SafeAlloc other) = delete;

    HANDLE get() const;

private:
    SafeHandle _process;
    void* _allocation;
};

// RAII wrapper for creating a remote thread
class SafeCreateRemoteThread
{
public:
    SafeCreateRemoteThread(HANDLE process, LPTHREAD_START_ROUTINE startAddress, LPVOID parameter);
    virtual ~SafeCreateRemoteThread();

    SafeCreateRemoteThread(const SafeCreateRemoteThread&) = delete;
    SafeCreateRemoteThread& operator=(SafeCreateRemoteThread other) = delete;

    HANDLE get() const;

private:
    HANDLE _thread;
};


// A symbol to be hooked from a module.
struct SymbolHook
{
    // symbol to hook
    std::string symbolName;

    // output function pointer to set to the original
    void** pOriginalFunction;

    // optional replacement function
    void* pHookFunction;
};

// A module to be hooked.
struct ModuleHook
{
    // name to use when loading the module
    // eg "Taskbar.dll", is then prefixed to symbolNames
    std::string moduleName;

    // full path to module 
    // eg "C:\Windows\System32\Taskbar.dll
    std::string modulePath;

    // symbols to hook
    std::vector<SymbolHook> symbolHooks;
};


// Get the full path to the current executable
std::wstring GetExecuablePath();

// Get the full path to the module specified
std::wstring GetModulePath(HINSTANCE moduleHandle);

// Get the directory of the file path specified
// (does not include the trailing slash)
std::wstring GetBaseName(const std::wstring& path);

// Check if a path exists
bool PathExists(const std::wstring& path);
