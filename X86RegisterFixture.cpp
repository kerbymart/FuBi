#include <windows.h>
#include <cstring>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA(instance, modulePath, MAX_PATH);
        char* slash = strrchr(modulePath, '\\');
        if (slash != nullptr) *(slash + 1) = '\0';
        strcat_s(modulePath, "x86_register_fixture.executed");
        HANDLE marker = CreateFileA(modulePath, GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (marker != INVALID_HANDLE_VALUE) CloseHandle(marker);
    }
    return TRUE;
}
