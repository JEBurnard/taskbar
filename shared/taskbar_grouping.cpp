// Based on https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-grouping.wh.cpp
// Licence GPL-3.0-only

#include "taskbar_grouping.h"

#include <Windows.h>
#include <shlobj.h>
#include <functional>

#include "MinHook.h"
#include "windhawk_common.h"
#include "modifier.h"


namespace
{
    // A resolved (added to taskbar) window
    struct RESOLVEDWINDOW
    {
        HWND hButtonWnd;
        WCHAR szPathStr[MAX_PATH];
        WCHAR szAppIdStr[MAX_PATH];
        ITEMIDLIST* pAppItemIdList;
        HWND hThumbInsertBeforeWnd;
        HWND hThumbParentWnd;
        BOOL bSetPinnableAndLaunchable;
        BOOL bSetThumbFlag;
    };

    // Original hooked function
    using CTaskBand__HandleItemResolved_t = void(WINAPI*)(PVOID pThis, RESOLVEDWINDOW* resolvedWindow, PVOID taskListUI, PVOID taskGroup, PVOID taskItem);
    CTaskBand__HandleItemResolved_t CTaskBand__HandleItemResolved_Original;


    // Replacement function
    void WINAPI CTaskBand__HandleItemResolved_Hook(PVOID pThis, RESOLVEDWINDOW* resolvedWindow, PVOID taskListUI, PVOID taskGroup, PVOID taskItem)
    {
        LogLine(L"CTaskBand__HandleItemResolved_Hook");

        // remove the group applications, so each (unique id we be in its own group)
        if (resolvedWindow->pAppItemIdList)
        {
            ILFree(resolvedWindow->pAppItemIdList);
            resolvedWindow->pAppItemIdList = nullptr;
        }

        // make the app id unique
        static DWORD counter = 0;
        counter = counter + 1;
        size_t len = wcslen(resolvedWindow->szAppIdStr);
        swprintf_s(resolvedWindow->szAppIdStr + len, MAX_PATH - len, L":WM:%08X", counter);
        LogLine(L"- %s", resolvedWindow->szAppIdStr);

        CTaskBand__HandleItemResolved_Original(pThis, resolvedWindow, taskListUI, taskGroup, taskItem);
    }
}


TaskbarGrouping::TaskbarGrouping()
    : moduleHooks{
        {
            .moduleName = "Taskbar.dll",
            .modulePath = "C:\\Windows\\System32\\Taskbar.dll",
            .symbolHooks = {
                {
                    {"?_HandleItemResolved@CTaskBand@@IEAAXPEAURESOLVEDWINDOW@@PEAUITaskListUI@@PEAUITaskGroup@@PEAUITaskItem@@@Z"},
                    (void**)&CTaskBand__HandleItemResolved_Original,
                    CTaskBand__HandleItemResolved_Hook,
                },
            }
        }
    }
{ }

const std::vector<ModuleHook>& TaskbarGrouping::GetHooks() const
{
    return moduleHooks;
}

bool TaskbarGrouping::Setup(const IResolveSymbols& symbolResolver)
{
    auto winVersion = GetExplorerVersion();
    if (winVersion == WinVersion::Unsupported)
    {
        LogLine(L"Unsupported Windows version");
        return false;
    }

    return HookSymbols(moduleHooks, symbolResolver);
}
