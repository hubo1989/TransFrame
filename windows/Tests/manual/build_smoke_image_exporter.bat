@echo off
rem Compile only the exporter regression and its sources, independently of
rem solution outputs so it can run while another agent builds NativeShell.
setlocal
cd /d "%~dp0..\.."
call "D:\vs-buildtools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1
if not exist build mkdir build
if not exist build\smoke_image_exporter mkdir build\smoke_image_exporter

cl /nologo /std:c++20 /EHsc /utf-8 /Zc:__cplusplus /W4 /MD /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I NativeShell\include /I Core /I Core\include /I Core\vendor Tests\manual\smoke_image_exporter.cpp NativeShell\src\ImageExporter.cpp NativeShell\src\ThumbnailCodec.cpp NativeShell\src\OverlayRenderer.cpp Core\AnnotationDocument.cpp /Fe:build\smoke_image_exporter.exe /Fo:build\smoke_image_exporter\ /link d3d11.lib dxgi.lib dwmapi.lib shcore.lib d2d1.lib dwrite.lib dcomp.lib windowscodecs.lib windowsapp.lib user32.lib gdi32.lib ole32.lib uuid.lib bcrypt.lib
exit /b %errorlevel%
