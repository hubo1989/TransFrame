@echo off
rem Build the FR-35 six-tab settings smoke harness. Run from anywhere; locates
rem the repo root relative to this script (windows/Tests/manual -> root).
rem Links /MD against WinUI3Shell.lib (+ NativeShell/Core static libs) and the
rem WASDK bootstrap import lib; copies the app-local bootstrap DLL next to exe.
pushd "%~dp0..\..\.."
call "D:\vs-buildtools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /std:c++20 /EHsc /MD /utf-8 /nologo windows\Tests\manual\smoke_settings_tabs.cpp ^
  /I windows\UI\SettingsShell ^
  /I windows\NativeShell\include ^
  /I windows\Core\include ^
  /Fe:smoke_settings_tabs.exe ^
  /link windows\build\x64\Release\WinUI3Shell\WinUI3Shell.lib ^
  windows\build\x64\Release\TransFrame.NativeShell\TransFrame.NativeShell.lib ^
  windows\build\x64\Release\TransFrame.Core\TransFrame.Core.lib ^
  "%USERPROFILE%\.nuget\packages\microsoft.windowsappsdk\1.6.250205002\lib\win10-x64\Microsoft.WindowsAppRuntime.Bootstrap.lib" ^
  windowsapp.lib user32.lib
if errorlevel 1 (
  popd
  exit /b 1
)
copy /y "%USERPROFILE%\.nuget\packages\microsoft.windowsappsdk\1.6.250205002\runtimes\win-x64\native\Microsoft.WindowsAppRuntime.Bootstrap.dll" . >nul
popd
