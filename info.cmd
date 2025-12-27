@echo off
setlocal

:: Get version from CMakeLists.txt
for /f "tokens=2 delims=()" %%a in ('findstr /c:"project(jucyaudio VERSION" CMakeLists.txt') do (
    for /f "tokens=2" %%v in ("%%a") do set VERSION=%%v
)

if "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    set ARCH=x64
) else if "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set ARCH=arm64
) else (
    set ARCH=x86
)

echo JucyAudio Build Information
echo ============================
echo OS: windows
echo Architecture: %ARCH%
echo Version: %VERSION%
echo CPU cores: %NUMBER_OF_PROCESSORS%
echo Default build type: RelWithDebInfo
