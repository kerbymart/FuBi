#include <windows.h>

#include <climits>
#include <cstring>

namespace
{
const char kAsciiEvidence[] = "Trigger ASCII static analysis fixture";
const wchar_t kUtf16Evidence[] = L"NativeUSB UTF16 static analysis fixture";
const wchar_t kWdfComponent[] = L"FuBi UMDF fixture";

struct FixtureWdfVersion
{
    ULONG Major;
    ULONG Minor;
    ULONG Build;
};

struct FixtureWdfBindInfo
{
    ULONG Size;
    const wchar_t* Component;
    FixtureWdfVersion Version;
    ULONG FunctionCount;
    void* FunctionTable;
    void* Module;
};
}

using FixtureWdfFunction = int (*)();
extern "C" __declspec(dllexport) FixtureWdfFunction* FixtureWdfFunctions = nullptr;
extern "C" __declspec(dllexport) const FixtureWdfBindInfo FuBiWdfBindInfo = {
    sizeof(FixtureWdfBindInfo),
    kWdfComponent,
    {2, 33, 0},
    274,
    &FixtureWdfFunctions,
    nullptr
};

#ifdef _WIN64
static_assert(sizeof(FixtureWdfBindInfo) == 48, "Unexpected x64 WDF bind layout");
#else
static_assert(sizeof(FixtureWdfBindInfo) == 32, "Unexpected x86 WDF bind layout");
#endif

extern "C" __declspec(dllexport) const char* FixtureAsciiString()
{
    return kAsciiEvidence;
}

extern "C" __declspec(dllexport) const wchar_t* FixtureUtf16String()
{
    return kUtf16Evidence;
}

extern "C" __declspec(dllexport) int DelayedMessageBoxReference()
{
    return MessageBoxA(nullptr, kAsciiEvidence, "FuBi fixture", MB_OK);
}

extern "C" __declspec(dllexport) int FxDriverEntryUm()
{
    return 0;
}

extern "C" __declspec(dllexport) int WdfUsbDispatchFixture()
{
    FixtureWdfFunction* functions = FixtureWdfFunctions;
    if (!functions) return -1;
    const int result = functions[202]();
    return result == INT_MIN ? 0 : result;
}

extern "C" __declspec(noinline) int InternalAddFixture(int left, int right)
{
    volatile int scratch[16] = {};
    scratch[0] = left + right;
    return scratch[0];
}

extern "C" __declspec(dllexport) int CallInternalAdd(int left, int right)
{
    volatile int result = InternalAddFixture(left, right);
    return result;
}

extern "C" __declspec(dllexport) int WriteNarrowString(char* buffer)
{
    if (buffer == nullptr) return 0;
    const char value[] = "fubi-output";
    std::memcpy(buffer, value, sizeof(value));
    return static_cast<int>(sizeof(value) - 1);
}

extern "C" __declspec(dllexport) double MixedIntFloat(int left, float middle,
    int right, double tail)
{
    return static_cast<double>(left) + static_cast<double>(middle) +
        static_cast<double>(right) + tail;
}

extern "C" __declspec(dllexport) int MixedFloatInt(float first, int second,
    float third)
{
    return static_cast<int>(first * 10.0f) + second +
        static_cast<int>(third * 10.0f);
}

extern "C" __declspec(dllexport) double MixedPointerFloat(const int* pointer,
    float value, int third, double fourth, int fifth)
{
    return static_cast<double>(*pointer) + static_cast<double>(value) +
        static_cast<double>(third) + fourth + static_cast<double>(fifth);
}

extern "C" __declspec(dllexport) int WriteWideString(wchar_t* buffer)
{
    if (buffer == nullptr) return 0;
    const wchar_t value[] = L"fubi-wide";
    std::memcpy(buffer, value, sizeof(value));
    return static_cast<int>(sizeof(value) / sizeof(value[0]) - 1);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        HANDLE marker = CreateFileA(
            "static_fixture.executed", GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (marker != INVALID_HANDLE_VALUE) CloseHandle(marker);
    }
    return TRUE;
}
