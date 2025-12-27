@echo off
setlocal

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=RelWithDebInfo

if "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    set ARCH=x64
) else if "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set ARCH=arm64
) else (
    set ARCH=x86
)

call "%~dp0build.cmd" %CONFIG%
echo Running JucyAudio...
"build-%ARCH%\%CONFIG%\JucyAudio.exe"
