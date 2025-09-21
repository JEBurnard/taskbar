// Based on: https://github.com/ramensoftware/windhawk/blob/main/src/windhawk/engine/functions.cpp
// Licence GPL-3.0-only
#include "functions.h"

#include <Windows.h>
#include <vector>
#include <string>
#include <ranges>

namespace
{
    // Source:
    // https://github.com/dotnet-bot/corert/blob/8928dfd66d98f40017ec7435df1fbada113656a8/src/Native/Runtime/windows/PalRedhawkCommon.cpp#L78
    //
    // Given the OS handle of a loaded module, compute the upper and lower virtual
    // address bounds (inclusive).
    void PalGetModuleBounds(HANDLE hOsHandle, _Out_ BYTE** ppLowerBound, _Out_ BYTE** ppUpperBound) 
    {
        BYTE* pbModule = (BYTE*)hOsHandle;
        DWORD cbModule;

        IMAGE_NT_HEADERS* pNtHeaders = (IMAGE_NT_HEADERS*)(pbModule + ((IMAGE_DOS_HEADER*)hOsHandle)->e_lfanew);
        if (pNtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        {
            cbModule = ((IMAGE_OPTIONAL_HEADER32*)&pNtHeaders->OptionalHeader)->SizeOfImage;
        }
        else
        {
            cbModule = ((IMAGE_OPTIONAL_HEADER64*)&pNtHeaders->OptionalHeader)->SizeOfImage;
        }

        *ppLowerBound = pbModule;
        *ppUpperBound = pbModule + cbModule - 1;
    }
}

namespace Functions 
{
    std::vector<std::wstring_view> SplitStringToViews(std::wstring_view s, WCHAR delim) 
    {
        // https://stackoverflow.com/a/48403210
        auto view = s | std::views::split(delim) | std::views::transform([](auto&& rng) 
        {
            return std::wstring_view(rng.data(), rng.size());
        });
        return std::vector<std::wstring_view>(view.begin(), view.end());
    }
    
