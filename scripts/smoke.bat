@echo off
:: smoke test: kill old, copy, run 15s, report stage
powershell -NoProfile -Command "Get-Process GuardianApp,App2,App3,CampusAuthGuardian -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue" >nul 2>&1
timeout /t 2 /nobreak >nul
copy /y "C:\Users\NekoMirra\campus-auth-guardian\dist\x64\Release\CampusAuthGuardian.exe" "C:\Users\NekoMirra\campus-auth-guardian\dist\smoke\GuardianApp.exe" >nul
del "C:\Users\NekoMirra\campus-auth-guardian\dist\smoke\stage.txt" 2>nul
powershell -NoProfile -Command "$p = Start-Process 'C:\Users\NekoMirra\campus-auth-guardian\dist\smoke\GuardianApp.exe' -WorkingDirectory 'C:\Users\NekoMirra\campus-auth-guardian\dist\smoke' -PassThru; Start-Sleep 15; Write-Host ALIVE=$(-not $p.HasExited)"
type "C:\Users\NekoMirra\campus-auth-guardian\dist\smoke\stage.txt"
