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

# Audio processing libraries
brew install aubio taglib lame

# Required by aubio
brew install ffmpeg

# Unicode support
brew install icu4c

# Audio utilities
brew install libsndfile rubberband libsamplerate fftw
```

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

The bundled .app includes all dependencies (aubio, ffmpeg, etc.) and will run on any Mac without requiring Homebrew.

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
brew reinstall aubio taglib lame ffmpeg icu4c libsndfile rubberband libsamplerate fftw
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

- Get jucyaudio from GIT: `git clone git@github.com:gersonkurz/jucyaudio.git`
- Download `jucyaudio_deps_win32_x64.7z` and extract it to `C:\Projects`, so that you end up with a folder `C:\Projects\jucyaudio_deps_win32_x64` that includes all the Windows x64 dependencies. (Go ahead, try to compile everything on your own, I dare you. I did it, so you don't have to. And I am not going to document it, because I don't want to do it again. If you want to do it, go ahead and PR me the instructions.)

### Building with Visual Studio 2022

- Open Visual Studio 2022 
- Select "Open folder" (not: "Open project or solution")
- Navigate to the `jucyaudio` folder you just cloned
- Visual Studio will automatically detect the CMakeLists.txt file and configure the project
- You may need to install the C++ CMake tools for Windows if prompted
- Once the project is configured, you can build it by selecting "Build" from the menu and then "Build Solution" (or pressing `Ctrl+Shift+B`)
- Before you attempt to start it for the first time, you *must* copy the files from `C:\Projects\jucyaudio_deps_win32_x64\dlls`to the `jucyaudio\build\Debug` or `jucyaudio\build\Release` folder, depending on your build configuration. This is necessary because the application depends on these DLLs to run properly.
That's it! You should now have a working build of JucyAudio on Windows.

### Building with Visual Studio Code

- Open Visual Studio Code
- Install the CMake Tools extension if you haven't already
- Open the `jucyaudio` folder you cloned
- CMake Tools should automatically detect the CMakeLists.txt file
- You may need to configure the CMake kit if prompted (select the appropriate Visual Studio 2022 kit)
- Once configured, you can build the project by running the "CMake: Build" command from the command palette (`Ctrl+Shift+P`)
- You can also run the project by selecting "CMake: Run" from the command palette
- Before you attempt to start it for the first time, you *must* copy the files from `C:\Projects\jucyaudio_deps_win32_x64\dlls`to the `jucyaudio\build\Debug` or `jucyaudio\build\Release` folder, depending on your build configuration. This is necessary because the application depends on these DLLs to run properly.

