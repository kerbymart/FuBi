/**
 * @file DbgHelpDll.h
 * @brief Header file for the DbgHelpDll class, which loads the DbgHelp library
 *        and provides functions for "unmangling" function signatures.
 * @author Kerby
 * @date 2022-12-20
 */
#pragma once

#include <DbgHelp.h>
#include "FunctionCatalog.h"

#include <string>
#include <vector>

struct SymbolPrototypeEvidence
{
    uint32_t rva = 0;
    std::string name;
    PrototypeSpec prototype;
};

class DbgHelpDll
{
public:
	DbgHelpDll(void);
	bool Load(void);
public:
	~DbgHelpDll(void);
public:
	DWORD UnDecorateSymbolName(const char* DecoratedName, char* UnDecoratedName, DWORD UndecoratedLength, DWORD Flags);
	bool EnumerateExactFunctionSymbols(const std::string& imagePath,
	    const ModuleIdentity& expected, std::vector<SymbolPrototypeEvidence>& symbols,
	    std::string& error);

private:
    HMODULE handle_ = nullptr;
};
