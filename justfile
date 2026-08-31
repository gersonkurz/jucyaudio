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

# Clean build directories (all build-*/install-* trees, incl. the CMakePresets dirs)
[windows]
clean:
    Get-ChildItem -Path . -Directory -Filter 'build*' -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
    Get-ChildItem -Path . -Directory -Filter 'install*' -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
    if (Test-Path releases) { Remove-Item -Recurse -Force releases }

[unix]
clean:
    rm -rf build build-* install-* releases

# ============================================================================
# Windows-Specific Build Commands
# ============================================================================

# Build via the CMakePresets (build-<arch>-<config>), so just, Visual Studio, and
# `just package-x64` all share one configured tree. Presets pin VS 2026 and exist for
# x64 and x86 (debug/release); there is no Windows-arm64 preset (2.0 ships x64 only).
[windows]
_build-windows config arch_target:
    cmake --preset {{arch_target}}-{{lowercase(config)}}
    cmake --build build-{{arch_target}}-{{lowercase(config)}} --config {{config}} --parallel {{cpu_count}}
    Write-Host "Build complete: build-{{arch_target}}-{{lowercase(config)}}/jucyaudio_artefacts/{{config}}/JucyAudio.exe"

[windows]
_build-offline-windows config arch_target:
    cmake --preset {{arch_target}}-{{lowercase(config)}} -DJUCYAUDIO_OFFLINE_BUILD=ON
    cmake --build build-{{arch_target}}-{{lowercase(config)}} --config {{config}} --parallel {{cpu_count}}
    Write-Host "Offline build complete: build-{{arch_target}}-{{lowercase(config)}}/jucyaudio_artefacts/{{config}}/JucyAudio.exe"

[windows]
build-x64 config=default_build_type:
    @just _build-windows {{config}} x64

[windows]
build-x86 config=default_build_type:
    @just _build-windows {{config}} x86

[windows]
build-all config="Release":
    Write-Host "Building all Windows architectures..."
    just build-x64 {{config}}
    just build-x86 {{config}}
    Write-Host "All builds complete!"

# Run the application (Windows)
[windows]
run config=default_build_type:
    just build {{config}}
    & "build-{{arch}}-{{lowercase(config)}}/jucyaudio_artefacts/{{config}}/JucyAudio.exe"

# Headless scan self test (Windows). Runs against a throwaway config root under the build
# directory, never the real library. Exit code is the result; details land in selftest-results.txt.
[windows]
selftest config=default_build_type:
    just build {{config}}
    if (Test-Path "build-{{arch}}-{{lowercase(config)}}/selftest-config") { Remove-Item -Recurse -Force "build-{{arch}}-{{lowercase(config)}}/selftest-config" }
    New-Item -ItemType Directory -Force -Path "build-{{arch}}-{{lowercase(config)}}/selftest-config" | Out-Null
    if (Test-Path "build-{{arch}}-{{lowercase(config)}}/selftest-root") { Remove-Item -Recurse -Force "build-{{arch}}-{{lowercase(config)}}/selftest-root" }
    New-Item -ItemType Directory -Force -Path "build-{{arch}}-{{lowercase(config)}}/selftest-root" | Out-Null
    $env:JUCYAUDIO_CONFIG = (Resolve-Path "build-{{arch}}-{{lowercase(config)}}/selftest-config").Path; $env:JUCYAUDIO_SELFTEST_ROOT = (Resolve-Path "build-{{arch}}-{{lowercase(config)}}/selftest-root").Path; $p = Start-Process -FilePath "build-{{arch}}-{{lowercase(config)}}/jucyaudio_artefacts/{{config}}/JucyAudio.exe" -ArgumentList "--selftest-scan" -Wait -PassThru; Get-Content "build-{{arch}}-{{lowercase(config)}}/selftest-root/selftest-results.txt","build-{{arch}}-{{lowercase(config)}}/selftest-root/mixrecovery-results.txt","build-{{arch}}-{{lowercase(config)}}/selftest-root/backup-results.txt"; if ($p.ExitCode -ne 0) { Write-Host "SELF TEST FAILED"; exit $p.ExitCode }; Write-Host "Self test passed."

