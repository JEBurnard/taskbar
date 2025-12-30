// application entry point
// injects injected.dll into explorer.exe

#include <Windows.h>
#include <tlhelp32.h>
#include <iostream>

namespace
{
	// RAII wrapper for handles
	typedef std::shared_ptr<void> SafeHandle;
	
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

	// RAII wrapper for memory allocations in other processes
	class SafeAlloc
	{
	public:
		SafeAlloc(SafeHandle process, SIZE_T size, DWORD protection)
			: _process(process)
			, _allocation(nullptr)
		{
			_allocation = VirtualAllocEx(process.get(), nullptr, size, MEM_RESERVE | MEM_COMMIT, protection);
		}

		virtual ~SafeAlloc()
		{
			if (_allocation != nullptr)
			{
				if (VirtualFreeEx(_process.get(), _allocation, NULL, MEM_RELEASE) != 0)
				{
					_allocation = nullptr;
				}
				else
				{
					std::cout << "Failed to free allocation" << std::endl;
				}
			}
		}

		SafeAlloc(const SafeAlloc&) = delete;
		SafeAlloc& operator=(SafeAlloc other) = delete;

		HANDLE get() const
		{
			return _allocation;
		}

	private:
		SafeHandle _process;
		void* _allocation;
	};
}

int main()
{
	// get the full path of the binary we want to inject
	// (narrow characters, for passing to LoadLibraryA)
	auto dllName = "injected.dll";
	char dllPath[MAX_PATH] = "";
	if (!GetFullPathNameA(dllName, MAX_PATH, dllPath, nullptr))
	{
		std::cout << "Failed to get full path of " << dllName << std::endl;
		return -1;
	}

	// enumerate processes
	// https://learn.microsoft.com/en-us/windows/win32/toolhelp/taking-a-snapshot-and-viewing-processes

	// take a snapshot of all processes in the system
	auto snapshot = safe_create_snapshot();
	if (snapshot.get() == INVALID_HANDLE_VALUE)
	{
		std::cout << "Failed to enumerate proceses" << std::endl;
		return -1;
	}

	// setup process information store
	PROCESSENTRY32 process = {0};
	process.dwSize = sizeof(PROCESSENTRY32);

	// query first process
	if (!Process32First(snapshot.get(), &process))
	{
		std::cout << "Failed to enumerate first process" << std::endl;
		return -1;
	}

	// iterate remaining processes, untill we find the first explorer process
	const DWORD INVALID_PROCESS_ID = MAXDWORD;
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
	if (INVALID_PROCESS_ID == MAXDWORD)
	{
		// failed to find
		// because we could not enuperate all processes?
		DWORD dwError = GetLastError();
		if (dwError != ERROR_NO_MORE_FILES)
		{
			std::cout << "Failed to enumerate all processes" << std::endl;
		}

		std::cout << "Failed to find explorer.exe" << std::endl;
		return -1;
	}

	// do while false error catcher loop
	do
	{
		// open the process
		const auto flags = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | SYNCHRONIZE;
		auto processHandle = safe_open_process(flags, process.th32ProcessID);
		if (processHandle.get() == INVALID_HANDLE_VALUE)
		{
			std::cout << "Failed to open explorer process " << process.th32ProcessID << std::endl;
			break;
		}

		// allocate memory for the path to our dll to inect
		auto allocation = SafeAlloc(processHandle, MAX_PATH, PAGE_READWRITE);
		if (allocation.get() == nullptr)
		{
			std::cout << "Failed to allocate memory in process " << process.th32ProcessID << std::endl;
			break;
		}

		// write the dll to load's path in the allocated memory
		if (!WriteProcessMemory(processHandle.get(), allocation.get(), dllPath, MAX_PATH, nullptr))
		{
			std::cout << "Failed to write memory in process " << process.th32ProcessID << std::endl;
			break;
		}

		// start a thread in the process which will load the dll
		HANDLE threadHandle = CreateRemoteThread(processHandle.get(), nullptr, NULL, LPTHREAD_START_ROUTINE(LoadLibraryA), allocation.get(), NULL, nullptr);
		if (!threadHandle) 
		{
			std::cout << "Failed to create thread in process " << process.th32ProcessID << std::endl;
			break;
		}

		// ok, successfully injected
		std::cout << "Successfully injected into process " << process.th32ProcessID << std::endl;

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

	} while (false);

	// done
	std::cout << "Done" << std::endl;
	return 0;
}
