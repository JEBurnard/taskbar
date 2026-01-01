// Shared code
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <Windows.h>

// RAII wrapper for handles
typedef std::shared_ptr<void> SafeHandle;

// Call CreateToolhelp32Snapshot and return a safe handle
SafeHandle safe_create_snapshot();

// Call OpenProcess and return a safe handle
SafeHandle safe_open_process(DWORD flags, DWORD processId);

// Call CreateNamedPipe and return a safe handle
SafeHandle safe_open_pipe(const std::wstring& name);

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


// A symbol to be hooked from a module.
typedef struct SymbolHook
{
    // symbol to hook
    std::string symbolName;

    // output function pointer to set to the original
    void** pOriginalFunction;

    // optional replacement function
    void* pHookFunction;
} SymbolHook;

// A module to be hooked.
typedef struct ModuleHook
{
    // name to use when loading the module
    // eg "Taskbar.dll", is then prefixed to symbolNames
    std::string moduleName;

    // full path to module 
    // eg "C:\Windows\System32\Taskbar.dll
    std::string modulePath;

    // symbols to hook
    std::vector<SymbolHook> symbolHooks;
} ModuleHook;

// Interface of a class that modifies windows explorer.
class IExplorerModifier
{
    // Get the module hooks this modifier requires.
    virtual const std::vector<ModuleHook>& GetHooks() const = 0;

    // Perform the hooking
    virtual bool Setup() = 0;
};


// Lookup symbols from a symbol server and save to disk.
// (for use in hook symbols)
bool LookupSymbols(const std::vector<ModuleHook>& moduleHooks);

// Perform the hooks
bool HookSymbols(const std::vector<ModuleHook>& moduleHooks);
