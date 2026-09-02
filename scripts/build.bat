@echo off
:: 组合调用示例：cmd /c scripts\build.bat x64
if "%1"=="arm64" (
  call "%~dp0env-arm64.bat"
) else (
  call "%~dp0env-x64.bat"
)
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=x64"
if /I "%TARGET%"=="arm64" (
  set "RUST_TARGET=aarch64-pc-windows-msvc"
) else (
  set "RUST_TARGET=x86_64-pc-windows-msvc"
)
cargo build --release --target %RUST_TARGET% --workspace
echo BUILD_EXIT=%ERRORLEVEL%
