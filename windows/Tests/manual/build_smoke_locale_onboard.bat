@echo off
rem build_smoke_locale_onboard.bat - compile the FR-37/39/50.7 locale +
rem onboarding + notification manual smoke tool against the sln-built static
rem libs (see smoke_locale_onboard.cpp header for the checks).
rem Single-line cl invocation: this file carries LF endings and cmd line
rem continuations need CRLF, so avoid multi-line caret continuations.
setlocal
cd /d "%~dp0..\.."
call "D:\vs-buildtools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo vcvars64 failed & exit /b 1 )

cl /nologo /std:c++20 /EHsc /utf-8 /W4 /MD /I NativeShell\include /I Core /I Core\include /I Core\vendor Tests\manual\smoke_locale_onboard.cpp /Fe:build\smoke_locale_onboard.exe /Fo:build\smoke_locale_onboard.obj /link build\x64\Release\TransFrame.NativeShell\TransFrame.NativeShell.lib build\x64\Release\TransFrame.Core\TransFrame.Core.lib shell32.lib user32.lib advapi32.lib ole32.lib
endlocal
