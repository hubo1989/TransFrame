@echo off
rem GUI regression driver; runs against the actual running TransFrame.App.
setlocal
cd /d "%~dp0..\.."
call "D:\vs-buildtools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /utf-8 /W4 /MD Tests\manual\smoke_selection_window.cpp /Fe:build\smoke_selection_window.exe /Fo:build\smoke_selection_window.obj /link user32.lib gdi32.lib dwmapi.lib wtsapi32.lib windowscodecs.lib ole32.lib
exit /b %errorlevel%
