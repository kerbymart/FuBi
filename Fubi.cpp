#include "StdAfx.h"
#include "Fubi.h"

Fubi::Fubi(void)
{
}

Fubi::~Fubi(void)
{
}

DWORD Fubi::Call_function(std::string funcName, const void* args)
{
	int size = 0;
	DWORD res = 0; // should be null
	std::string res_type(""); // should add this to the return value Map<string,DWORD>
	for ( functionVec::iterator it = sys.m_Functions.begin() ; it != sys.m_Functions.end(); ++it ) {	
		if ( !it->m_Name.compare(funcName) && !it->m_CallType.compare("__cdecl") )
		{
			res = Call_cdecl(args, size,it->m_dwAddress);	
			res_type = it->m_ReturnType;
		}
	} 	
	return res;
}

DWORD Fubi::Call_cdecl( const void* args, size_t sz, DWORD func )
{
    DWORD rc;               // here's our return value...
    __asm
    {
        mov   ecx, sz       // get size of buffer
        mov   esi, args     // get buffer
        sub   esp, ecx      // allocate stack space
        mov   edi, esp      // start of destination stack frame
        shr   ecx, 2        // make it dwords
        rep   movsd         // copy params to real stack
        call  [func]        // call the function
        mov   rc,  eax      // save the return value
        add   esp, sz       // restore the stack pointer
    }
    return ( rc );
}

DWORD Fubi::Call_stdcall( const void* args, size_t sz, DWORD func )
{
    DWORD rc;               // here's our return value...
    __asm
    {
        mov   ecx, sz       // get size of buffer
        mov   esi, args     // get buffer
        sub   esp, ecx      // allocate stack space
        mov   edi, esp      // start of destination stack frame
        shr   ecx, 2        // make it dwords
        rep   movsd         // copy it
        call  [func]        // call the function
        mov   rc,  eax      // save the return value
    }
    return ( rc );
}

DWORD Fubi::Call_thiscall( const void* args, size_t sz, void* object, DWORD func )
{
    DWORD rc;               // here's our return value...
    _asm
    {
        mov   ecx, sz       // get size of buffer
        mov   esi, args     // get buffer
        sub   esp, ecx      // allocate stack space
        mov   edi, esp      // start of destination stack frame
        shr   ecx, 2        // make it dwords
        rep   movsd         // copy it
        mov   ecx, object   // set "this"
        call  [func]        // call the function
        mov   rc,  eax      // save the return value
    }
    return ( rc );
}

DWORD Fubi::Call_thiscallvararg( const void* args, size_t sz, void* object, DWORD func )
{
    DWORD rc;               // here's our return value...
    _asm
    {
        mov   ecx, sz       // get size of buffer
        mov   esi, args     // get buffer
        sub   esp, ecx      // allocate stack space
        mov   edi, esp      // start of destination stack frame
        shr   ecx, 2        // make it dwords
        rep   movsd         // copy it
        mov   ecx, object   // set "this"
        push  ecx           // push it on the stack
        call  [func]        // call the function
        mov   rc,  eax      // save the return value
        mov   eax, sz       // ready to restore stack pointer
        add   eax, 4        // pushed ecx too
        add   esp, eax      // restore the stack pointer
    }
    return ( rc );
}