// Common functionality
// Licence GPL-3.0-only and Unlicense
#pragma once

#include <string>
#include <vector>
#include <Windows.h>


// Log a debug message
// Based on https://github.com/ramensoftware/windhawk-mods/blob/main/src/windhawk/shared/logger_base.cpp
// Licence GPL-3.0-only
void LogLine(PCWSTR format, ...);

// Detected window version
// Based on https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-click.wh.cpp
// Licence GPL-3.0-only
enum class WinVersion 
{
    Unsupported,
    Win10,
    Win11,
    Win11_24H2,
};

// Detect windows version from explorer
// Based on https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-click.wh.cpp
// Licence GPL-3.0-only
WinVersion GetExplorerVersion();

// A hook to perform
// Based on https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/mods_api_internal.h
// Licence GPL-3.0-only
typedef struct tagWH_SYMBOL_HOOK
{
    // symbol to hook
    std::string symbol;

    // output function pointer to set to the original
    void** pOriginalFunction;

    // optional replacement function
    void* pHookFunction;
} SYMBOL_HOOK;

// Perform the hooks
// Unlicense 
bool HookSymbols(std::string& moduleName, std::vector<SYMBOL_HOOK> &symbolHooks);

// Wait for the launcher to signal that the injected thread should exit.
// Unlicence
void WaitForExitSignal();
