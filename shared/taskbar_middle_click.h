// Taskbar middle click modification
#pragma once

#include "modifier.h"


class TaskbarMiddleClick : public IExplorerModifier
{
public:
    TaskbarMiddleClick();
    virtual ~TaskbarMiddleClick() = default;

    // IExplorerModifier methods
    virtual const std::vector<ModuleHook>& GetHooks() const;
    virtual bool Setup(const IResolveSymbols& symbolResolver);

private:
    std::vector<ModuleHook> moduleHooks;
};
