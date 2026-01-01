// application entry point
// injects injected.dll into explorer.exe

#include <iostream>
#include <string>
#include <Windows.h>
#include <tlhelp32.h>

#include "shared.h"


namespace
{
	// An invalid process id
	// (process ids are divisible by 4, so MAXDWORD is not possible)
	const DWORD INVALID_PROCESS_ID = MAXDWORD;

	// Get the full path to the dll file to inject
	// Returns empty string on error
	std::string GetInjectedFilePath()
	{
		auto dllName = "injected.dll";
		char dllPath[MAX_PATH] = "";
		if (!GetFullPathNameA(dllName, MAX_PATH, dllPath, nullptr))
		{
			std::cout << "Failed to get full path of " << dllName << std::endl;
			return std::string();
		}

		return std::string(dllPath);
	}

	// Get the id of the first "explorer.exe" process
	// Returns INVALID_PROCESS_ID if not found or error
	DWORD GetFirstExplorerProcessId()
	{
		// enumerate processes
		// https://learn.microsoft.com/en-us/windows/win32/toolhelp/taking-a-snapshot-and-viewing-processes

		// take a snapshot of all processes in the system
		auto snapshot = safe_create_snapshot();
		if (snapshot.get() == INVALID_HANDLE_VALUE)
		{
			std::cout << "Failed to enumerate proceses" << std::endl;
			return INVALID_PROCESS_ID;
		}

		// setup process information store
		PROCESSENTRY32 process = { 0 };
		process.dwSize = sizeof(PROCESSENTRY32);

		// query first process
		if (!Process32First(snapshot.get(), &process))
		{
			std::cout << "Failed to enumerate first process" << std::endl;
			return INVALID_PROCESS_ID;
		}

		// iterate remaining processes, untill we find the first explorer process
		DWORD explorerProcessId = INVALID_PROCESS_ID;
		do
		{
			// is this an explorer process?
			// (expect only one in a stable system)
			const std::wstring explorer(L"explorer.exe");
			if (explorer.compare(0, MAX_PATH, process.szExeFile) == 0)
			{
				explorerProcessId = process.th32ProcessID;
				break;
			}

			// setup for next query
			process.dwSize = sizeof(PROCESSENTRY32);
		} while (Process32Next(snapshot.get(), &process));

		// did we find explorer.exe?
		if (explorerProcessId == INVALID_PROCESS_ID)
		{
			// failed to find
			// because we could not enuperate all processes?
			DWORD dwError = GetLastError();
			if (dwError == ERROR_NO_MORE_FILES)
			{
				std::cout << "Failed to enumerate all processes" << std::endl;
			}

			std::cout << "Failed to find explorer.exe" << std::endl;
		}

		return explorerProcessId;
	}
}


int main()
{
	// get the full path of the binary we want to inject
	// (narrow characters, for passing to LoadLibraryA)
	auto dllPath = GetInjectedFilePath();
	if (dllPath.empty())
	{
		return -1;
	}

	// get the id of the first explorer process
	auto explorerProcessId = GetFirstExplorerProcessId();
	if (explorerProcessId == INVALID_PROCESS_ID)
	{
		return -1;
	}

	// open the process
	const auto flags = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | SYNCHRONIZE;
	auto processHandle = safe_open_process(flags, explorerProcessId);
	if (processHandle.get() == INVALID_HANDLE_VALUE)
	{
		std::cout << "Failed to open explorer process " << explorerProcessId << std::endl;
		return -1;
	}

	// allocate memory for the path to our dll to inect
	auto allocation = SafeAlloc(processHandle, MAX_PATH, PAGE_READWRITE);
	if (allocation.get() == nullptr)
	{
		std::cout << "Failed to allocate memory in process " << explorerProcessId << std::endl;
		return -1;
	}

	// write the dll to load's path in the allocated memory
	if (!WriteProcessMemory(processHandle.get(), allocation.get(), dllPath.c_str(), MAX_PATH, nullptr))
	{
		std::cout << "Failed to write memory in process " << explorerProcessId << std::endl;
		return -1;
	}

	// start a thread in the process which will load the dll
	HANDLE threadHandle = CreateRemoteThread(processHandle.get(), nullptr, NULL, LPTHREAD_START_ROUTINE(LoadLibraryA), allocation.get(), NULL, nullptr);
	if (!threadHandle)
	{
		std::cout << "Failed to create thread in process " << explorerProcessId << std::endl;
		return 01;
	}

	// ok, successfully injected
	std::cout << "Successfully injected into process " << explorerProcessId << std::endl;

	// need to keep running
	std::cout << "Press enter to quit:";
	(void)std::cin.get();

	// user closed
	std::cout << "Signaling injected thread to exit" << std::endl;

	// signal thread to exit: create a specifically named pipe
	const std::wstring pipeName = L"\\\\.\\pipe\\takbar-close-thread-pipe";
	auto pipeHandle = safe_open_pipe(pipeName);

	// wait for thread to exit
	std::cout << "Waiting for injected thread to exit" << std::endl;
	(void)WaitForSingleObject(threadHandle, INFINITE);
	(void)CloseHandle(threadHandle);
	std::cout << "Done" << std::endl;

	return 0;
}
