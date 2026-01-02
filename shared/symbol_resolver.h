// Helper to resolve symbols from a symbol server or file
#pragma once

#include "shared.h"

#include <vector>
#include <string>
#include <map>

class IResolveSymbols
{
public:
    virtual ~IResolveSymbols() = default;

    // Get the address of the symbol specified.
    virtual void* GetSymbolAddress(const std::string& moduleName, const std::string& symbolName) const = 0;
};

class SymbolResolver : public IResolveSymbols
{
public:
    SymbolResolver();
    virtual ~SymbolResolver();

    // Use a symbol server (dbghelp) to find debug symbols for the modules specified.
    // Cache (in memory) the symbols and their addresses.
    bool LoadSymbolsFromServer(const std::vector<ModuleHook>& moduleHooks);

    // Load the cached symbols and their addresses from a file.
    bool LoadSymbolsFromFile(const std::wstring& filePath);

    // Persist the current cache to file
    bool SaveSymbolsToFile(const std::wstring& filePath);

    // IResolveSymbols methods
    virtual void* GetSymbolAddress(const std::string& moduleName, const std::string& symbolName) const;

private:
    // Symbol server process id (if used)
    HANDLE symbolProcessHandle;

    // Map of fully qualified symbol name (ie includes the module prefix)
    // to cached address of symbol including base address of module
    std::map<std::string, void*> symbols;
};
