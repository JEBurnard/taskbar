// Modifiers that alter the behaviour of windows (explorer).
#pragma once

#include <vector>
#include <memory>

#include "shared.h"
#include "symbol_resolver.h"


// Interface of a class that modifies windows explorer.
class IExplorerModifier
{
public:
    virtual ~IExplorerModifier() = default;

    // Get the module hooks this modifier requires.
    virtual const std::vector<ModuleHook>& GetHooks() const = 0;

    // Perform the hooking
    virtual bool Setup(const IResolveSymbols& symbolResolver) = 0;
};

// Perform the hooks, by reading the symbols cached in a resolver.
bool HookSymbols(const std::vector<ModuleHook>& moduleHooks, const IResolveSymbols& symbolResolver);

// Get all the modifiers.
std::vector<std::unique_ptr<IExplorerModifier>> Modifiers();
