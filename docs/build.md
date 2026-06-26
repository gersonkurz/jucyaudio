# Building Instructions for JucyAudio

## Building on macOS

### Pre-Requisites

#### 1. Install Xcode Command Line Tools
```bash
xcode-select --install
```

#### 2. Install Homebrew
If you don't have Homebrew installed:
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

#### 3. Install Dependencies
```bash
# Core build tools
brew install cmake just dylibbundler

# Audio utilities (optional, for system integration)
brew install rubberband libsamplerate
```

Note: All dependencies (JUCE, SoundTouch, TagLib, LAME, spdlog, etc.) are automatically downloaded via CMake's FetchContent during the build process.

#### 4. Install JUCE
JUCE is automatically downloaded via CMake's FetchContent during the build process. No manual installation is required.

#### 5. Clone the Repository
```bash
git clone git@github.com:gersonkurz/jucyaudio.git
cd jucyaudio
```

### Building with just (Recommended)

The project uses `just` for build automation:

```bash
# Build with default configuration (RelWithDebInfo - optimized with debug symbols)
just build

# Build debug version (unoptimized, full debugging)
just debug

# Build release version (optimized, no debug symbols)
just release

# Build in offline mode (airplane-safe - requires prior online build)
just build-offline

# Build and run
just run

# Clean build directory
just clean

# Show build information
just info
```

### Offline Build Mode (Airplane-Safe)

JucyAudio supports offline building for situations where you don't have internet access (e.g., airplane mode):

```bash
# First-time setup (requires internet):
just build

# Subsequent offline builds (no internet required):
just build-offline
```

**How it works:**
- The first `just build` downloads all dependencies via CMake's FetchContent and caches them in `build-{arch}/_deps/`
- Subsequent `just build-offline` commands use the cached dependencies without attempting network updates
- The offline build passes `-DJUCYAUDIO_OFFLINE_BUILD=ON` to CMake, which sets `FETCHCONTENT_FULLY_DISCONNECTED=ON`

**Limitations:**
- You must run a successful online build at least once before using offline mode
- If you clean the build directory (`just clean`), you'll need to rebuild online first
- Switching between architectures requires an initial online build for each architecture

### Building with CMake Directly

If you prefer to use CMake directly:

```bash
# Configure the project
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64

# Build (adjust -j to match your CPU cores)
cmake --build build -j8

# The .app bundle will be in: build/arm64-Release/JucyAudio.app

# For offline builds (after initial online build):
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DJUCYAUDIO_OFFLINE_BUILD=ON
cmake --build build -j8
```

### Creating a Distribution DMG

To create a self-contained DMG for distribution:

```bash
# This builds a Release version and packages it as a DMG with all dependencies bundled
just publish
```

The DMG will be created in the `releases/` directory with the format:
- `JucyAudio-{version}-macOS-arm64.dmg` (Apple Silicon)

The bundled .app includes all dependencies and will run on any Mac without requiring Homebrew.

### Architecture Notes

- **Native builds**: By default, builds target your Mac's architecture (arm64 on Apple Silicon, x86_64 on Intel)
- **Cross-compilation**: Building for Intel on Apple Silicon requires Intel versions of all Homebrew dependencies
- The build system uses `dylibbundler` to automatically bundle all dependencies into the .app

### Build Output Locations

- Native builds: `build/{arch}-{Config}/JucyAudio.app`
  - Example: `build/arm64-Release/JucyAudio.app`
- Cross-compiled builds: `build-{arch}/{arch}-{Config}/JucyAudio.app`
  - Example: `build-x86_64/x86_64-Release/JucyAudio.app`

### Troubleshooting

**Problem**: Build fails with missing library errors
```bash
# Reinstall dependencies
brew reinstall rubberband libsamplerate
```

**Problem**: dylibbundler not found
```bash
brew install dylibbundler
```

**Problem**: Codesigning errors during build
- The build automatically applies ad-hoc codesigning
- Extended attributes are cleaned before signing to prevent errors
- If issues persist, try: `just clean && just build`

## Building on Windows

### Pre-Requisites

- Visual Studio 2022 or later with C++ and CMake tools
- Git: `git clone git@github.com:gersonkurz/jucyaudio.git`

All dependencies are automatically downloaded via CMake's FetchContent - no manual setup required!

### Building with Visual Studio 2022/2026

1. Open Visual Studio
2. Select "Open folder" (not "Open project or solution")
3. Navigate to the `jucyaudio` folder you cloned
4. Visual Studio will automatically detect CMakeLists.txt and configure the project
5. Build via "Build" → "Build Solution" (or `Ctrl+Shift+B`)
6. Run directly from Visual Studio

### Building with Visual Studio Code

1. Install the CMake Tools extension
2. Open the `jucyaudio` folder
3. Select the Visual Studio kit when prompted
4. Run "CMake: Build" from the command palette (`Ctrl+Shift+P`)
5. Run "CMake: Run" to launch

### Building with just (Command Line)

```powershell
# Build with default configuration
just build

# Build in offline mode (airplane-safe - requires prior online build)
just build-offline

# Build specific architectures
just build-x64
just build-x86
just build-arm64
```

### Offline Build Mode (Windows)

Similar to macOS, Windows supports offline builds:

```powershell
# First-time setup (requires internet):
just build

# Subsequent offline builds (no internet required):
just build-offline
```

The offline build works the same way as on macOS - dependencies are cached in `build-{arch}/_deps/` after the first online build.

### Creating the MSI Installer (Windows)

The Windows distributable is an **MSI** built with the [`msis`](https://github.com/gersonkurz/msis) tool (a declarative front-end over the WiX Toolset 6/7).

Prerequisites (one-time):

```powershell
# msis on PATH (https://github.com/gersonkurz/msis/releases), then provision WiX + extensions:
msis /SETUP-WIX
```

Build the installer:

```powershell
just package-x64    # configure + build + cmake --install + msis /BUILD /STANDALONE
```

This produces `releases/jucyaudio-<version>-x64.msi`. Under the hood:

1. `cmake --install` stages a clean, **self-contained** payload into `install-x64-release/bin/` — the app, the projectM/GLEW DLLs, the ~9,800 visualizer presets, themes, licenses, **and the app-local MSVC runtime DLLs**. (Dependency install rules are suppressed via `EXCLUDE_FROM_ALL`, so no headers/static libs/debug DLLs leak in.)
2. `msis /BUILD /STANDALONE setup/jucyaudio-x64.msis` turns that directory into the MSI. Because the runtime ships app-local, no VC++ redistributable prerequisite is required.

The installer creates desktop + Start-Menu shortcuts and an "Open with jucyaudio" shell entry, and registers in Add/Remove Programs. 2.0 ships **x64 only**; the legacy NSIS scripts (`setup/*.nsi`) are superseded by `setup/jucyaudio-x64.msis`.

