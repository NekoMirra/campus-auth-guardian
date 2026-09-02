@echo off
:: Manual ARM64 build: BuildTools 14.44 ARM64 cross toolchain
setlocal
cd /d "%~dp0.."

set "BTVS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "BTMSVC=%BTVS%\VC\Tools\MSVC\14.44.35207"
set "WINKIT=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"

set "PATH=D:\UserJunctions\scoop\apps\llvm\21.1.8\bin;%BTMSVC%\bin\Hostx64\arm64;%WINKIT%\bin\%SDKVER%\x64;%PATH%"
set "INCLUDE=%BTMSVC%\include;%WINKIT%\Include\%SDKVER%\ucrt;%WINKIT%\Include\%SDKVER%\um;%WINKIT%\Include\%SDKVER%\shared;%WINKIT%\Include\%SDKVER%\winrt;app\Generated"
set "LIB=%BTMSVC%\lib\arm64;%WINKIT%\Lib\%SDKVER%\um\arm64;%WINKIT%\Lib\%SDKVER%\ucrt\arm64"

set "CC=clang-cl"
set "CXX=clang-cl"
set "CFLAGS=--target=aarch64-pc-windows-msvc"
set "CXXFLAGS=--target=aarch64-pc-windows-msvc"
set "RUSTUP_HOME=D:\UserJunctions\.rustup"
set "CARGO_HOME=D:\UserJunctions\.cargo"
set "PATH=D:\UserJunctions\.cargo\bin;%PATH%"

:: 1) Rust ARM64 static lib
cargo build --release --target aarch64-pc-windows-msvc -p guardian-core
if errorlevel 1 goto fail

if not exist obj\ARM64\Release mkdir obj\ARM64\Release
if not exist dist\ARM64\Release mkdir dist\ARM64\Release

:: 2) C++ sources -> ARM64 objs
cl /nologo /utf-8 /std:c++20 /EHsc /O2 /GL /c ^
  /Iapp /Iapp\Generated /I"%WINKIT%\Include\%SDKVER%\cppwinrt" ^
  /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
  /Foobj\ARM64\Release\ ^
  app\pch.cpp app\App.cpp app\MainWindow.cpp app\combase_shim.cpp
if errorlevel 1 goto fail

:: 3) Link
link /nologo /MACHINE:ARM64 /LTCG /OUT:dist\ARM64\Release\CampusAuthGuardian.exe /SUBSYSTEM:WINDOWS ^
  obj\ARM64\Release\pch.obj obj\ARM64\Release\App.obj obj\ARM64\Release\MainWindow.obj obj\ARM64\Release\combase_shim.obj ^
  target\aarch64-pc-windows-msvc\release\guardian_core.lib ^
  ntdll.lib advapi32.lib ws2_32.lib bcrypt.lib userenv.lib iphlpapi.lib crypt32.lib synchronization.lib kernel32.lib ole32.lib oleaut32.lib user32.lib shell32.lib gdi32.lib ucrt.lib msvcrt.lib ^
  /MANIFEST:EMBED /MANIFESTINPUT:app\app.manifest
if errorlevel 1 goto fail

echo ARM64 BUILD OK: dist\ARM64\Release\CampusAuthGuardian.exe
exit /b 0

:fail
echo ARM64 BUILD FAILED
exit /b 1
