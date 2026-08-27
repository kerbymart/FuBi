#include <windows.h>

// This fixture deliberately keeps the two x86 calling conventions in separate
// entry points.  The functions are small, deterministic probes for the
// isolated worker's stack cleanup and scalar return handling.
extern "C" __declspec(noinline) unsigned char __cdecl CdeclReturn8()
{
    return 0xA5;
}

extern "C" __declspec(noinline) unsigned short __cdecl CdeclReturn16()
{
    return 0xBEEF;
}

extern "C" __declspec(noinline) unsigned int __cdecl CdeclReturn32()
{
    return 0xDEADBEEF;
}

extern "C" __declspec(noinline) unsigned long long __cdecl CdeclReturn64()
{
    return 0xFEDCBA9876543210ULL;
}

extern "C" __declspec(noinline) int __cdecl CdeclSum8(
    int first, int second, int third, int fourth,
    int fifth, int sixth, int seventh, int eighth)
{
    return first + second + third + fourth + fifth + sixth + seventh + eighth;
}

extern "C" __declspec(noinline) int __stdcall StdcallSum8(
    int first, int second, int third, int fourth,
    int fifth, int sixth, int seventh, int eighth)
{
    return first + second + third + fourth + fifth + sixth + seventh + eighth;
}

extern "C" __declspec(noinline) unsigned long long __stdcall StdcallReturn64()
{
    return 0x0123456789ABCDEFULL;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        HANDLE marker = CreateFileA(
            "x86_abi_fixture.executed", GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (marker != INVALID_HANDLE_VALUE) CloseHandle(marker);
    }
    return TRUE;
}
