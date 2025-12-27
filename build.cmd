@echo off
setlocal

:: Default config is RelWithDebInfo
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=RelWithDebInfo

:: Detect architecture
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

echo Building JucyAudio (%CONFIG%) for %ARCH%...
cmake -B build-%ARCH% -A %CMAKE_ARCH% -DCMAKE_BUILD_TYPE=%CONFIG%
cmake --build build-%ARCH% --config %CONFIG% --parallel %NUMBER_OF_PROCESSORS%
echo Build complete: build-%ARCH%\%CONFIG%\