# Record recovery data for every mix that has none, and write each a playlist (Windows).
# One-off: recomputes every mix's stored total_length. Mixes that are already correct are left
# untouched, so a second run reports nothing to do and is how you verify the first.
# Runs against the real library, and takes a confirmed backup first like backup-mixes.
[windows]
repair-mix-durations config=default_build_type:
    just build {{config}}
    $p = Start-Process -FilePath "build-{{arch}}-{{lowercase(config)}}/jucyaudio_artefacts/{{config}}/JucyAudio.exe" -ArgumentList "--repair-mix-durations" -Wait -PassThru; $root = if ($env:JUCYAUDIO_CONFIG) { $env:JUCYAUDIO_CONFIG } else { Join-Path $env:LOCALAPPDATA "jucyaudio" }; Get-Content (Join-Path $root "mix-duration-repair.txt"); if ($p.ExitCode -ne 0) { Write-Host "REPAIR FAILED"; exit $p.ExitCode }; Write-Host "Repair complete."

# One-off: records what survives of the mixes that lost rows before recovery data existed, marked
# as partial. Those mixes are refused by backup-mixes, by design - so they are the only mixes with
# no record at all, and their missing tracks are in no surviving backup. A partial record never
# replaces a whole one.
[windows]
backup-damaged-mixes config=default_build_type:
    just build {{config}}
    $p = Start-Process -FilePath "build-{{arch}}-{{lowercase(config)}}/jucyaudio_artefacts/{{config}}/JucyAudio.exe" -ArgumentList "--export-mix-recovery-partial" -Wait -PassThru; $root = if ($env:JUCYAUDIO_CONFIG) { $env:JUCYAUDIO_CONFIG } else { Join-Path $env:LOCALAPPDATA "jucyaudio" }; Get-Content (Join-Path $root "backfill-results.txt"); if ($p.ExitCode -ne 0) { Write-Host "BACKFILL FAILED"; exit $p.ExitCode }; Write-Host "Backfill complete."

# Runs against the real library. Takes a confirmed backup first and refuses to start without one.
# Playlists land in %LOCALAPPDATA%\jucyaudio\MixBackups (or under JUCYAUDIO_CONFIG if set).
[windows]
backup-mixes config=default_build_type:
    just build {{config}}
    $p = Start-Process -FilePath "build-{{arch}}-{{lowercase(config)}}/jucyaudio_artefacts/{{config}}/JucyAudio.exe" -ArgumentList "--export-mix-recovery" -Wait -PassThru; $root = if ($env:JUCYAUDIO_CONFIG) { $env:JUCYAUDIO_CONFIG } else { Join-Path $env:LOCALAPPDATA "jucyaudio" }; Get-Content (Join-Path $root "backfill-results.txt"); if ($p.ExitCode -ne 0) { Write-Host "BACKFILL FAILED"; exit $p.ExitCode }; Write-Host "Backfill complete."

# ============================================================================
# macOS-Specific Build Commands
# ============================================================================

[macos]
_build-macos config arch_target:
    cmake -B build-{{arch_target}} -DCMAKE_BUILD_TYPE={{config}} -DCMAKE_OSX_ARCHITECTURES={{arch_target}}
    cmake --build build-{{arch_target}} -j{{cpu_count}}
    @echo "Build complete: build-{{arch_target}}/jucyaudio_artefacts/{{config}}/JucyAudio.app"

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
    ./build-{{arch}}/jucyaudio_artefacts/{{config}}/JucyAudio.app/Contents/MacOS/JucyAudio

