// Based on https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-click.wh.cpp
// Licence GPL-3.0-only

#include "taskbar_middle_click.h"

#include <Windows.h>

#include "MinHook.h"
#include "windhawk_common.h"
#include "modifier.h"


namespace 
{
    // group being handled in a click
    LPVOID g_pCTaskListWndHandlingClick = nullptr;

    // button group being handled in a click
    LPVOID g_pCTaskListWndTaskBtnGroup = nullptr;

    // task item being handled in a click
    int g_CTaskListWndTaskItemIndex = -1;

    // click action being performed in a click 
    int g_CTaskListWndClickAction = -1;


    // Original hooked functions

    void* CImmersiveTaskItem_vftable;

    using CTaskListWnd_HandleClick_t = long(WINAPI*)(
        LPVOID pThis,
        LPVOID,         // ITaskGroup *
        LPVOID,         // ITaskItem *
        LPVOID          // winrt::Windows::System::LauncherOptions const &
    );
    CTaskListWnd_HandleClick_t CTaskListWnd_HandleClick_Original;

    using CTaskListWnd__HandleClick_t = void(WINAPI*)(
        LPVOID pThis,
        LPVOID,         // ITaskBtnGroup *
        int,
        int,            // enum CTaskListWnd::eCLICKACTION
        int,
        int
    );
    CTaskListWnd__HandleClick_t CTaskListWnd__HandleClick_Original;

    using CTaskBand_Launch_t = long(WINAPI*)(
        LPVOID pThis,
        LPVOID,         // ITaskGroup *
        LPVOID,         // tagPOINT const &
        int             // enum LaunchFromTaskbarOptions
    );
    CTaskBand_Launch_t CTaskBand_Launch_Original;

    using CTaskListWnd_GetActiveBtn_t = HRESULT(WINAPI*)(
        LPVOID pThis,
        LPVOID*,        // ITaskGroup **
        int*
    );
    CTaskListWnd_GetActiveBtn_t CTaskListWnd_GetActiveBtn_Original;

    using CTaskListWnd_ProcessJumpViewCloseWindow_t = void(WINAPI*)(
        LPVOID pThis,
        HWND,
        LPVOID,         // struct ITaskGroup *
        HMONITOR
    );
    CTaskListWnd_ProcessJumpViewCloseWindow_t CTaskListWnd_ProcessJumpViewCloseWindow_Original;

    using CTaskBand__EndTask_t = void(WINAPI*)(
        LPVOID pThis,
        HWND hWnd,
        BOOL bForce
    );
    CTaskBand__EndTask_t CTaskBand__EndTask_Original;

    using CTaskBtnGroup_GetGroupType_t = int(WINAPI*)(LPVOID pThis);
    CTaskBtnGroup_GetGroupType_t CTaskBtnGroup_GetGroupType_Original;

    using CTaskBtnGroup_GetGroup_t = LPVOID(WINAPI*)(LPVOID pThis);
    CTaskBtnGroup_GetGroup_t CTaskBtnGroup_GetGroup_Original;

    using CTaskBtnGroup_GetTaskItem_t = void* (WINAPI*)(LPVOID pThis, int);
    CTaskBtnGroup_GetTaskItem_t CTaskBtnGroup_GetTaskItem_Original;

    using CWindowTaskItem_GetWindow_t = HWND(WINAPI*)(LPVOID pThis);
    CWindowTaskItem_GetWindow_t CWindowTaskItem_GetWindow_Original;

    using CImmersiveTaskItem_GetWindow_t = HWND(WINAPI*)(LPVOID pThis);
    CImmersiveTaskItem_GetWindow_t CImmersiveTaskItem_GetWindow_Original;


    // Replacement functions

    long WINAPI CTaskListWnd_HandleClick_Hook(LPVOID pThis, LPVOID param1, LPVOID param2, LPVOID param3) 
    {
        LogLine(L"CTaskListWnd_HandleClick_Hook");

        g_pCTaskListWndHandlingClick = pThis;
        long ret = CTaskListWnd_HandleClick_Original(pThis, param1, param2, param3);
        g_pCTaskListWndHandlingClick = nullptr;

        return ret;
    }

    void WINAPI CTaskListWnd__HandleClick_Hook(LPVOID pThis, LPVOID taskBtnGroup, int taskItemIndex, int clickAction, int param4, int param5) 
    {
        LogLine(L"CTaskListWnd__HandleClick_Hook: %d", clickAction);

        g_pCTaskListWndTaskBtnGroup = taskBtnGroup;
        g_CTaskListWndTaskItemIndex = taskItemIndex;
        g_CTaskListWndClickAction = clickAction;

        CTaskListWnd__HandleClick_Original(pThis, taskBtnGroup, taskItemIndex, clickAction, param4, param5);

        g_pCTaskListWndTaskBtnGroup = nullptr;
        g_CTaskListWndTaskItemIndex = -1;
        g_CTaskListWndClickAction = -1;
    }

