// The dll that will be injected into the explorer.exe process.
// Hooks functions in the process to customise explorer behaviour.

#include <Windows.h>
#include <string>
#include <algorithm>
#include <DbgHelp.h>

#include "MinHook.h"
#include "shared.h"
#include "windhawk_common.h"
#include "taskbar_middle_click.h"

namespace
{

void WaitForExitSignal()
{
	// loop untill signalled: check for named pipe to exist
	const std::wstring pipeName = L"\\\\.\\pipe\\takbar-close-thread-pipe";

	while (true)
	{
		// query first matching file
		WIN32_FIND_DATAW findData;
		HANDLE hFind = FindFirstFileW(pipeName.c_str(), &findData);

		// found? (not error nor found)
		if (hFind != INVALID_HANDLE_VALUE)
		{
			// found, exit loop
			FindClose(hFind);
			break;
		}

		// yield before next call
		// (1 second wait to avoid a tight loop)
		Sleep(1000);
	}
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE moduleHandle, DWORD reason, LPVOID reserved)
{
	LogLine(L"injected.dll: DllMain called, reason: %d", reason);

	// always return false, so this dll is automatically unloaded on return
	const BOOL ret = FALSE;

	// only execute on dll attach (load library)
	if (reason != DLL_PROCESS_ATTACH)
	{
		return ret;
	}

	// disable thread attach/detach calls
	DisableThreadLibraryCalls(moduleHandle);

	// get the process name that is calling us
	wchar_t processPathBuffer[MAX_PATH];
	GetModuleFileNameW(NULL, processPathBuffer, MAX_PATH);
	auto processPath = std::wstring(processPathBuffer);
	std::transform(processPath.begin(), processPath.end(), processPath.begin(), tolower);
	auto isExplorer = processPath.ends_with(L"explorer.exe");
	
	// we only hook (a single thread) in explorer
	if (!isExplorer)
	{
		return ret;
	}

	// get the full path of this module
	wchar_t modulePathBuffer[MAX_PATH];
	GetModuleFileNameW(moduleHandle, modulePathBuffer, MAX_PATH);
	auto modulePath = std::wstring(modulePathBuffer);

	// set the working directory to be the directory this module is in
	// (needed for dbghelp symbol server libraries)
	auto slashPos = modulePath.rfind('\\');
	if (slashPos != std::wstring::npos)
	{
		auto moduleDirectory = modulePath.substr(0, slashPos);
		LogLine(L"Changing directory to %s", moduleDirectory.c_str());
		if (!SetCurrentDirectory(moduleDirectory.c_str()))
		{
			LogLine(L"Failed to change directory");
		}
	}

	// initalise minhook
	LogLine(L"Initalising minhook");
	if (MH_Initialize() != MH_OK)
	{
		LogLine(L"Failed to initalise minhook");
		return ret;
	}

	// setup mods
	bool modsSetupOk = true;
	TaskbarMiddleClick taskbarMiddleClick;
	modsSetupOk &= taskbarMiddleClick.Setup();

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

	return ret;
}
