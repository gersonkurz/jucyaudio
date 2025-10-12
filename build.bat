@echo off
REM Build automation for JucyAudio project (Windows)
REM Usage: build.bat [command] [config]
REM Commands: configure, build, rebuild, clean, install
REM Config: Debug, Release, RelWithDebInfo (default)

setlocal enabledelayedexpansion

set DEFAULT_CONFIG=x64-RelWithDebInfo

if "%1"=="" (
    call :build %DEFAULT_CONFIG%
    exit /b
)

if "%1"=="configure" (
    set CONFIG=%2
    if "!CONFIG!"=="" set CONFIG=%DEFAULT_CONFIG%
    call :configure !CONFIG!
    exit /b
)

if "%1"=="build" (
    set CONFIG=%2
    if "!CONFIG!"=="" set CONFIG=%DEFAULT_CONFIG%
    call :build !CONFIG!
    exit /b
)

if "%1"=="rebuild" (
    set CONFIG=%2
    if "!CONFIG!"=="" set CONFIG=%DEFAULT_CONFIG%
    call :rebuild !CONFIG!
    exit /b
)

if "%1"=="clean" (
    call :clean
    exit /b
)

if "%1"=="install" (
    set CONFIG=%2
    if "!CONFIG!"=="" set CONFIG=x64-Release
    call :install !CONFIG!
    exit /b
)

if "%1"=="debug" (
    call :build x64-Debug
    exit /b
)

if "%1"=="release" (
    call :build x64-Release
    exit /b
)

if "%1"=="relwithdebinfo" (
    call :build x64-RelWithDebInfo
    exit /b
)

if "%1"=="help" (
    call :help
    exit /b
)

echo Unknown command: %1
call :help
exit /b 1

:configure
echo.
echo Configuring JucyAudio [%1]...
cmake --preset %1
if errorlevel 1 (
    echo Error: Configuration failed
    exit /b 1
)
echo Configuration complete.
exit /b 0

:build
echo.
echo Building JucyAudio [%1]...
cmake --preset %1
if errorlevel 1 (
    echo Error: Configuration failed
    exit /b 1
)
cmake --build --preset %1 --parallel
if errorlevel 1 (
    echo Error: Build failed
    exit /b 1
)
echo.
echo Build complete: out\build\%1\
exit /b 0

:rebuild
echo.
echo Rebuilding JucyAudio [%1]...
call :clean
call :build %1
exit /b

:clean
echo.
echo Cleaning build directories...
if exist out rmdir /s /q out
if exist build rmdir /s /q build
if exist install rmdir /s /q install
echo Clean complete.
exit /b 0

:install
echo.
echo Installing JucyAudio [%1]...
cmake --install out\build\%1 --config Release
if errorlevel 1 (
    echo Error: Install failed
    exit /b 1
)
echo Install complete: out\install\%1\bin\
exit /b 0

:help
echo.
echo JucyAudio Build Script (Windows)
echo ================================
echo.
echo Usage: build.bat [command] [config]
echo.
echo Commands:
echo   (none)          Build with default configuration (RelWithDebInfo)
echo   configure       Configure CMake only
echo   build           Build project (configure + build)
echo   rebuild         Clean + build
echo   clean           Remove all build directories
echo   install         Install to out\install directory
echo   debug           Build Debug configuration
echo   release         Build Release configuration
echo   relwithdebinfo  Build RelWithDebInfo configuration
echo   help            Show this help
echo.
echo Config: x64-Debug, x64-Release, x64-RelWithDebInfo
echo.
echo Examples:
echo   build.bat                          - Build with RelWithDebInfo
echo   build.bat build x64-Release        - Build Release
echo   build.bat rebuild x64-Debug        - Clean rebuild Debug
echo   build.bat clean                    - Clean all
echo   build.bat install x64-Release      - Install Release build
echo.
exit /b 0
