// Taskbar grouping modification
#pragma once

#include "modifier.h"


class TaskbarGrouping : public IExplorerModifier
{
public:
    TaskbarGrouping();
    virtual ~TaskbarGrouping() = default;

    // IExplorerModifier methods
    virtual const std::vector<ModuleHook>& GetHooks() const;
    virtual bool Setup(const IResolveSymbols& symbolResolver);

private:
    std::vector<ModuleHook> moduleHooks;
};
