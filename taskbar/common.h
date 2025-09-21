// Common code from WindHawk
// Extracted from 
// - https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/shared/logger_base.cpp
// - https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-grouping.wh.cp
// - https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-click.wh.cpp
// - https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/mod.h
// - https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/mod.cpp
// - https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/mods_api_internal.h
// Licence GPL-3.0-only
#pragma once

#include <string>
#include <vector>
#include <Windows.h>


// Log a debug message
void LogLine(PCWSTR format, ...);

// Detected window version
enum class WinVersion 
{
    Unsupported,
    Win10,
    Win11,
    Win11_24H2,
};

// Detect windows version from explorer
WinVersion GetExplorerVersion();

// A hook to perform
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
bool HookSymbols(HMODULE module, std::vector<SYMBOL_HOOK> &symbolHooks);
