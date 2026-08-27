#include <windows.h>

namespace
{
const char kAsciiEvidence[] = "Trigger ASCII static analysis fixture";
const wchar_t kUtf16Evidence[] = L"NativeUSB UTF16 static analysis fixture";
}

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
