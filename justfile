# Build automation for JucyAudio project
# Cross-platform: Windows and macOS
# Requires: just (https://github.com/casey/just)

# Set shell for Windows (PowerShell)
set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]

# ============================================================================
# Platform Detection
# ============================================================================

# Detect OS: "windows", "macos", or "linux"
os := os()

# Detect native architecture
native_arch := if os == "windows" {
    env_var_or_default("PROCESSOR_ARCHITECTURE", "AMD64")
} else {
    `uname -m`
}

# Normalize architecture names
arch := if native_arch == "AMD64" { "x64" } else if native_arch == "x86_64" { "x64" } else if native_arch == "arm64" { "arm64" } else if native_arch == "aarch64" { "arm64" } else { native_arch }

# Default build type
default_build_type := "Release"

# Get version from CMakeLists.txt (uses sh from Git)
version := `grep "project(jucyaudio VERSION" CMakeLists.txt | grep -o "[0-9]*\.[0-9]*\.[0-9]*"`

# CPU count for parallel builds
cpu_count := if os == "windows" {
    env_var_or_default("NUMBER_OF_PROCESSORS", "8")
} else {
    `sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8`
}

# ============================================================================
# Common Build Commands
# ============================================================================

# Default: show help
default:
    @just --list

# Configure using CMake presets (useful for IDE integration)
[windows]
configure preset="x64-release":
    cmake --preset {{preset}}

[unix]
configure:
    cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build with specified configuration
build config=default_build_type:
    @just _build-{{os}} {{config}} {{arch}}

# Build debug configuration
debug:
    @just build Debug

# Build release configuration
release:
    @just build Release

# Build in offline mode (airplane-safe - requires prior online build)
build-offline config=default_build_type:
    @just _build-offline-{{os}} {{config}} {{arch}}

# Clean and rebuild
rebuild config=default_build_type:
    @just clean
    @just build {{config}}

# Clean build directories
[windows]
clean:
    if (Test-Path build) { Remove-Item -Recurse -Force build }
    if (Test-Path build-x64) { Remove-Item -Recurse -Force build-x64 }
    if (Test-Path build-x86) { Remove-Item -Recurse -Force build-x86 }
    if (Test-Path build-arm64) { Remove-Item -Recurse -Force build-arm64 }
    if (Test-Path releases) { Remove-Item -Recurse -Force releases }

[unix]
clean:
    rm -rf build build-* install-* releases

# ============================================================================
# Windows-Specific Build Commands
# ============================================================================

[windows]
_build-windows config arch_target:
    cmake -B build-{{arch_target}} -A {{ if arch_target == "x64" { "x64" } else if arch_target == "x86" { "Win32" } else { "ARM64" } }} -DCMAKE_BUILD_TYPE={{config}}
    cmake --build build-{{arch_target}} --config {{config}} --parallel {{cpu_count}}
    Write-Host "Build complete: build-{{arch_target}}/jucyaudio_artefacts/{{config}}/JucyAudio.exe"

[windows]
_build-offline-windows config arch_target:
    cmake -B build-{{arch_target}} -A {{ if arch_target == "x64" { "x64" } else if arch_target == "x86" { "Win32" } else { "ARM64" } }} -DCMAKE_BUILD_TYPE={{config}} -DJUCYAUDIO_OFFLINE_BUILD=ON
    cmake --build build-{{arch_target}} --config {{config}} --parallel {{cpu_count}}
    Write-Host "Offline build complete: build-{{arch_target}}/jucyaudio_artefacts/{{config}}/JucyAudio.exe"

[windows]
build-x64 config=default_build_type:
    @just _build-windows {{config}} x64

[windows]
build-x86 config=default_build_type:
    @just _build-windows {{config}} x86

[windows]
build-arm64 config=default_build_type:
    @just _build-windows {{config}} arm64

[windows]
build-all config="Release":
    Write-Host "Building all Windows architectures..."
    just build-x64 {{config}}
    just build-x86 {{config}}
    just build-arm64 {{config}}
    Write-Host "All builds complete!"

# Run the application (Windows)
[windows]
run config=default_build_type:
    just build {{config}}
    & "build-{{arch}}/jucyaudio_artefacts/{{config}}/JucyAudio.exe"

# ============================================================================
# macOS-Specific Build Commands
# ============================================================================

[macos]
_build-macos config arch_target:
    cmake -B build-{{arch_target}} -DCMAKE_BUILD_TYPE={{config}} -DCMAKE_OSX_ARCHITECTURES={{arch_target}}
    cmake --build build-{{arch_target}} -j{{cpu_count}}
    @echo "Build complete: build-{{arch_target}}/{{arch_target}}-{{config}}/"

