#include <windows.h>

extern "C" int NamedExport()
{
    return 42;
}

extern "C" int SumEight(int first, int second, int third, int fourth,
    int fifth, int sixth, int seventh, int eighth)
{
    return first + second + third + fourth + fifth + sixth + seventh + eighth;
}

extern "C" unsigned char ReturnByte()
{
    return 0xA5;
}

extern "C" unsigned short ReturnWord()
{
    return 0xBEEF;
}

extern "C" unsigned int ReturnDword()
{
    return 0xDEADBEEF;
}

extern "C" unsigned long long ReturnQword()
{
    return 0xFEDCBA9876543210ULL;
}

extern "C" int* PointerEcho(int* value)
{
    return value;
}

extern "C" void CrashProcess()
{
    TerminateProcess(GetCurrentProcess(), 0xC0000005U);
}

extern "C" void HangProcess()
{
    Sleep(INFINITE);
}

__declspec(dllexport) int AddNumbers(int left, int right)
{
    return left + right;
}
