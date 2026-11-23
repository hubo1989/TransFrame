@echo off
rem build_smoke_uploads_picker.bat - compile the FR-31/32/41 smoke tool
rem (image-host upload + color picker + color history) against the sln-built
rem static libs (see smoke_uploads_picker.cpp header for context).
rem Single-line cl invocation: this file carries LF endings and cmd line
rem continuations need CRLF, so avoid multi-line caret continuations.
setlocal
cd /d "%~dp0..\.."
call "D:\vs-buildtools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo vcvars64 failed & exit /b 1 )

cl /nologo /std:c++20 /EHsc /utf-8 /W4 /MD /I NativeShell\include /I Core /I Core\include /I Core\vendor Tests\manual\smoke_uploads_picker.cpp /Fe:build\smoke_uploads_picker.exe /Fo:build\smoke_uploads_picker.obj /link build\x64\Release\TransFrame.NativeShell\TransFrame.NativeShell.lib build\x64\Release\TransFrame.Core\TransFrame.Core.lib d3d11.lib dxgi.lib dwmapi.lib shcore.lib d2d1.lib dwrite.lib dcomp.lib windowscodecs.lib windowsapp.lib winhttp.lib advapi32.lib user32.lib gdi32.lib ole32.lib
endlocal
