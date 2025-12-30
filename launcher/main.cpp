// application entry point
// injects taskbar.dll into explorer.exe

#include <Windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <DbgHelp.h>

namespace
{
	// RAII wrapper for handles
	typedef std::shared_ptr<void> SafeProcess;
	
	SafeProcess safe_create_snapshot()
	{
		auto handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		return SafeProcess(handle, CloseHandle);
	}
	
	SafeProcess safe_open_process(DWORD flags, DWORD processId)
	{
		auto handle = OpenProcess(flags, FALSE, processId);
		return SafeProcess(handle, CloseHandle);
	}

	// RAII wrapper for memory allocations in other processes
	class SafeAlloc
	{
	public:
		SafeAlloc(SafeProcess process, SIZE_T size, DWORD protection)
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
		SafeProcess _process;
		void* _allocation;
	};
}

int main()
{
	// test: load symbols
	// 
	// need:
	// SymSrv.dll and SrcSrv.dll must be installed in the same directory as DbgHelp.dll
	// debugging tools for windows needed for SymSrv:
	// C:\Program Files (x86)\Windows Kits\10\Debuggers\x64
	// = need to copy those to output / run there?
	
	//std::string symbolServer = "srv*DownstreamStore*https://msdl.microsoft.com/download/symbols";
	std::string symbolServer = "srv*https://msdl.microsoft.com/download/symbols";
	//std::string symbolServer = "D:\\Data\\Programming\\Projects\\Windows Taskbar\\dll";
	
	std::string modulePath = "C:\\Windows\\System32\\Taskbar.dll";
	std::string moduleName = "Taskbar.dll";

	//std::string symbolName = R"(public: virtual long __cdecl CTaskListWnd::HandleClick(struct ITaskGroup *,struct ITaskItem *,struct winrt::Windows::System::LauncherOptions const &))";
	std::string symbolName = "Taskbar.dll!?HandleClick@CTaskListWnd@@UEAAJPEAUITaskGroup@@PEAUITaskItem@@AEBULauncherOptions@System@Windows@winrt@@@Z";
	
	//SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	SymSetOptions(SYMOPT_DEBUG);
	//std::cout << " SymGetOptions: " << SymGetOptions() << std::endl;

	//HANDLE hProcess = INVALID_HANDLE_VALUE;
	HANDLE hCurrentProcess = GetCurrentProcess();
	/*if (!DuplicateHandle(hCurrentProcess, hCurrentProcess, hCurrentProcess, &hProcess, 0, FALSE, DUPLICATE_SAME_ACCESS))
	{
		std::cout << "DuplicateHandle returned error: " << GetLastError() << std::endl;
		return FALSE;
	}*/

	if (!SymInitialize(hCurrentProcess, NULL, FALSE))
	{
		std::cout << "SymInitialize returned error: " << GetLastError() << std::endl;
		return FALSE;
	}

	// load symbols from symbol server
	if (!SymSetSearchPath(hCurrentProcess, symbolServer.c_str()))
	{
		std::cout << "SymSetSearchPath returned error: " << GetLastError() << std::endl;
		return FALSE;
	}
	/*std::string searchPath(512, '\0');
	if (!SymGetSearchPath(hCurrentProcess, searchPath.data(), searchPath.length()))
	{
		std::cout << "SymGetSearchPath returned error: " << GetLastError() << std::endl;
		return FALSE;
	}
	std::cout << "Search path: " << searchPath << std::endl;*/

	// load symbols for the module
	if (!SymLoadModuleEx(hCurrentProcess, NULL, modulePath.c_str(), moduleName.c_str(), 0, 0, NULL, 0))
	{
		std::cout << "SymLoadModuleEx returned error: " << GetLastError() << std::endl;
		return false;
	}

	// load the module (to trigger symbol load)?
	/*auto taskbarModule = LoadLibrary(L"taskbar.dll");
	if (!taskbarModule)
	{
		std::cout << "Couldn't load taskbar.dll" << std::endl;
		return false;
	}*/

	// lookup this symbol
	std::cout << "Finding symbol: " << symbolName.c_str() << std::endl;
	ULONG64 buffer[(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(WCHAR) + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
	PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
	pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	pSymbol->MaxNameLen = MAX_SYM_NAME;
	if (!SymFromName(hCurrentProcess, symbolName.c_str(), pSymbol))
	{
		std::cout << "SymFromName returned error: " << GetLastError() << std::endl;
		return false;
	}

	std::cout << "Worked!" << std::endl;
	return 0;

	// todo: SymCleanup 

	////////////////////


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

	// iterate remaining processes
	do
	{
		// skip non explorer.exe processes
		const std::wstring explorer(L"explorer.exe");
		if (explorer.compare(0, MAX_PATH, process.szExeFile) != 0)
		{
			continue;
		}

		// open the process
		const auto flags = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | SYNCHRONIZE;
		auto processHandle = safe_open_process(flags, process.th32ProcessID);
		if (processHandle.get() == INVALID_HANDLE_VALUE)
		{
			std::cout << "Failed to open explorer process " << process.th32ProcessID << std::endl;
			continue;
		}

		// allocate memory for the path to our dll to inect
		auto allocation = SafeAlloc(processHandle, MAX_PATH, PAGE_READWRITE);
		if (allocation.get() == nullptr)
		{
			std::cout << "Failed to allocate memory in process " << process.th32ProcessID << std::endl;
			continue;
		}

		// write the dll to load's path in the allocated memory
		if (!WriteProcessMemory(processHandle.get(), allocation.get(), dllPath, MAX_PATH, nullptr))
		{
			std::cout << "Failed to write memory in process " << process.th32ProcessID << std::endl;
			continue;
		}

		// start a thread in the process which will load the dll
		// and leave it running
		HANDLE h_thread = CreateRemoteThread(processHandle.get(), nullptr, NULL, LPTHREAD_START_ROUTINE(LoadLibraryA), allocation.get(), NULL, nullptr);
		if (!h_thread) 
		{
			std::cout << "Failed to create thread in process " << process.th32ProcessID << std::endl;
			continue;
		}

		// ok, successfully injected
		std::cout << "Successfully injected into process " << process.th32ProcessID << std::endl;

		// need to keep running
		std::cout << "Press enter to quit:";
		(void)std::cin.get();

		// todo: need to stop / unload thread etc?

		// setup for next query
		process.dwSize = sizeof(PROCESSENTRY32);
	} while (Process32Next(snapshot.get(), &process));

	// check for end
	DWORD dwError = GetLastError();
	if (dwError != ERROR_NO_MORE_FILES)
	{
		std::cout << "Failed to enumerate all processes" << std::endl;
	}

	// done
	std::cout << "Done" << std::endl;
	return 0;
}
