# Build automation for JucyAudio project
# Requires: just (brew install just on macOS, scoop install just on Windows)

# Set shell for Windows (use PowerShell)
set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]

# Detect OS
is_windows := if os() == "windows" { "true" } else { "false" }
is_macos := if os() == "macos" { "true" } else { "false" }

# Get architecture for build directory (macOS only)
arch := if is_macos == "true" { `uname -m` } else { "x64" }

# Default build type (RelWithDebInfo for optimized builds with debug symbols)
default_build_type := if is_windows == "true" { "x64-RelWithDebInfo" } else { "RelWithDebInfo" }

# Default config for release builds
default_release_config := if is_windows == "true" { "x64-Release" } else { "Release" }

# Get version from CMakeLists.txt
version := "0.8.0"

# ============================================================================
# Build Commands
# ============================================================================

# Build with specified configuration (Debug, Release, or RelWithDebInfo)
build config=default_build_type:
    @echo "Building JucyAudio [{{config}}]..."
    cmake --preset {{config}}
    cmake --build --preset {{config}} --parallel
    @echo "✓ Build complete: out/build/{{config}}/"

# Cross-compile for a specific architecture
build-cross arch_target config=default_build_type:
    cmake -B build-{{arch_target}} \
        -DCMAKE_BUILD_TYPE={{config}} \
        -DCMAKE_OSX_ARCHITECTURES={{arch_target}}
    cmake --build build-{{arch_target}} -j`sysctl -n hw.ncpu`
    @echo "✓ Cross-compile complete: build-{{arch_target}}/{{arch_target}}-{{config}}/"

# Build Intel version on Apple Silicon
build-intel config=default_build_type:
    @just build-cross x86_64 {{config}}

# Build Universal Binary (both architectures)
build-universal config=default_build_type:
    cmake -B build-universal \
        -DCMAKE_BUILD_TYPE={{config}} \
        -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
    cmake --build build-universal -j`sysctl -n hw.ncpu`
    @echo "✓ Universal build complete: build-universal/arm64_x86_64-{{config}}/"

# Build debug configuration
debug:
    @just build x64-Debug

# Build release configuration
release:
    @just build x64-Release

# Build release with debug info
relwithdebinfo:
    @just build {{default_build_type}}

# ============================================================================
# Clean Commands
# ============================================================================

# Clean build directory
clean:
    @echo "Cleaning build directories..."
    @if (Test-Path out) { Remove-Item -Recurse -Force out }
    @if (Test-Path build) { Remove-Item -Recurse -Force build }
    @echo "✓ Clean complete"

# Clean and rebuild
rebuild config=default_build_type:
    @just clean
    @just build {{config}}

# Clean all package files
clean-packages:
    @echo "🧹 Cleaning old packages..."
    @rm -rf releases
    @find . -name "*.dmg" -type f -delete 2>/dev/null || true

# ============================================================================
# Run Commands
# ============================================================================

# Run the application
run config=default_build_type:
    @just build {{config}}
    ./build/{{arch}}-{{config}}/jucyaudio.app/Contents/MacOS/jucyaudio

# Run debug build
run-debug:
    @just run Debug

# Run release build
run-release:
    @just run Release

# Open the app bundle in Finder
open config=default_build_type:
    @just build {{config}}
    open ./build/{{arch}}-{{config}}/jucyaudio.app

# ============================================================================
# Development Commands
# ============================================================================

# Configure and generate Xcode project
xcode:
    cmake -B build-xcode -G Xcode
    open build-xcode/jucyaudio.xcodeproj

# Generate compile_commands.json for IDE support
compile-commands:
    cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    @if [ -f build/compile_commands.json ]; then \
        ln -sf build/compile_commands.json .; \
        echo "✓ compile_commands.json linked to project root"; \
    fi

# ============================================================================
# Package Commands
# ============================================================================

