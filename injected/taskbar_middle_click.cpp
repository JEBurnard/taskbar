// Based on https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-click.wh.cpp
// Licence GPL-3.0-only

#include "taskbar.h"

#include <Windows.h>
#include <psapi.h>
#include <atomic>
#include <iostream>

#include "MinHook.h"
#include "common.h"


namespace 
{
// ...

void* CImmersiveTaskItem_vftable;
LPVOID g_pCTaskListWndHandlingClick;
LPVOID g_pCTaskListWndTaskBtnGroup;
int g_CTaskListWndTaskItemIndex = -1;
int g_CTaskListWndClickAction = -1;


// Orignal hooked functions

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

} // namespace


// Entry point for setting up
void setup_taskbar_middle_click()
{
    auto winVersion = GetExplorerVersion();
    if (winVersion == WinVersion::Unsupported) 
    {
        LogLine(L"Unsupported Windows version");
        return;
    }

    auto taskbarModule = LoadLibrary(L"taskbar.dll");
    if (!taskbarModule) 
    {
        LogLine(L"Couldn't load taskbar.dll");
        return;
    }

    std::vector<SYMBOL_HOOK> symbolHooks = {
        {
            //public: virtual long __cdecl CTaskListWnd::HandleClick(struct ITaskGroup *,struct ITaskItem *,struct winrt::Windows::System::LauncherOptions const &)
            {"Taskbar.dll!?HandleClick@CTaskListWnd@@UEAAJPEAUITaskGroup@@PEAUITaskItem@@AEBULauncherOptions@System@Windows@winrt@@@Z"},
            (void**)& CTaskListWnd_HandleClick_Original,
            CTaskListWnd_HandleClick_Hook,
        },
        {
            {R"(protected: void __cdecl CTaskListWnd::_HandleClick(struct ITaskBtnGroup *,int,enum CTaskListWnd::eCLICKACTION,int,int))"},
            (void**)&CTaskListWnd__HandleClick_Original,
            CTaskListWnd__HandleClick_Hook,
        },
        {
            {R"(public: virtual long __cdecl CTaskBand::Launch(struct ITaskGroup *,struct tagPOINT const &,enum LaunchFromTaskbarOptions))"},
            (void**)&CTaskBand_Launch_Original,
            CTaskBand_Launch_Hook,
        },
        {
            {"(public: virtual long __cdecl CTaskListWnd::GetActiveBtn(struct ITaskGroup * *,int *))"},
            (void**)&CTaskListWnd_GetActiveBtn_Original,
        },
        {
            {R"(public: virtual void __cdecl CTaskListWnd::ProcessJumpViewCloseWindow(struct HWND__ *,struct ITaskGroup *,struct HMONITOR__ *))"},
            (void**)&CTaskListWnd_ProcessJumpViewCloseWindow_Original,
        },
        {
            {"(protected: void __cdecl CTaskBand::_EndTask(struct HWND__ * const,int))"},
            (void**)&CTaskBand__EndTask_Original,
        },
        {
            {"(public: virtual enum eTBGROUPTYPE __cdecl CTaskBtnGroup::GetGroupType(void))"},
            (void**)&CTaskBtnGroup_GetGroupType_Original,
        },
        {
            {"(public: virtual struct ITaskGroup * __cdecl CTaskBtnGroup::GetGroup(void))"},
            (void**)&CTaskBtnGroup_GetGroup_Original,
        },
        {
            {"(public: virtual struct ITaskItem * __cdecl CTaskBtnGroup::GetTaskItem(int))"},
            (void**)&CTaskBtnGroup_GetTaskItem_Original,
        },
        {
            {"(public: virtual struct HWND__ * __cdecl CWindowTaskItem::GetWindow(void))"},
            (void**)&CWindowTaskItem_GetWindow_Original,
        },
        {
            {"(public: virtual struct HWND__ * __cdecl CImmersiveTaskItem::GetWindow(void))"},
            (void**)&CImmersiveTaskItem_GetWindow_Original,
        },
        {
            {"(const CImmersiveTaskItem::`vftable'{for `ITaskItem'})"},
            (void**)&CImmersiveTaskItem_vftable,
        },
    };

    if (!HookSymbols(taskbarModule, symbolHooks)) {
        LogLine(L"HookSymbols failed");
        return;
    }

    // not needed?:
    /*
    HMODULE kernelBaseModule = GetModuleHandle(L"kernelbase.dll");
    auto pKernelBaseLoadLibraryExW = (decltype(&LoadLibraryExW))GetProcAddress(kernelBaseModule, "LoadLibraryExW");
    WindhawkUtils::Wh_SetFunctionHookT(pKernelBaseLoadLibraryExW, LoadLibraryExW_Hook, &LoadLibraryExW_Original);
    */
}