    // Based on:
    // https://github.com/dotnet-bot/corert/blob/8928dfd66d98f40017ec7435df1fbada113656a8/src/Native/Runtime/windows/PalRedhawkCommon.cpp#L109
    //
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
    bool ModuleGetPDBInfo(HANDLE hOsHandle, _Out_ GUID* pGuidSignature, _Out_ DWORD* pdwAge) 
    {
        // Zero-init [out]-params
        ZeroMemory(pGuidSignature, sizeof(*pGuidSignature));
        *pdwAge = 0;

        BYTE* pbModule = (BYTE*)hOsHandle;

        IMAGE_NT_HEADERS const* pNtHeaders = (IMAGE_NT_HEADERS*)(pbModule + ((IMAGE_DOS_HEADER*)hOsHandle)->e_lfanew);
        IMAGE_DATA_DIRECTORY const* rgDataDirectory = NULL;
        if (pNtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        {
            rgDataDirectory = ((IMAGE_OPTIONAL_HEADER32 const*)&pNtHeaders->OptionalHeader)->DataDirectory;
        }
        else
        {
            rgDataDirectory =((IMAGE_OPTIONAL_HEADER64 const*)&pNtHeaders->OptionalHeader)->DataDirectory;
        }

        IMAGE_DATA_DIRECTORY const* pDebugDataDirectory = &rgDataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];

        // In Redhawk, modules are loaded as MAPPED, so we don't have to worry about
        // dealing with FLAT files (with padding missing), so header addresses can
        // be used as is
        IMAGE_DEBUG_DIRECTORY const* rgDebugEntries = (IMAGE_DEBUG_DIRECTORY const*)(pbModule + pDebugDataDirectory->VirtualAddress);
        DWORD cbDebugEntries = pDebugDataDirectory->Size;
        if (cbDebugEntries < sizeof(IMAGE_DEBUG_DIRECTORY))
        {
            return false;
        }

        // Since rgDebugEntries is an array of IMAGE_DEBUG_DIRECTORYs,
        // cbDebugEntries should be a multiple of sizeof(IMAGE_DEBUG_DIRECTORY).
        if (cbDebugEntries % sizeof(IMAGE_DEBUG_DIRECTORY) != 0)
        {
            return false;
        }

        // CodeView RSDS debug information -> PDB 7.00
        struct CV_INFO_PDB70 
        {
            DWORD magic;
            GUID signature;                 // unique identifier
            DWORD age;                      // an always-incrementing value
            _Field_z_ char path[MAX_PATH];  // zero terminated string with the name of the PDB file
        };

        // Temporary storage for a CV_INFO_PDB70 and its size (which could be less
        // than sizeof(CV_INFO_PDB70); see below).
        struct PdbInfo 
        {
            CV_INFO_PDB70* m_pPdb70;
            ULONG m_cbPdb70;
        };

        // Grab module bounds so we can do some rough sanity checking before we
        // follow any RVAs
        BYTE* pbModuleLowerBound = NULL;
        BYTE* pbModuleUpperBound = NULL;
        PalGetModuleBounds(hOsHandle, &pbModuleLowerBound, &pbModuleUpperBound);

        // Iterate through all debug directory entries. The convention is that
        // debuggers & profilers typically just use the very last
        // IMAGE_DEBUG_TYPE_CODEVIEW entry.  Treat raw bytes we read as untrusted.
        PdbInfo pdbInfoLast = { 0 };
        int cEntries = cbDebugEntries / sizeof(IMAGE_DEBUG_DIRECTORY);
        for (int i = 0; i < cEntries; i++) 
        {
            if ((BYTE*)(&rgDebugEntries[i]) + sizeof(rgDebugEntries[i]) >= pbModuleUpperBound) 
            {
                // Bogus pointer
                return false;
            }

            if (rgDebugEntries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW)
            {
                continue;
            }

            // Get raw data pointed to by this IMAGE_DEBUG_DIRECTORY

            // AddressOfRawData is generally set properly for Redhawk modules, so we
            // don't have to worry about using PointerToRawData and converting it to
            // an RVA
            if (rgDebugEntries[i].AddressOfRawData == NULL)
            {
                continue;
            }

            DWORD rvaOfRawData = rgDebugEntries[i].AddressOfRawData;
            ULONG cbDebugData = rgDebugEntries[i].SizeOfData;
            if (cbDebugData < size_t(&((CV_INFO_PDB70*)0)->magic) + sizeof(((CV_INFO_PDB70*)0)->magic)) 
            {
                // raw data too small to contain magic number at expected spot, so
                // its format is not recognizeable. Skip
                continue;
            }

            // Verify the magic number is as expected
            const DWORD CV_SIGNATURE_RSDS = 0x53445352;
            CV_INFO_PDB70* pPdb70 = (CV_INFO_PDB70*)(pbModule + rvaOfRawData);
            if ((BYTE*)(pPdb70)+cbDebugData >= pbModuleUpperBound) 
            {
                // Bogus pointer
                return false;
            }

            if (pPdb70->magic != CV_SIGNATURE_RSDS)
            {
                // Unrecognized magic number.  Skip
                continue;
            }

            // From this point forward, the format should adhere to the expected
            // layout of CV_INFO_PDB70. If we find otherwise, then assume the
            // IMAGE_DEBUG_DIRECTORY is outright corrupt.

            // Verify sane size of raw data
            if (cbDebugData > sizeof(CV_INFO_PDB70))
            {
                return false;
            }

            // cbDebugData actually can be < sizeof(CV_INFO_PDB70), since the "path"
            // field can be truncated to its actual data length (i.e., fewer than
            // MAX_PATH chars may be present in the PE file). In some cases, though,
            // cbDebugData will include all MAX_PATH chars even though path gets
            // null-terminated well before the MAX_PATH limit.

            // Gotta have at least one byte of the path
            if (cbDebugData < offsetof(CV_INFO_PDB70, path) + sizeof(char))
            {
                return false;
            }

            // How much space is available for the path?
            size_t cchPathMaxIncludingNullTerminator = (cbDebugData - offsetof(CV_INFO_PDB70, path)) / sizeof(char);
            // assert(cchPathMaxIncludingNullTerminator >= 1);  // Guaranteed above

            // Verify path string fits inside the declared size
            size_t cchPathActualExcludingNullTerminator = strnlen_s(pPdb70->path, cchPathMaxIncludingNullTerminator);
            if (cchPathActualExcludingNullTerminator == cchPathMaxIncludingNullTerminator) 
            {
                // This is how strnlen indicates failure--it couldn't find the null
                // terminator within the buffer size specified
                return false;
            }

            // Looks valid.  Remember it.
            pdbInfoLast.m_pPdb70 = pPdb70;
            pdbInfoLast.m_cbPdb70 = cbDebugData;
        }

        // Take the last IMAGE_DEBUG_TYPE_CODEVIEW entry we saw, and return it to
        // the caller
        if (pdbInfoLast.m_pPdb70 != NULL) 
        {
            memcpy(pGuidSignature, &pdbInfoLast.m_pPdb70->signature, sizeof(GUID));
            *pdwAge = pdbInfoLast.m_pPdb70->age;
            return true;
        }

        return false;
    }
}