    long WINAPI CTaskBand_Launch_Hook(LPVOID pThis, LPVOID taskGroup, LPVOID param2, int param3) 
    {
        LogLine(L"CTaskBand_Launch_Hook");

        auto original = [=]() 
        {
            return CTaskBand_Launch_Original(pThis, taskGroup, param2, param3);
        };

        if (!g_pCTaskListWndHandlingClick || !g_pCTaskListWndTaskBtnGroup) 
        {
            return original();
        }

        // Get the task group from taskBtnGroup instead of relying on taskGroup for
        // compatibility with the taskbar-grouping mod, which hooks this function
        // and replaces taskGroup. An ugly workaround but it works.
        LPVOID realTaskGroup = CTaskBtnGroup_GetGroup_Original(g_pCTaskListWndTaskBtnGroup);
        if (!realTaskGroup) 
        {
            return original();
        }

        // The click action of launching a new instance can happen in two ways:
        // * Middle click.
        // * Shift + Left click.
        // Exclude the second click action by checking whether the shift key is
        // down.
        if (g_CTaskListWndClickAction != 3 || GetKeyState(VK_SHIFT) < 0) 
        {
            return original();
        }

        // Group types:
        // 1 - Single item or multiple uncombined items
        // 2 - Pinned item
        // 3 - Multiple combined items
        int groupType = CTaskBtnGroup_GetGroupType_Original(g_pCTaskListWndTaskBtnGroup);
        if (groupType != 1) 
        {
            return original();
        }

        HWND hWnd = nullptr;
        if (g_CTaskListWndTaskItemIndex >= 0)
        {
            void* taskItem = CTaskBtnGroup_GetTaskItem_Original(g_pCTaskListWndTaskBtnGroup, g_CTaskListWndTaskItemIndex);
            if (*(void**)taskItem == CImmersiveTaskItem_vftable) 
            {
                hWnd = CImmersiveTaskItem_GetWindow_Original(taskItem);
            }
            else 
            {
                hWnd = CWindowTaskItem_GetWindow_Original(taskItem);
            }
        }

        if (hWnd != nullptr)
        {
            LogLine(L"Closing HWND %08X", (DWORD)(ULONG_PTR)hWnd);

            POINT pt;
            GetCursorPos(&pt);
            HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
            CTaskListWnd_ProcessJumpViewCloseWindow_Original(g_pCTaskListWndHandlingClick, hWnd, realTaskGroup, monitor);
        }

        return 0;
    }
}


TaskbarMiddleClick::TaskbarMiddleClick()
    : moduleHooks {
        {
            .moduleName = "Taskbar.dll",
            .modulePath = "C:\\Windows\\System32\\Taskbar.dll",
            .symbolHooks = {
                {
                    {"?HandleClick@CTaskListWnd@@UEAAJPEAUITaskGroup@@PEAUITaskItem@@AEBULauncherOptions@System@Windows@winrt@@@Z"},
                    (void**)&CTaskListWnd_HandleClick_Original,
                    CTaskListWnd_HandleClick_Hook,
                },
                {
                    {"?_HandleClick@CTaskListWnd@@IEAAXPEAUITaskBtnGroup@@HW4eCLICKACTION@1@HH@Z"},
                    (void**)&CTaskListWnd__HandleClick_Original,
                    CTaskListWnd__HandleClick_Hook,
                },
                {
                    {"?Launch@CTaskBand@@UEAAJPEAUITaskGroup@@AEBUtagPOINT@@W4LaunchFromTaskbarOptions@@@Z"},
                    (void**)&CTaskBand_Launch_Original,
                    CTaskBand_Launch_Hook,
                },
                {
                    {"?GetActiveBtn@CTaskListWnd@@UEAAJPEAPEAUITaskGroup@@PEAH@Z"},
                    (void**)&CTaskListWnd_GetActiveBtn_Original,
                },
                {
                    {"?ProcessJumpViewCloseWindow@CTaskListWnd@@UEAAXPEAUHWND__@@PEAUITaskGroup@@PEAUHMONITOR__@@@Z"},
                    (void**)&CTaskListWnd_ProcessJumpViewCloseWindow_Original,
                },
                {
                    {"?_EndTask@CTaskBand@@IEAAXQEAUHWND__@@H@Z"},
                    (void**)&CTaskBand__EndTask_Original,
                },
                {
                    {"?GetGroupType@CTaskBtnGroup@@UEAA?AW4eTBGROUPTYPE@@XZ"},
                    (void**)&CTaskBtnGroup_GetGroupType_Original,
                },
                {
                    {"?GetGroup@CTaskBtnGroup@@UEAAPEAUITaskGroup@@XZ"},
                    (void**)&CTaskBtnGroup_GetGroup_Original,
                },
                {
                    {"?GetTaskItem@CTaskBtnGroup@@UEAAPEAUITaskItem@@H@Z"},
                    (void**)&CTaskBtnGroup_GetTaskItem_Original,
                },
                {
                    {"?GetWindow@CWindowTaskItem@@UEAAPEAUHWND__@@XZ"},
                    (void**)&CWindowTaskItem_GetWindow_Original,
                },
                {
                    {"?GetWindow@CImmersiveTaskItem@@UEAAPEAUHWND__@@XZ"},
                    (void**)&CImmersiveTaskItem_GetWindow_Original,
                },
                {
                    {"??_7CImmersiveTaskItem@@6BITaskItem@@@"},
                    (void**)&CImmersiveTaskItem_vftable,
                },
            }
        }
    }
{ }

const std::vector<ModuleHook>& TaskbarMiddleClick::GetHooks() const
{
    return moduleHooks;
}

bool TaskbarMiddleClick::Setup(const IResolveSymbols& symbolResolver)
{
    auto winVersion = GetExplorerVersion();
    if (winVersion == WinVersion::Unsupported) 
    {
        LogLine(L"Unsupported Windows version");
        return false;
    }

    return HookSymbols(moduleHooks, symbolResolver);
}
