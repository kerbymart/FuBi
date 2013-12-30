#pragma once
#include "stdafx.h"
#include "SysExports.h"

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
	DWORD Call_function(std::string funcName, const void* args);
};
