// Taskbar middle click modificiation
#pragma once

#include "shared.h"

class TaskbarMiddleClick : public IExplorerModifier
{
public:
    TaskbarMiddleClick();
    virtual ~TaskbarMiddleClick() = default;

    virtual const std::vector<ModuleHook>& GetHooks() const;
    virtual bool Setup();

private:
    std::vector<ModuleHook> moduleHooks;
};
