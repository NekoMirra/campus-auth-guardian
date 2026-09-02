#include <windows.h>
int WINAPI wWinMain(HINSTANCE h, HINSTANCE p, PWSTR c, int n) {
    MessageBoxW(0, L"hello", L"test", 0);
    return 0;
}
