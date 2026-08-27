#include <windows.h>

extern "C" __declspec(noinline) int CdeclZero() { return 17; }
extern "C" __declspec(noinline) int CdeclAdd(int left, int right) { return left + right; }
extern "C" __declspec(noinline) int __stdcall StdcallAdd(int left, int right) { return left + right; }
extern "C" __declspec(noinline) int CdeclFour(int a, int b, int c, int d) { return a * 1000 + b * 100 + c * 10 + d; }
extern "C" __declspec(noinline) int CdeclEight(int a, int b, int c, int d, int e, int f, int g, int h) { return a + b + c + d + e + f + g + h; }
extern "C" __declspec(noinline) unsigned char CdeclReturn8() { return 0xA5; }
extern "C" __declspec(noinline) unsigned short CdeclReturn16() { return 0xBEEF; }
extern "C" __declspec(noinline) unsigned int CdeclReturn32() { return 0xDEADBEEF; }
extern "C" __declspec(noinline) unsigned long long CdeclReturn64() { return 0xFEDCBA9876543210ULL; }
extern "C" __declspec(noinline) unsigned long long __stdcall StdcallReturn64() { return 0x0123456789ABCDEFULL; }

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        HANDLE marker = CreateFileA("x86_fixture.executed", GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (marker != INVALID_HANDLE_VALUE) CloseHandle(marker);
    }
    return TRUE;
}
