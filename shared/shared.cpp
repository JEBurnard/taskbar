// Shared code

#include "shared.h"

#include <algorithm>
#include <vector>
#include <functional>
#include <filesystem>
#include <tlhelp32.h>

#include "windhawk_common.h"


SafeHandle safe_create_snapshot()
{
    auto handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    return SafeHandle(handle, CloseHandle);
}

SafeHandle safe_open_process(DWORD flags, DWORD processId)
{
    auto handle = OpenProcess(flags, FALSE, processId);
    return SafeHandle(handle, CloseHandle);
}

SafeHandle safe_open_pipe(const std::wstring& name)
{
    auto handle = CreateNamedPipe(name.c_str(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE, 1, 0, 0, 0, NULL);
    return SafeHandle(handle, CloseHandle);
}

SafeAlloc::SafeAlloc(SafeHandle process, SIZE_T size, DWORD protection)
    : _process(process)
    , _allocation(nullptr)
{
    _allocation = VirtualAllocEx(process.get(), nullptr, size, MEM_RESERVE | MEM_COMMIT, protection);
}

SafeAlloc::~SafeAlloc()
{
    if (_allocation != nullptr)
    {
        if (VirtualFreeEx(_process.get(), _allocation, NULL, MEM_RELEASE) != 0)
        {
            _allocation = nullptr;
        }
        else
        {
            LogLine(L"Failed to free allocation");
        }
    }
}

HANDLE SafeAlloc::get() const
{
    return _allocation;
}


std::wstring GetExecuablePath()
{
    wchar_t pathBuffer[MAX_PATH];
    if (!GetModuleFileNameW(NULL, pathBuffer, MAX_PATH))
    {
        return std::wstring();
    }

    return std::wstring(pathBuffer);
}

std::wstring GetModulePath(HINSTANCE moduleHandle)
{
    wchar_t pathBuffer[MAX_PATH];
    if (!GetModuleFileNameW(moduleHandle, pathBuffer, MAX_PATH))
    {
        return std::wstring();
    }

    return std::wstring(pathBuffer);
}

std::wstring GetBaseName(const std::wstring& path)
{
    auto slashPos = path.rfind('\\');
    if (slashPos == std::wstring::npos)
    {
        return std::wstring();
    }

    return path.substr(0, slashPos);
}

bool PathExists(const std::wstring& path)
{
    // query first matching file
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(path.c_str(), &findData);

    // found? (not error nor found)
    if (hFind != INVALID_HANDLE_VALUE)
    {
        // found, exit loop
        FindClose(hFind);
        return true;
    }

    return false;
}
