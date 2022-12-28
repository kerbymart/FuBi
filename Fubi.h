/**
 * @file Fubi.h
 * @brief Header file for the Fubi class, which acts as a wrapper for a DLL's functions and provides a
 *        function table for calling those functions.
 * @author Kerby
 * @date 2022-12-20
 */
#pragma once
#include "stdafx.h"
#include "SysExports.h"

struct Result {
    std::string res_type;
    DWORD res;
};

class Fubi
{
public:
	SysExports sys;
public:
	Fubi(void);
	~Fubi(void);
	DWORD Call_cdecl( const void* args, size_t sz, DWORD func );
	DWORD Call_stdcall( const void* args, size_t sz, DWORD func );
	DWORD Call_thiscall( const void* args, size_t sz, void* object, DWORD func );
	DWORD Call_thiscallvararg( const void* args, size_t sz, void* object, DWORD func );
    Result Call_function(std::string funcName, const void* args);
};
