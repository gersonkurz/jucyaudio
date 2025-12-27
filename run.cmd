@echo off
setlocal

:: Usage: run.cmd [arch] [config]
set ARCH=%1
set CONFIG=%2

if "%CONFIG%"=="" set CONFIG=Release

:: Detect arch if not specified
if "%ARCH%"=="" (
    if "%PROCESSOR_ARCHITECTURE%"=="AMD64" set ARCH=x64
    if "%PROCESSOR_ARCHITECTURE%"=="ARM64" set ARCH=arm64
    if "%ARCH%"=="" set ARCH=x86
)

call "%~dp0build.cmd" %ARCH% %CONFIG%
echo.
echo Running JucyAudio...
"build-%ARCH%\jucyaudio_artefacts\%CONFIG%\JucyAudio.exe"
