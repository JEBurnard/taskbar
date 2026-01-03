// WindHawk Common functionality
// Licence GPL-3.0-only

#include "windhawk_common.h"

#include <Windows.h>
#include <iostream>
#include <functional>
#include <filesystem>


namespace
{
    // Log a debug line
    // Based on https://github.com/ramensoftware/windhawk-mods/blob/main/src/windhawk/shared/logger_base.cpp
    void VLogLine(PCWSTR format, va_list args)
    {
        WCHAR buffer[1025];
        int len = _vsnwprintf_s(buffer, _TRUNCATE, format, args);
        if (len == -1)
        {
            // Truncation occurred.
            len = _countof(buffer) - 1;
        }

        while (--len >= 0 && buffer[len] == L'\n')
        {
            // Skip all newlines at the end.
        }

        // Leave only a single trailing newline.
        if (len >= 2 && len < 1023 && buffer[len + 1] == L'\n' && buffer[len + 2] == L'\n')
        {
            buffer[len + 2] = L'\0';
        }

        OutputDebugStringW(buffer);
    }

    // Get the version of a library
    // Based on https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-click.wh.cpp
    VS_FIXEDFILEINFO* GetModuleVersionInfo(HMODULE hModule, UINT* puPtrLen)
    {
        void* pFixedFileInfo = nullptr;
        UINT uPtrLen = 0;

        HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
        if (hResource)
        {
            HGLOBAL hGlobal = LoadResource(hModule, hResource);
            if (hGlobal)
            {
                void* pData = LockResource(hGlobal);
                if (pData != nullptr)
                {
                    if (!VerQueryValue(pData, L"\\", &pFixedFileInfo, &uPtrLen) || uPtrLen == 0) {
                        pFixedFileInfo = nullptr;
                        uPtrLen = 0;
                    }
                }
            }
        }

        if (puPtrLen)
        {
            *puPtrLen = uPtrLen;
        }

        return (VS_FIXEDFILEINFO*)pFixedFileInfo;
    }
}

void LogLine(PCWSTR format, ...)
{
    va_list args;
    va_start(args, format);
    VLogLine(format, args);
    va_end(args);
}

WinVersion GetExplorerVersion()
{
    VS_FIXEDFILEINFO* fixedFileInfo = GetModuleVersionInfo(nullptr, nullptr);
    if (fixedFileInfo == nullptr)
    {
        return WinVersion::Unsupported;
    }

    WORD major = HIWORD(fixedFileInfo->dwFileVersionMS);
    WORD minor = LOWORD(fixedFileInfo->dwFileVersionMS);
    WORD build = HIWORD(fixedFileInfo->dwFileVersionLS);
    WORD qfe = LOWORD(fixedFileInfo->dwFileVersionLS);

    LogLine(L"Version: %u.%u.%u.%u", major, minor, build, qfe);

    switch (major) {
    case 10:
        if (build < 22000)
        {
            return WinVersion::Win10;
        }
        else if (build < 26100)
        {
            return WinVersion::Win11;
        }
        else
        {
            return WinVersion::Win11_24H2;
        }
        break;
    }

    return WinVersion::Unsupported;
}
