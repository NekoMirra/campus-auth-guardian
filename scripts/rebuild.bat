@echo off
:: 强制完整重编：杀进程 -> 清 bin/obj -> build
taskkill /F /IM CampusAuthGuardian.exe >nul 2>&1
timeout /t 2 /nobreak >nul
if exist "csharp\bin" rmdir /s /q csharp\bin
if exist "csharp\obj" rmdir /s /q csharp\obj
powershell -NoProfile -Command "& 'D:\AI\VisualStudio\MSBuild\Current\Bin\MSBuild.exe' 'C:\Users\NekoMirra\campus-auth-guardian\csharp\CampusAuthGuardian.csproj' /p:Configuration=Release /p:Platform=x64 /restore /v:q /nologo 2>&1 | Select-String 'error' | Select-Object -First 5 | Out-String"
if exist "csharp\bin\x64\Release\net10.0-windows10.0.26100.0\CampusAuthGuardian.exe" (
  echo BUILD-OK
) else (
  echo BUILD-FAILED
)
