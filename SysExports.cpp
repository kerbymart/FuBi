#include "StdAfx.h"
#include "SysExports.h"
#include "DbgHelpDll.h"
#include <iostream>

#define rcast reinterpret_cast

#define UNDNAME_32_BIT_DECODE	0x0800 
#define UNDNAME_COMPLETE		0x0000

SysExports::SysExports(void)
{
}

SysExports::~SysExports(void)
{
}

bool SysExports::ImportBindigs(HMODULE module)
// Description	: Iterates Export table entries
{
	SignatureParser parser;
	if ( module == NULL )
	{
		::GetModuleHandle(NULL); // get HMODULE
	}

	// Find the export table.

		// get headers
		const BYTE* imageBase = rcast<const BYTE*> ( module );
		const IMAGE_DOS_HEADER* dosHeader = rcast<const IMAGE_DOS_HEADER*> ( module );
		const IMAGE_NT_HEADERS* winHeader = rcast<const IMAGE_NT_HEADERS*> 
			( imageBase + dosHeader->e_lfanew );

		// find the import data directory
		const IMAGE_DATA_DIRECTORY& exportDataDir = winHeader->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXPORT ];
		DWORD exportRva = exportDataDir.VirtualAddress;
		DWORD exportSize = exportDataDir.Size;

		if ( exportRva == 0 ) 
		{
			return ( false );
		}
		DWORD exportBegin = exportRva,
			  exportEnd = exportBegin + exportSize;
		const IMAGE_EXPORT_DIRECTORY* exportDir = rcast<const IMAGE_EXPORT_DIRECTORY*> 
			( imageBase + exportBegin );

		// find subtables
		const DWORD* funcTable = rcast<const DWORD*> ( imageBase + exportDir->AddressOfFunctions );
		const DWORD* nameTable = rcast<const DWORD*> ( imageBase + exportDir->AddressOfNames );
		const WORD* ordinalTable = rcast<const WORD*> ( imageBase + exportDir->AddressOfNameOrdinals );
		
		const DWORD* nameTableIter,
				   * nameTableBegin = nameTable,
				   * nameTableEnd = nameTableBegin + exportDir->NumberOfFunctions;
		const WORD* ordinalTableIter = ordinalTable;

		wchar_t * szBuf = new wchar_t;

		for ( nameTableIter = nameTableBegin ; 
			  nameTableIter != nameTableEnd ;
			  nameTableIter++, ordinalTableIter++){
			
			const char* functionName = rcast<const char*> ( imageBase + *nameTableIter);
			DWORD functionAddress = funcTable[ *ordinalTableIter ];
			
			if ( ( functionAddress < exportBegin ) || ( functionAddress > exportEnd ) )
			{
				functionAddress += rcast<DWORD>( imageBase );	

				// import the function.
				if ( !ImportFunction( functionName, functionAddress, &parser)){
					return false;
				}
			}
		}
		
	return true;
}

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
	}
	std::cout << unmangledName << "\n";
	++i;
	return true;
}

//
// Utility funciton to print Table Data.
//
void SysExports::PrintFunctionInfo() {
		std::cout << "*********************************\n";
		std::cout << "* Printing function information *\n";
		std::cout << "*********************************\n";
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
