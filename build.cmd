@echo off
setlocal

:: Usage: build.cmd [arch] [config]
::   arch:   x64, x86, arm64 (default: native)
::   config: Release, Debug, RelWithDebInfo (default: Release)

:: Parse arguments
set ARCH=%1
set CONFIG=%2

:: Default config is Release
if "%CONFIG%"=="" set CONFIG=Release

:: Handle architecture
if "%ARCH%"=="" goto :detect_arch
if /i "%ARCH%"=="x64" goto :set_x64
if /i "%ARCH%"=="x86" goto :set_x86
if /i "%ARCH%"=="arm64" goto :set_arm64
:: If first arg isn't an arch, treat it as config and detect arch
set CONFIG=%ARCH%
set ARCH=
goto :detect_arch

:set_x64
set ARCH=x64
set CMAKE_ARCH=x64
goto :build

:set_x86
set ARCH=x86
set CMAKE_ARCH=Win32
goto :build

:set_arm64
set ARCH=arm64
set CMAKE_ARCH=ARM64
goto :build

:detect_arch
if "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    set ARCH=x64
    set CMAKE_ARCH=x64
) else if "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set ARCH=arm64
    set CMAKE_ARCH=ARM64
) else (
    set ARCH=x86
    set CMAKE_ARCH=Win32
)

:build
echo Building JucyAudio [%ARCH%] [%CONFIG%]...
cmake -B build-%ARCH% -A %CMAKE_ARCH%
cmake --build build-%ARCH% --config %CONFIG% --parallel %NUMBER_OF_PROCESSORS%
echo.
echo Build complete: build-%ARCH%\jucyaudio_artefacts\%CONFIG%\JucyAudio.exe
