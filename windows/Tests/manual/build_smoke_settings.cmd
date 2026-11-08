@echo off
rem Build the WinUI settings smoke harness. Run from anywhere; locates the
rem repo root relative to this script (windows/Tests/manual -> root).
pushd "%~dp0..\..\.."
call "D:\vs-buildtools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /std:c++20 /EHsc /MD /utf-8 /nologo windows\Tests\manual\smoke_settings_winui.cpp ^
  /I windows\UI\SettingsShell ^
  /I "%USERPROFILE%\.nuget\packages\microsoft.windowsappsdk\1.6.250205002\include" ^
  /Fe:smoke_settings_winui.exe ^
  /link windows\build\x64\Release\WinUI3Shell\WinUI3Shell.lib ^
  "%USERPROFILE%\.nuget\packages\microsoft.windowsappsdk\1.6.250205002\lib\win10-x64\Microsoft.WindowsAppRuntime.Bootstrap.lib" ^
  windowsapp.lib user32.lib
popd
