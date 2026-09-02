#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef int32_t(__stdcall* MddBootstrapInitialize_t)(uint32_t majorMinorVersion, char const* versionTag, void* packageMinVersion);
typedef int32_t(__stdcall* RoGetActivationFactory_t)(void* classId, void* iid, void** factory);
typedef int32_t(__stdcall* WindowsCreateString_t)(const wchar_t* src, uint32_t len, void** hstr);

int main()
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    HMODULE h = LoadLibraryW(L"Microsoft.WindowsAppRuntime.Bootstrap.dll");
    if (!h) { printf("LOAD-FAIL %lu\n", GetLastError()); return 1; }
    MddBootstrapInitialize_t init = (MddBootstrapInitialize_t)GetProcAddress(h, "MddBootstrapInitialize");
    if (!init) { printf("GETPROC-FAIL\n"); return 2; }
    int32_t rc = init(0x00010008, NULL, NULL);
    printf("bootstrap rc=%d\n", rc);
    if (rc != 0) return 3;

    HMODULE combase = GetModuleHandleW(L"combase.dll");
    if (!combase) combase = LoadLibraryW(L"combase.dll");
    RoGetActivationFactory_t roGet = (RoGetActivationFactory_t)GetProcAddress(combase, "RoGetActivationFactory");
    WindowsCreateString_t mkStr = (WindowsCreateString_t)GetProcAddress(combase, "WindowsCreateString");

    void* hstr = NULL;
    const wchar_t* name = L"Microsoft.UI.Xaml.Application";
    mkStr(name, 29, &hstr);

    // IApplicationStatics = 4E0D09F5-4358-512C-A987-503B52848E95
    static const unsigned char iid[16] = {
        0xF5,0x09,0x0D,0x4E, 0x58,0x43, 0x2C,0x51, 0xA9,0x87,0x50,0x3B,0x52,0x84,0x8E,0x95
    };
    void* factory = NULL;
    int32_t arc = roGet(hstr, (void*)iid, &factory);
    printf("activate rc=0x%08X factory=%p\n", arc, factory);
    if (arc == 0) printf("ACTIVATION-OK\n");
    else if (arc == -2147221006 /*0x80040154*/) printf("CLASS-NOT-REGISTERED\n");
    else printf("ACTIVATION-ERR\n");
    return 0;
}
