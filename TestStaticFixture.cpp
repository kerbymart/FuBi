#include <windows.h>

#include <climits>

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
