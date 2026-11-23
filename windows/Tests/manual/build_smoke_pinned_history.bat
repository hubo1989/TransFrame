@echo off
rem build_smoke_pinned_history.bat - compile the pinned-windows + history
rem smoke tool (FR-14/FR-30) against the sln-built static libs. Build the
rem solution Release x64 first. Single-line cl invocation: this file carries
rem LF endings and cmd line continuations need CRLF, so avoid multi-line
rem caret continuations.
setlocal
cd /d "%~dp0..\.."
call "D:\vs-buildtools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo vcvars64 failed & exit /b 1 )

cl /nologo /std:c++20 /EHsc /utf-8 /W4 /MD /I NativeShell\include /I Core /I Core\include /I Core\vendor Tests\manual\smoke_pinned_history.cpp /Fe:build\smoke_pinned_history.exe /Fo:build\smoke_pinned_history.obj /link build\x64\Release\TransFrame.NativeShell\TransFrame.NativeShell.lib build\x64\Release\TransFrame.Core\TransFrame.Core.lib d3d11.lib dxgi.lib dwmapi.lib shcore.lib d2d1.lib dwrite.lib dcomp.lib windowscodecs.lib windowsapp.lib winhttp.lib advapi32.lib user32.lib gdi32.lib ole32.lib
endlocal
