/**
 * @file DbgHelpDll.h
 * @brief Header file for the DbgHelpDll class, which loads the DbgHelp library
 *        and provides functions for "unmangling" function signatures.
 * @author Kerby
 * @date 2022-12-20
 */
#pragma once

#include <DbgHelp.h>

class DbgHelpDll
{
public:
	DbgHelpDll(void);
	bool Load(void);
public:
	~DbgHelpDll(void);
public:
	DWORD UnDecorateSymbolName(const char* DecoratedName, char* UnDecoratedName, DWORD UndecoratedLength, DWORD Flags);
};
