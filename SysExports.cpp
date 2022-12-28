/**
 * @file SysExports.cpp
 * @brief Source file for the SysExports class, which handles the import
 *        and management of functions in a dynamic-link library (DLL).
 * @author Kerby
 * @date 2022-12-20
 */
#include "StdAfx.h"
#include "SysExports.h"
#include "DbgHelpDll.h"
#include <iostream>

#define rcast reinterpret_cast

#define UNDNAME_32_BIT_DECODE	0x0800 
#define UNDNAME_64_BIT_DECODE	0x2000
#define UNDNAME_COMPLETE		0x0000

SysExports::SysExports(void)
{
}

SysExports::~SysExports(void)
{
}

/**
 * @brief This function is responsible for importing all functions from the specified dynamic-link library (DLL).
 * @param module A handle to the DLL module that contains the functions to be imported.
 * @return Returns true if the import process was successful, false otherwise.
 */
bool SysExports::ImportBindings(HMODULE module)
{
    SignatureParser parser;
    if (module == NULL)
    {
        module = ::GetModuleHandle(NULL); // get HMODULE
    }

    // Find the export table.

    // get headers
    const BYTE* imageBase = rcast<const BYTE*>(module);
    const IMAGE_DOS_HEADER* dosHeader = rcast<const IMAGE_DOS_HEADER*>(module);
    const IMAGE_NT_HEADERS* winHeader = rcast<const IMAGE_NT_HEADERS*>
    (imageBase + dosHeader->e_lfanew);

    // find the import data directory
    const IMAGE_DATA_DIRECTORY& exportDataDir = winHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    DWORD exportRva = exportDataDir.VirtualAddress;
    DWORD exportSize = exportDataDir.Size;

    if (exportRva == 0)
    {
        return (false);
    }
    DWORD exportBegin = exportRva,
            exportEnd = exportBegin + exportSize;
    const IMAGE_EXPORT_DIRECTORY* exportDir = rcast<const IMAGE_EXPORT_DIRECTORY*>
    (imageBase + exportBegin);

    // find subtables
    const DWORD* funcTable = rcast<const DWORD*>(imageBase + exportDir->AddressOfFunctions);
    const DWORD* nameTable = rcast<const DWORD*>(imageBase + exportDir->AddressOfNames);
    const WORD* ordinalTable = rcast<const WORD*>(imageBase + exportDir->AddressOfNameOrdinals);

    const DWORD* nameTableIter,
            * nameTableBegin = nameTable,
            * nameTableEnd = nameTableBegin + exportDir->NumberOfFunctions;
    const WORD* ordinalTableIter = ordinalTable;

    wchar_t* szBuf = new wchar_t;

    for (nameTableIter = nameTableBegin;
         nameTableIter != nameTableEnd;
         nameTableIter++, ordinalTableIter++)
    {

        const char* functionName = rcast<const char*>(imageBase + *nameTableIter);
        DWORD functionAddress = funcTable[*ordinalTableIter];

        if ((functionAddress < exportBegin) || (functionAddress > exportEnd))
        {
            functionAddress += rcast<DWORD>(imageBase);
            // import the function.
            if (!ImportFunction(functionName, functionAddress, &parser))
            {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Import a function from a dynamic-link library (DLL).
 *        The function is added to the function table based on its address and its name is unmangled using
 *         the provided SignatureParser object.
 * @param mangledName The mangled name of the function as it appears in the import table of the DLL file
 * @param address address The address of the function in the DLL.
 * @param parser The SignatureParser object to use for unmangling the function name.
 * @return True if the function was successfully imported, false otherwise.
 */
bool SysExports::ImportFunction(const char* mangledName, DWORD address, SignatureParser* parser)
{
	static int i = 0;
	static DbgHelpDll s_DbgHelpDll;
	if ( !s_DbgHelpDll.Load() )
	{
		return false;
	}
	
	char unmangledName[ 0x400 ];
    if ( s_DbgHelpDll.UnDecorateSymbolName(mangledName,
			unmangledName, sizeof(unmangledName),
			UNDNAME_32_BIT_DECODE | UNDNAME_COMPLETE) == 0)
	{
		return false;
	}
	FunctionSpec spec;
	spec.m_SerialID = i;
	spec.m_dwAddress = address;

    parse_info<> r = parser->Parse(unmangledName, spec);
	if ( r.full ){
		// add to table
		m_Functions.push_back( spec );
        int numElements = m_Functions.size();
	}
	std::cout << unmangledName << "\n";
	++i;
	return true;
}

/**
* @brief Outputs the function information stored in the function map to the console.
*        This includes the Function ID, name, return type, calling convention,
 *       and parameter types for each function.
*/
void SysExports::PrintFunctionInfo() {
		std::cout << "*********************************\n";
		std::cout << "* Printing function information *\n";
		std::cout << "*********************************\n";
        int numElements = m_Functions.size();
        std::cout << "Number of function(s): " << numElements << std::endl;
		for ( functionVec::iterator it = m_Functions.begin() ; it != m_Functions.end(); ++it ) {
			std::cout << "================================" << std::endl;
			std::cout << "FID = " << it->m_SerialID << "'\n";
			std::cout << "FUN_NAME = '" << it->m_Name << "'\n";
			std::cout << "RET_TYPE = '" << it->m_ReturnType << "'\n";
			std::cout << "CAL_TYPE = '" << it->m_CallType << "'\n";
			for ( FunctionSpec::ParamVec::size_type i = 0; i < it->m_ParamTypes.size() ; ++i ){
				std::cout << "PARAM_" << i << "  = '" << it->m_ParamTypes[i] << "'\n";
			}
			std::cout << "================================" << std::endl;

		}
}
