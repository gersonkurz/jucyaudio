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

# Build and run
just run

# Clean build directory
just clean

# Show build information
just info
```

### Building with CMake Directly

If you prefer to use CMake directly:

```bash
# Configure the project
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64

# Build (adjust -j to match your CPU cores)
cmake --build build -j8

# The .app bundle will be in: build/arm64-Release/JucyAudio.app
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