[macos]
_build-offline-macos config arch_target:
    cmake -B build-{{arch_target}} -DCMAKE_BUILD_TYPE={{config}} -DCMAKE_OSX_ARCHITECTURES={{arch_target}} -DJUCYAUDIO_OFFLINE_BUILD=ON
    cmake --build build-{{arch_target}} -j{{cpu_count}}
    @echo "Offline build complete: build-{{arch_target}}/{{arch_target}}-{{config}}/"

[macos]
build-arm64 config=default_build_type:
    @just _build-macos {{config}} arm64

[macos]
build-x64 config=default_build_type:
    @just _build-macos {{config}} x86_64

[macos]
build-universal config=default_build_type:
    cmake -B build-universal -DCMAKE_BUILD_TYPE={{config}} -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
    cmake --build build-universal -j{{cpu_count}}
    @echo "Universal build complete!"

[macos]
build-all config="Release":
    @echo "Building all macOS architectures..."
    @just build-arm64 {{config}}
    @just build-x64 {{config}}
    @echo "All builds complete!"

# Run the application (macOS)
[macos]
run config=default_build_type:
    @just build {{config}}
    ./build-{{arch}}/{{arch}}-{{config}}/jucyaudio.app/Contents/MacOS/jucyaudio

# ============================================================================
# Package Commands - Windows
# ============================================================================

[windows]
_package-windows arch_target config="Release":
    just _build-windows {{config}} {{arch_target}}
    if (-not (Test-Path releases)) { New-Item -ItemType Directory -Path releases | Out-Null }
    if (-not (Test-Path "build-{{arch_target}}/jucyaudio_artefacts/{{config}}/themes")) { New-Item -ItemType Directory -Path "build-{{arch_target}}/jucyaudio_artefacts/{{config}}/themes" -Force | Out-Null }
    Copy-Item -Path "themes/*" -Destination "build-{{arch_target}}/jucyaudio_artefacts/{{config}}/themes" -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "Creating installer for {{arch_target}}..."
    Push-Location setup; makensis "setup-{{arch_target}}.nsi"; Pop-Location
    Move-Item -Force "setup/jucyaudio-{{version}}-setup-{{arch_target}}.exe" "releases/"
    Write-Host "Installer created: releases/jucyaudio-{{version}}-setup-{{arch_target}}.exe"

[windows]
package-x64:
    @just _package-windows x64 Release

[windows]
package-x86:
    @just _package-windows x86 Release

[windows]
package-arm64:
    @just _package-windows arm64 Release

# ============================================================================
# Package Commands - macOS
# ============================================================================

