// The dll that will be injected into the explorer.exe process.
// Hooks functions in the process to customise explorer behaviour.

#include <Windows.h>
#include <string>
#include <algorithm>
#include <DbgHelp.h>

#include "MinHook.h"
#include "taskbar.h"
#include "common.h"

BOOL WINAPI DllMain(HINSTANCE modules, DWORD reason, LPVOID reserved)
{
	LogLine(L"injected.dll: DllMain called, reason: %d", reason);

	// get the process name that is calling us
	wchar_t pName[MAX_PATH];
	GetModuleFileNameW(NULL, pName, MAX_PATH);
	auto name = std::wstring(pName);
	std::transform(name.begin(), name.end(), name.begin(), tolower);
	auto isExplorer = name.ends_with(L"explorer.exe");

	// we only hook (a single thread) in explorer
	if (isExplorer && reason == DLL_PROCESS_ATTACH)
	{
		// disable thread attach/detach calls
		DisableThreadLibraryCalls(modules);

		// initalise minhook
		LogLine(L"Initalising minhook");
		if (MH_Initialize() != MH_OK)
		{
			LogLine(L"Failed to initalise minhook");
			return FALSE;
		}

		// setup mods
		bool modsSetupOk = true;
		modsSetupOk &= setup_taskbar_middle_click();

		// all setup ok?
		if (modsSetupOk)
		{
			// keep running untill signalled
			LogLine(L"Waiting for exit signal");
			WaitForExitSignal();
		}

		// clean up
		LogLine(L"Uninitalising minhook");
		if (MH_Uninitialize() != MH_OK)
		{
			LogLine(L"Failed to clean up minhook");
		}

		// return false to unload this dll
		return FALSE;
	}

	return TRUE;
}
