#include "StdAfx.h"
#include "DbgHelpDll.h"

typedef DWORD (WINAPI *PFNUNDECORATESYMBOLNAME)(PCSTR, PSTR, DWORD, DWORD);
static PFNUNDECORATESYMBOLNAME pfnUnDecorateSymbolName = NULL;

DbgHelpDll::DbgHelpDll(void)
{
}

DbgHelpDll::~DbgHelpDll(void)
{
}

bool DbgHelpDll::Load(void)
{
	HINSTANCE hDbgHelpDLL;
	hDbgHelpDLL = LoadLibrary(_T("DbgHelp"));
	if ( hDbgHelpDLL == NULL )
	{
		return false;
	}
	pfnUnDecorateSymbolName = (PFNUNDECORATESYMBOLNAME)GetProcAddress( hDbgHelpDLL, "UnDecorateSymbolName");
	return true;
}

DWORD DbgHelpDll::UnDecorateSymbolName(const char* DecoratedName, char* UnDecoratedName, DWORD UndecoratedLength, DWORD Flags)
{
	PCSTR pszDecoratedName = (PCSTR) DecoratedName;
	PSTR pszUndecoratedName = (PSTR) UnDecoratedName;
	return ( pfnUnDecorateSymbolName( pszDecoratedName, pszUndecoratedName, UndecoratedLength , Flags ) ) ;
}
