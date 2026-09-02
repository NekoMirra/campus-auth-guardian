@echo off
:: MSVC x64 env (bypass broken vcvars: system VSINSTALLDIR conflict)
set "VCTOOLS=D:\AI\VisualStudio\VC\Tools\MSVC\14.50.35717"
set "WINKIT=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set "PATH=%VCTOOLS%\bin\Hostx64\x64;%WINKIT%\bin\%SDKVER%\x64;%PATH%"
set "INCLUDE=%VCTOOLS%\include;%WINKIT%\Include\%SDKVER%\ucrt;%WINKIT%\Include\%SDKVER%\um;%WINKIT%\Include\%SDKVER%\shared;%WINKIT%\Include\%SDKVER%\winrt"
set "LIB=%VCTOOLS%\lib\x64;%VCTOOLS%\lib\onecore\x64;%WINKIT%\Lib\%SDKVER%\um\x64;%WINKIT%\Lib\%SDKVER%\ucrt\x64"
set "RUSTC_LINKER=D:\AI\VisualStudio\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64\link.exe"
set "RUSTFLAGS=-C link-arg=/LIBPATH:D:/AI/VisualStudio/VC/Tools/MSVC/14.50.35717/lib/x64 -C link-arg=/LIBPATH:C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/um/x64 -C link-arg=/LIBPATH:C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/ucrt/x64 -C link-arg=/LIBPATH:D:/AI/VisualStudio/VC/Tools/MSVC/14.50.35717/lib/onecore/x64"
set "RUSTUP_HOME=D:\UserJunctions\.rustup"
set "CARGO_HOME=D:\UserJunctions\.cargo"
set "PATH=D:\UserJunctions\.cargo\bin;%PATH%"
