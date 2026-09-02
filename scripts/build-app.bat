@echo off
:: Full build: restore -> Rust -> WinUI3 MSBuild
:: Usage: build-app.bat [x64|ARM64] [Debug|Release]
setlocal
set "ARCH=%~1"
set "CONF=%~2"
if "%ARCH%"=="" set "ARCH=x64"
if "%CONF%"=="" set "CONF=Release"

set "ROOT=%~dp0.."
cd /d "%ROOT%"

echo === [1/3] NuGet restore ===
tools\nuget.exe restore app\packages.config -PackagesDirectory packages -NonInteractive
if errorlevel 1 goto fail

echo === [2/3] Rust core (%ARCH%) ===
if /I "%ARCH%"=="arm64" (
  call scripts\env-arm64.bat
  set "RUST_TARGET=aarch64-pc-windows-msvc"
) else (
  call scripts\env-x64.bat
  set "RUST_TARGET=x86_64-pc-windows-msvc"
)
cargo build --release --target %RUST_TARGET% -p guardian-core
if errorlevel 1 goto fail

echo === [3/3] WinUI3 app (%ARCH% %CONF%) ===
set "MSBUILD=D:\AI\VisualStudio\MSBuild\Current\Bin\MSBuild.exe"

"%MSBUILD%" app\CampusAuthGuardian.vcxproj /p:Platform=%ARCH% /p:Configuration=%CONF% /m /v:m
if errorlevel 1 goto fail

echo BUILD OK: dist\%ARCH%\%CONF%\CampusAuthGuardian.exe
exit /b 0

:fail
echo BUILD FAILED
exit /b 1
