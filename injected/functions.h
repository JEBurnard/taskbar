// Non-Windhawk functions
// Licence: MIT & Unlicence
#pragma once

#include <Windows.h>
#include <string>
#include <vector>

namespace Functions
{
	// Split a string based on a delimiter.
	//
	// Source: https://stackoverflow.com/a/48403210
	// Licence: Unlicence
	std::vector<std::wstring_view> SplitStringToViews(std::wstring_view s, WCHAR delim);

    // Reads through the PE header of the specified module, and returns
	// the module's matching PDB's signature GUID and age by
	// fishing them out of the last IMAGE_DEBUG_DIRECTORY of type
	// IMAGE_DEBUG_TYPE_CODEVIEW.  Used when sending the ModuleLoad event
	// to help profilers find matching PDBs for loaded modules.
	//
	// Arguments:
	//
	// [in] hOsHandle - OS Handle for module from which to get PDB info
	// [out] pGuidSignature - PDB's signature GUID to be placed here
	// [out] pdwAge - PDB's age to be placed here
	//
	// This is a simplification of similar code in desktop CLR's GetCodeViewInfo
	// in eventtrace.cpp.
	//
	// Source: https://github.com/dotnet-bot/corert/blob/8928dfd66d98f40017ec7435df1fbada113656a8/src/Native/Runtime/windows/PalRedhawkCommon.cpp#L109
	// Licence: MIT
	bool ModuleGetPDBInfo(HANDLE hOsHandle, _Out_ GUID* pGuidSignature, _Out_ DWORD* pdwAge);
}