[macos]
_package-macos arch_target config="Release":
    @just _build-macos {{config}} {{arch_target}}
    @just _bundle-licenses-macos {{arch_target}} {{config}}
    cd build-{{arch_target}} && cpack -G DragNDrop
    @mkdir -p releases
    @mv build-{{arch_target}}/*.dmg releases/ 2>/dev/null || true
    @echo "Package created in releases/"

[macos]
_package-offline-macos arch_target config="Release":
    @just _build-offline-macos {{config}} {{arch_target}}
    @just _bundle-licenses-macos {{arch_target}} {{config}}
    cd build-{{arch_target}} && cpack -G DragNDrop
    @mkdir -p releases
    @mv build-{{arch_target}}/*.dmg releases/ 2>/dev/null || true
    @echo "Package created in releases/ (offline mode)"

[macos]
_bundle-licenses-macos arch_target config:
    #!/usr/bin/env bash
    app_path="build-{{arch_target}}/{{arch_target}}-{{config}}/jucyaudio.app/Contents/Resources"
    if [ -d "$app_path" ]; then
        mkdir -p "$app_path/licenses"
        cp -f LICENSE THIRD_PARTY_NOTICES.txt "$app_path/licenses/"
        cp -R licenses/* "$app_path/licenses/" 2>/dev/null || true
        echo "Licenses bundled into app"
    fi

[macos]
package-arm64:
    @just _package-macos arm64 Release

[macos]
package-x64:
    @just _package-macos x86_64 Release

[macos]
package-arm64-offline:
    @just _package-offline-macos arm64 Release

[macos]
package-x64-offline:
    @just _package-offline-macos x86_64 Release

# ============================================================================
# Publish Commands
# ============================================================================

# Generate version.nsi for NSIS installer
[windows]
_generate-version-nsi:
    $v = "{{version}}".Split('.'); \
    $content = @" \
    ; This file is auto-generated by justfile. Do not edit manually. \
    ; Version information for NSIS installer scripts \
    \
    !define CURRENT_VERSION "{{version}}" \
    !define VERSION_MAJOR $($v[0]) \
    !define VERSION_MINOR $($v[1]) \
    !define VERSION_PATCH $($v[2]) \
    \
    ; Build information \
    !define BUILD_TYPE "Release" \
    !define COMPILER_ID "MSVC" \
    !define SYSTEM_NAME "Windows" \
    \
    ; Product information \
    !define PRODUCT_NAME "JucyAudio" \
    !define PRODUCT_PUBLISHER "P-nand-Q" \
    !define PRODUCT_WEB_SITE "https://github.com/p-nand-q/jucyaudio" \
    !define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\`${PRODUCT_NAME}" \
    !define PRODUCT_UNINST_ROOT_KEY "HKLM" \
    "@; \
    $content | Out-File -FilePath "setup\version.nsi" -Encoding ASCII

# Publish all releases for current platform
[windows]
publish:
    Write-Host "========================================"
    Write-Host "JucyAudio Release Build - Windows"
    Write-Host "Version: {{version}}"
    Write-Host "========================================"
    Write-Host ""
    if (Test-Path releases) { Remove-Item -Recurse -Force releases }
    New-Item -ItemType Directory -Path releases | Out-Null
    just _generate-version-nsi
    Write-Host ""
    Write-Host "[1/3] Building x64..."
    just package-x64
    Write-Host ""
    Write-Host "[2/3] Building x86..."
    just package-x86
    Write-Host ""
    Write-Host "[3/3] Building ARM64..."
    just package-arm64
    Write-Host ""
    Write-Host "========================================"
    Write-Host "Generating checksums..."
    Push-Location releases; \
    Get-ChildItem *.exe | ForEach-Object { \
        $hash = (Get-FileHash $_.Name -Algorithm SHA256).Hash.ToLower(); \
        "$hash  $($_.Name)" \
    } | Out-File -FilePath checksums.txt -Encoding ASCII; \
    Pop-Location
    Write-Host ""
    Write-Host "Release artifacts in releases\:"
    Get-ChildItem releases\*.exe | ForEach-Object { Write-Host $_.Name }
    Write-Host ""
    Write-Host "Checksums:"
    Get-Content releases\checksums.txt
    Write-Host ""
    Write-Host "========================================"
    Write-Host "JucyAudio Windows Release Complete!"
    Write-Host "========================================"

[macos]
publish:
    #!/usr/bin/env bash
    echo "========================================"
    echo "JucyAudio Release Build - macOS"
    echo "Version: {{version}}"
    echo "========================================"
    echo ""
    rm -rf releases
    mkdir -p releases
    echo ""
    echo "[1/2] Building ARM64 (Apple Silicon)..."
    just package-arm64
    echo ""
    echo "[2/2] Building x64 (Intel)..."
    just package-x64
    echo ""
    echo "========================================"
    echo "Generating checksums..."
    cd releases && shasum -a 256 *.dmg > checksums.txt
    echo ""
    echo "Release artifacts in releases/:"
    ls -lh releases/*.dmg
    echo ""
    echo "Checksums:"
    cat releases/checksums.txt
    echo ""
    echo "========================================"
    echo "JucyAudio macOS Release Complete!"
    echo "========================================"

# Publish all releases for current platform (offline/airplane mode)
[macos]
publish-offline:
    #!/usr/bin/env bash
    echo "========================================"
    echo "JucyAudio Release Build - macOS (OFFLINE)"
    echo "Version: {{version}}"
    echo "========================================"
    echo ""
    rm -rf releases
    mkdir -p releases
    echo ""
    echo "[1/2] Building ARM64 (Apple Silicon)..."
    just package-arm64-offline
    echo ""
    echo "[2/2] Building x64 (Intel)..."
    just package-x64-offline
    echo ""
    echo "========================================"
    echo "Generating checksums..."
    cd releases && shasum -a 256 *.dmg > checksums.txt
    echo ""
    echo "Release artifacts in releases/:"
    ls -lh releases/*.dmg
    echo ""
    echo "Checksums:"
    cat releases/checksums.txt
    echo ""
    echo "========================================"
    echo "JucyAudio macOS Release Complete! (offline)"
    echo "========================================"

# ============================================================================
# Information Commands
# ============================================================================

# Show build information
[windows]
info:
    @Write-Host "JucyAudio Build Information`n============================`nOS: {{os}}`nArchitecture: {{arch}}`nVersion: {{version}}`nCPU cores: {{cpu_count}}`nDefault build type: {{default_build_type}}"

[unix]
info:
    @echo "JucyAudio Build Information"
    @echo "============================"
    @echo "OS: {{os}}"
    @echo "Architecture: {{arch}}"
    @echo "Version: {{version}}"
    @echo "CPU cores: {{cpu_count}}"
    @echo "Default build type: {{default_build_type}}"

# List available recipes
help:
    @just --list
