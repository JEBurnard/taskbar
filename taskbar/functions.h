// Based on: https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/functions.h
// Licence GPL-3.0-only
#pragma once

#include <Windows.h>
#include <string>
#include <vector>

namespace Functions
{
	std::vector<std::wstring_view> SplitStringToViews(std::wstring_view s, WCHAR delim);

	bool ModuleGetPDBInfo(HANDLE hOsHandle, _Out_ GUID* pGuidSignature, _Out_ DWORD* pdwAge);
}