# Headless scan self test (macOS). Runs against a throwaway config root under the build
# directory, never the real library. Exit code is the result; details land in selftest-results.txt.
[macos]
selftest config=default_build_type:
    #!/usr/bin/env bash
    set -euo pipefail
    just build {{config}}
    conf="$PWD/build-{{arch}}/selftest-config"
    root="$PWD/build-{{arch}}/selftest-root"
    rm -rf "$conf" "$root" && mkdir -p "$conf" "$root"
    code=0
    JUCYAUDIO_CONFIG="$conf" JUCYAUDIO_SELFTEST_ROOT="$root"         ./build-{{arch}}/jucyaudio_artefacts/{{config}}/JucyAudio.app/Contents/MacOS/JucyAudio --selftest-scan || code=$?
    cat "$root/selftest-results.txt" "$root/mixrecovery-results.txt" "$root/backup-results.txt"
    if [ "$code" -ne 0 ]; then echo "SELF TEST FAILED"; exit "$code"; fi
    echo "Self test passed."

# Record recovery data for every mix that has none, and write each a playlist (macOS).
[macos]
backup-mixes config=default_build_type:
    #!/usr/bin/env bash
    set -euo pipefail
    just build {{config}}
    root="${JUCYAUDIO_CONFIG:-$HOME/Library/Application Support/jucyaudio}"
    code=0
    ./build-{{arch}}/jucyaudio_artefacts/{{config}}/JucyAudio.app/Contents/MacOS/JucyAudio --export-mix-recovery || code=$?
    cat "$root/backfill-results.txt"
    if [ "$code" -ne 0 ]; then echo "BACKFILL FAILED"; exit "$code"; fi
    echo "Backfill complete."

# ============================================================================
# Package Commands - Windows
# ============================================================================

# Build the x64 MSI installer: configure + build + install (the cmake install
# step stages a clean, self-contained payload incl. the app-local MSVC runtime),
# then MSIS turns setup\jucyaudio-x64.msis into a standalone .msi. Requires
# msis.exe on PATH (https://github.com/gersonkurz/msis) and WiX (msis /SETUP-WIX).
# 2.0 ships x64 only.
[windows]
package-x64:
    cmake --preset x64-release
    cmake --build build-x64-release --config Release
    cmake --install build-x64-release --config Release
    if (-not (Test-Path releases)) { New-Item -ItemType Directory -Path releases | Out-Null }
    Write-Host "Creating MSI installer (x64)..."
    Push-Location setup; msis /BUILD /STANDALONE /SET:PRODUCT_VERSION={{version}} jucyaudio-x64.msis; Pop-Location
    Move-Item -Force "setup/jucyaudio-{{version}}-x64.msi" "releases/"
    Write-Host "Installer created: releases/jucyaudio-{{version}}-x64.msi"

# Alias: default Windows package is the x64 MSI.
[windows]
package:
    @just package-x64

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
    app_path="build-{{arch_target}}/jucyaudio_artefacts/{{config}}/JucyAudio.app/Contents/Resources"
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

# Publish the Windows release (x64 MSI) with a checksum.
[windows]
publish:
    Write-Host "========================================"
    Write-Host "JucyAudio Release Build - Windows"
    Write-Host "Version: {{version}}"
    Write-Host "========================================"
    Write-Host ""
    if (Test-Path releases) { Remove-Item -Recurse -Force releases }
    New-Item -ItemType Directory -Path releases | Out-Null
    just package-x64
    Write-Host ""
    Write-Host "Generating checksums..."
    Push-Location releases; \
    Get-ChildItem *.msi | ForEach-Object { \
        $hash = (Get-FileHash $_.Name -Algorithm SHA256).Hash.ToLower(); \
        "$hash  $($_.Name)" \
    } | Out-File -FilePath checksums.txt -Encoding ASCII; \
    Pop-Location
    Write-Host ""
    Write-Host "Release artifacts in releases\:"
    Get-ChildItem releases\*.msi | ForEach-Object { Write-Host $_.Name }
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
