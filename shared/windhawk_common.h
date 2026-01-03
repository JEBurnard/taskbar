// WindHawk Common functionality
// Licence GPL-3.0-only
#pragma once

#include <string>
#include <vector>
#include <Windows.h>


// Log a debug message
// Based on https://github.com/ramensoftware/windhawk-mods/blob/main/src/windhawk/shared/logger_base.cpp
void LogLine(PCWSTR format, ...);

// Detected window version
// Based on https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-click.wh.cpp
enum class WinVersion
{
    Unsupported,
    Win10,
    Win11,
    Win11_24H2,
};

// Detect windows version from explorer
// Based on https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-click.wh.cpp
WinVersion GetExplorerVersion();