# Package the application (macOS DMG) - native architecture
package config="Release":
    @just build {{config}}
    cmake --install build --prefix install-{{arch}}
    cd build && cpack -G DragNDrop
    @echo "✓ DMG created: build/*.dmg"
    @ls -lh build/*.dmg 2>/dev/null || true

# Package Intel version as DMG
package-intel config="Release":
    @just build-intel {{config}}
    cmake --install build-x86_64 --prefix install-x86_64
    cd build-x86_64 && cpack -G DragNDrop
    @echo "✓ Intel DMG created: build-x86_64/JucyAudio-*.dmg"
    @ls -lh build-x86_64/*.dmg 2>/dev/null || true

# Package Universal Binary as DMG
package-universal config="Release":
    @just build-universal {{config}}
    cmake --install build-universal --prefix install-universal
    cd build-universal && cpack -G DragNDrop
    @echo "✓ Universal DMG created: build-universal/JucyAudio-*.dmg"
    @ls -lh build-universal/*.dmg 2>/dev/null || true

# ============================================================================
# Release Publishing
# ============================================================================

# Publish release builds for all architectures (macOS)
publish: clean-packages
    @echo "🚀 Starting JucyAudio release build process..."
    @echo "Version: {{version}}"
    @echo ""
    
    # Build and package Apple Silicon version
    @echo "📦 Building Apple Silicon (arm64) release..."
    rm -rf build build-arm64
    @just build Release
    cmake --install build --prefix install-arm64
    cmake --build build --target package
    @if [ -f build/*.dmg ]; then \
        mv build/*.dmg ./JucyAudio-arm64.dmg.tmp; \
        echo "✅ Apple Silicon DMG created"; \
    else \
        echo "❌ Failed to create Apple Silicon DMG"; \
        exit 1; \
    fi
    
    # Build and package Intel version
    @echo ""
    @echo "📦 Building Intel (x86_64) release..."
    @just build-cross x86_64 Release
    cmake --install build-x86_64 --prefix install-x86_64
    cd build-x86_64 && cpack -G DragNDrop
    @if [ -f build-x86_64/*.dmg ]; then \
        mv build-x86_64/*.dmg ./JucyAudio-x86_64.dmg.tmp; \
        echo "✅ Intel DMG created"; \
    else \
        echo "❌ Failed to create Intel DMG"; \
        exit 1; \
    fi
    
    # Optional: Build Universal Binary
    @echo ""
    @echo "📦 Building Universal Binary release..."
    @just build-universal Release
    cmake --install build-universal --prefix install-universal
    cd build-universal && cpack -G DragNDrop
    @if [ -f build-universal/*.dmg ]; then \
        mv build-universal/*.dmg ./JucyAudio-universal.dmg.tmp; \
        echo "✅ Universal DMG created"; \
    else \
        echo "⚠️  Universal DMG creation skipped"; \
    fi
    
    # Move DMGs to release directory
    @echo ""
    @echo "📁 Organizing release files..."
    @mkdir -p releases
    @mv JucyAudio-arm64.dmg.tmp releases/JucyAudio-{{version}}-macOS-arm64.dmg
    @mv JucyAudio-x86_64.dmg.tmp releases/JucyAudio-{{version}}-macOS-x86_64.dmg
    @if [ -f JucyAudio-universal.dmg.tmp ]; then \
        mv JucyAudio-universal.dmg.tmp releases/JucyAudio-{{version}}-macOS-universal.dmg; \
    fi
    
    # Generate checksums
    @echo ""
    @echo "🔐 Generating checksums..."
    @cd releases && shasum -a 256 *.dmg > checksums.txt
    
    # Summary
    @echo ""
    @echo "✨ JucyAudio Release Build Complete!"
    @echo ""
    @echo "📦 Release artifacts in ./releases/:"
    @ls -lh releases/*.dmg | awk '{print "  " $9 " (" $5 ")"}'
    @echo ""
    @echo "🔐 Checksums:"
    @cat releases/checksums.txt | sed 's/^/  /'
    @echo ""
    @echo "🎵 Ready for distribution! 🎉"

# ============================================================================
# Information Commands
# ============================================================================

# Show build sizes for all configurations
sizes:
    @echo "Binary sizes by configuration:"
    @for config in Debug Release RelWithDebInfo; do \
        if [ -f "build/{{arch}}-$config/jucyaudio.app/Contents/MacOS/jucyaudio" ]; then \
            echo -n "  $config: "; \
            ls -lh "build/{{arch}}-$config/jucyaudio.app/Contents/MacOS/jucyaudio" | awk '{print $5}'; \
        fi; \
    done

# List all generated DMGs
list-packages:
    @echo "Available packages:"
    @find . -name "*.dmg" -type f 2>/dev/null | while read dmg; do \
        echo "  $$dmg ($$(du -h "$$dmg" | cut -f1))"; \
    done

# Show current configuration
info:
    @echo "JucyAudio Build Information"
    @echo "============================"
    @echo "OS: {{os()}}"
    @echo "Version: {{version}}"
    @echo "Architecture: {{arch}}"
    @echo "Default build type: {{default_build_type}}"
    @if (Test-Path out/build) { echo "Build directories:"; Get-ChildItem out/build -Directory | ForEach-Object { echo "  $($_.Name)" } } else { echo "No build directory found" }

# Install the application (requires admin privileges)
install config="Release":
    @just build {{config}}
    sudo cmake --install build
    @echo "✓ JucyAudio installed to system"

# Verify the build works
verify:
    @echo "🔍 Verifying JucyAudio build..."
    @just clean
    @just build Release
    @if [ -f "build/{{arch}}-Release/jucyaudio.app/Contents/MacOS/jucyaudio" ]; then \
        echo "✅ Build verification successful"; \
    else \
        echo "❌ Build verification failed"; \
        exit 1; \
    fi