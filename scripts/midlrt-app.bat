@echo off
:: 手动编译 App.idl -> winmd + 投影（绕开 VS2026 MSBuild 的 MIDL 参数变更）
set "MIDLRT=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\midlrt.exe"
set "MSVC=D:\AI\VisualStudio\VC\Tools\MSVC\14.50.35717"
set "WINKIT=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set "WUI=packages\Microsoft.WindowsAppSDK.WinUI.1.8.260803003"
set "FND=packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002"
set "IEE=packages\Microsoft.WindowsAppSDK.InteractiveExperiences.1.8.260708001"
set "PATH=%MSVC%\bin\Hostx64\x64;%WINKIT%\bin\%SDKVER%\x64;%PATH%"

"%MIDLRT%" /nomidl ^
  /metadata_dir "%WINKIT%\UnionMetadata\%SDKVER%" ^
  /reference "%WUI%\metadata\Microsoft.UI.Xaml.winmd" ^
  /reference "%FND%\metadata\Microsoft.Windows.Foundation.winmd" ^
  /reference "%IEE%\metadata\10.0.18362.0\Microsoft.UI.winmd" ^
  /I "%WINKIT%\Include\%SDKVER%\um" ^
  /I "%WINKIT%\Include\%SDKVER%\shared" ^
  /I "%WUI%\include" ^
  /I "app\Generated" ^
  /winmd "app\CompMidl\CampusAuthGuardian.winmd" ^
  /h "app\CompMidl\CampusAuthGuardian.h" ^
  app\App.idl
if errorlevel 1 goto fail
echo MIDLRT-OK
exit /b 0
:fail
echo MIDLRT-FAILED
exit /b 1
