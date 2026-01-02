// Helper to resolve symbols from a symbol server or file

#include "symbol_resolver.h"

#include <fstream>
#include <DbgHelp.h>

#include "shared.h"
#include "windhawk_common.h"


namespace
{
    // Setup the symbol resolver
    // returns INVALID_HANDLE_VALUE on error
    HANDLE SetupSymbolServer()
    {
        // enable debug logs for symbol resolving
        (void)SymSetOptions(SYMOPT_DEBUG);

        // duplicate the current process handle for running the symbol resolver
        HANDLE hProcess = INVALID_HANDLE_VALUE;
        HANDLE hCurrentProcess = GetCurrentProcess();
        if (!DuplicateHandle(hCurrentProcess, hCurrentProcess, hCurrentProcess, &hProcess, 0, FALSE, DUPLICATE_SAME_ACCESS))
        {
            LogLine(L"Error: DuplicateHandle returned error: %d", GetLastError());
            return INVALID_HANDLE_VALUE;
        }

        // initialise the symbol resolver
        if (!SymInitialize(hProcess, NULL, FALSE))
        {
            LogLine(L"Error: SymInitialize returned error: %d", GetLastError());
            (void)CloseHandle(hProcess);
            return INVALID_HANDLE_VALUE;
        }

        // load symbols from microsoft symbol server (caches in the "sym" folder in the working directory)
        // needs SymSrv.dll and SrcSrv.dll to be in the same directory as (the dynamically loaded) DbgHelp.dll
        std::string symbolServer = "srv*https://msdl.microsoft.com/download/symbols";
        if (!SymSetSearchPath(hProcess, symbolServer.c_str()))
        {
            LogLine(L"Error: SymSetSearchPath returned error: %d", GetLastError());
            (void)SymCleanup(hProcess);
            (void)CloseHandle(hProcess);
            return INVALID_HANDLE_VALUE;
        }

        // ok if reach here
        return hProcess;
    }
}


SymbolResolver::SymbolResolver()
    : symbolProcessHandle(INVALID_HANDLE_VALUE)
{ }

SymbolResolver::~SymbolResolver()
{
    if (symbolProcessHandle != INVALID_HANDLE_VALUE)
    {
        (void)SymCleanup(symbolProcessHandle);
        (void)CloseHandle(symbolProcessHandle);
        symbolProcessHandle = INVALID_HANDLE_VALUE;
    }
}

bool SymbolResolver::LoadSymbolsFromServer(const std::vector<ModuleHook>& moduleHooks)
{
    // need to setup the symbol server?
    if (symbolProcessHandle == INVALID_HANDLE_VALUE)
    {
        symbolProcessHandle = SetupSymbolServer();
        if (symbolProcessHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }
    }

    // temporary for symbols resolved in this call
    std::map<std::string, void*> newSymbols;
    
    // add all the modules' symbols' hooks
    bool error = false;
    for (const auto& moduleHook : moduleHooks)
    {
        // load the module
        if (!SymLoadModuleEx(symbolProcessHandle, NULL, moduleHook.modulePath.c_str(), moduleHook.moduleName.c_str(), 0, 0, NULL, 0))
        {
            LogLine(L"Error: SymLoadModuleEx for %S returned error: %d", moduleHook.modulePath.c_str(), GetLastError());
            error = true;
            break;
        }

        // resolve the symbols
        for (const auto& symbolHook : moduleHook.symbolHooks)
        {
            // create store for symbol lookup
            struct CFullSymbol : SYMBOL_INFO {
                CHAR nameBuffer[MAX_SYM_NAME];
            } symbol;
            ZeroMemory(&symbol, sizeof(symbol));
            symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol.MaxNameLen = MAX_SYM_NAME;

            // prefix symbol name with module name
            auto symbolName = moduleHook.moduleName + "!" + symbolHook.symbolName;

            // log
            LogLine(L"Finding symbol: %S", symbolName.c_str());

            // lookup the symbol
            if (!SymFromName(symbolProcessHandle, symbolName.c_str(), &symbol))
            {
                LogLine(L"Error: SymFromName returned error: %d", GetLastError());
                error = true;
                break;
            }

            // add to temporary cache
            auto symbolAddress = (void*)symbol.Address;
            newSymbols.insert({ symbolName, symbolAddress });
        }

        if (error)
        {
            break;
        }
    }

    // add the fully successful symbols only
    if (!error)
    {
        symbols.insert(newSymbols.begin(), newSymbols.end());
    }

    return (!error);
}

bool SymbolResolver::LoadSymbolsFromFile(const std::wstring& filePath)
{
    // temporary for symbols read in this call
    std::map<std::string, void*> newSymbols;

    try
    {
        // open the file
        std::ifstream file(filePath);

        // read each line
        // <symbol> <address dec><eol>
        std::string line;
        while (std::getline(file, line))
        {
            // split the line
            auto pos = line.find(" ");
            if (pos == std::string::npos)
            {
                return false;
            }
            auto symbolName = line.substr(0, pos);
            auto symbolAddressDec = line.substr(pos + 1);

            // parse the address
            auto symbolAddress = std::stoll(symbolAddressDec);
            auto symbolAddressPtr = (void*)symbolAddress;
            newSymbols.insert({ symbolName, symbolAddressPtr });
        }
    }
    catch (const std::exception& e)
    {
        LogLine(L"Error: failed to read symbols from file '%s': %S", filePath.c_str(), e.what());
        return false;
    }

    // add the fully successful symbols only
    symbols.insert(newSymbols.begin(), newSymbols.end());
    return true;
}

bool SymbolResolver::SaveSymbolsToFile(const std::wstring& filePath)
{
    try
    {
        // overwrite the file
        std::ofstream file(filePath, std::ios_base::trunc);

        // write out:
        // <symbol> <address dec><eol>
        for (const auto& symbol : symbols)
        {
            file << symbol.first << " " << symbol.second << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        LogLine(L"Error: failed to save symbols to file '%s': %S", filePath.c_str(), e.what());
        return false;
    }

    return true;
}

void* SymbolResolver::GetSymbolAddress(const std::string& moduleName, const std::string& symbolName) const
{
    auto fullSymbolName = moduleName + "!" + symbolName;
    auto match = symbols.find(fullSymbolName);
    if (match == symbols.end())
    {
        return nullptr;
    }

    return match->second;
}
