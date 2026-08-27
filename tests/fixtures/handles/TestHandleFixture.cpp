#include <windows.h>

extern "C" __declspec(dllexport) unsigned long long CreateOwnedHandle()
{
    return static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(
        CreateEventA(nullptr, TRUE, FALSE, nullptr)));
}

extern "C" __declspec(dllexport) unsigned long long EchoHandle(unsigned long long value)
{
    return value;
}
