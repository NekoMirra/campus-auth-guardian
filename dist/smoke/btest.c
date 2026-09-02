#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef int32_t(__stdcall* MddBootstrapInitialize_t)(uint32_t majorMinorVersion, char const* versionTag, void* packageMinVersion);

int main()
{
    HMODULE h = LoadLibraryW(L"Microsoft.WindowsAppRuntime.Bootstrap.dll");
    if (!h) { printf("LOAD-FAIL %lu\n", GetLastError()); return 1; }
    printf("loaded ok\n");
    MddBootstrapInitialize_t init = (MddBootstrapInitialize_t)GetProcAddress(h, "MddBootstrapInitialize2");
    if (!init) init = (MddBootstrapInitialize_t)GetProcAddress(h, "MddBootstrapInitialize");
    if (!init) { printf("GETPROC-FAIL %lu\n", GetLastError()); return 2; }
    printf("proc ok\n");
    int32_t rc = init(0x00010008, NULL, NULL);
    printf("init rc=%d (0x%X)\n", rc, rc);
    printf(rc == 0 ? "BOOTSTRAP-OK\n" : "BOOTSTRAP-FAIL\n");
    return 0;
}
