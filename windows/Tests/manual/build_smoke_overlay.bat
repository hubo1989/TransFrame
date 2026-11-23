@echo off
rem build_smoke_overlay.bat - compile the FR-26 overlay render smoke tool
rem against the sln-built static libs (see smoke_overlay.cpp header).
rem Single-line cl invocation: this file carries LF endings and cmd line
rem continuations need CRLF, so avoid multi-line caret continuations.
setlocal
cd /d "%~dp0..\.."
call "D:\vs-buildtools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo vcvars64 failed & exit /b 1 )

cl /nologo /std:c++20 /EHsc /utf-8 /W4 /MD /I NativeShell\include /I Core /I Core\include /I Core\vendor Tests\manual\smoke_overlay.cpp /Fe:build\smoke_overlay.exe /Fo:build\smoke_overlay.obj /link build\x64\Release\TransFrame.NativeShell\TransFrame.NativeShell.lib build\x64\Release\TransFrame.Core\TransFrame.Core.lib d3d11.lib dxgi.lib dwmapi.lib shcore.lib d2d1.lib dwrite.lib dcomp.lib windowscodecs.lib windowsapp.lib winhttp.lib advapi32.lib user32.lib gdi32.lib ole32.lib
endlocal
