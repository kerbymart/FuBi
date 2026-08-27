/**
 * @file Fubi.cpp
 * @brief Implementation of the Fubi class, which wraps functions from a DLL in a function table.
 * @author Kerby
 * @date 2022-12-20
 */
#include "StdAfx.h"
#include "Fubi.h"

#include <algorithm>

Fubi::Fubi(void)
{
}

Fubi::~Fubi(void)
{
}

Result Fubi::Call_function(std::string funcName, const void* args)
{
    int size = 0;
    DWORD res = 0;
    std::string res_type("");
    for ( functionVec::iterator it = sys.m_Functions.begin() ; it != sys.m_Functions.end(); ++it ) {
        const bool nameMatches = it->m_Name == funcName ||
            std::find(it->m_ExportNames.begin(), it->m_ExportNames.end(), funcName) !=
                it->m_ExportNames.end();
        if (nameMatches && it->m_CallType == "__cdecl")
        {
            res = Call_cdecl(args, size,it->m_dwAddress);
            res_type = it->m_ReturnType;
        }
    }

    Result result;
    result.res_type = res_type;
    result.res = res;
    return result;
}

DWORD Fubi::Call_cdecl( const void* args, size_t sz, uintptr_t func )
{
#if defined(_M_X64)
    if (sz != 0) return 0;
    return reinterpret_cast<DWORD(*)()>(func)();
#else
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
#endif
}

DWORD Fubi::Call_stdcall( const void* args, size_t sz, uintptr_t func )
{
#if defined(_M_X64)
    if (sz != 0) return 0;
    return reinterpret_cast<DWORD(*)()>(func)();
#else
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
#endif
}

DWORD Fubi::Call_thiscall( const void* args, size_t sz, void* object, uintptr_t func )
{
#if defined(_M_X64)
    (void)args; (void)sz; (void)object; (void)func;
    return 0;
#else
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
#endif
}

DWORD Fubi::Call_thiscallvararg( const void* args, size_t sz, void* object, uintptr_t func )
{
#if defined(_M_X64)
    (void)args; (void)sz; (void)object; (void)func;
    return 0;
#else
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
#endif
}
