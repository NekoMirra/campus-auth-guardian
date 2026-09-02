@echo off
:: 快速检查（不产出二进制）
if "%1"=="arm64" (
  call "%~dp0env-arm64.bat"
) else (
  call "%~dp0env-x64.bat"
)
if "%2"=="test" (
  cargo test --workspace
) else (
  cargo check --workspace
)
echo EXIT=%ERRORLEVEL%
