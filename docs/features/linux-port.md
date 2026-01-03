# Linux Port Plan for JucyAudio

## Goal
Make JucyAudio a first-class citizen on Linux, supporting major distributions (Ubuntu/Debian, Fedora, Arch) with native audio backends (ALSA/PulseAudio/PipeWire).

## 1. Analysis of Blockers

The codebase is 95% cross-platform (thanks to JUCE), but contains specific Windows/macOS assumptions:

### Critical Blockers (Compile Errors)
1.  **`Utils/AssortedUtils.cpp`**: Contains `#error "Not implemented"` in `normalizeForCache`. This handles Unicode normalization (NFC).
    *   *Solution*: Use `libicu` (standard on Linux) or `glib` for Unicode normalization.
2.  **`CMakeLists.txt`**: The `else()` block (non-Windows) assumes macOS and links `-framework CoreFoundation`.
    *   *Solution*: Split the `else()` block into `elseif(APPLE)` and `elseif(UNIX AND NOT APPLE)`.

### Logic Issues (Runtime Bugs)
1.  **Path Handling**:
    *   `SqliteFolderDatabase.cpp` uses hardcoded `\\` separators.
    *   *Solution*: Replace with `std::filesystem::path` or `juce::File::getSeparatorString()`.
2.  **Audio Device**:
    *   `MainComponent.cpp` has Windows-specific logic for "Default Audio Device".
    *   *Solution*: Use JUCE's `AudioDeviceManager` default selection for Linux (which usually auto-selects PulseAudio/PipeWire).
3.  **Key Bindings**:
    *   `VK_MEDIA_*` codes are Windows-only.
    *   *Solution*: Use `juce::KeyPress::playKey`, `stopKey` etc.

## 2. Dependencies & Build System

### System Libraries (Install via apt/dnf)
Unlike Windows/Mac (where we bundle everything), Linux relies on system libs for low-level tasks.
*   **Essential**: `libasound2-dev` (ALSA), `libcurl4-openssl-dev`, `libfreetype6-dev`, `libx11-dev`, `libxinerama-dev`, `libxrandr-dev`, `libxcursor-dev`, `mesa-common-dev`, `libgl1-mesa-dev`.
*   **New Dependency**: `libicu-dev` (for Unicode normalization).

### CMake Updates
*   **Linking**: Ensure `CoreFoundation` is NOT linked on Linux.
*   **ZLib**: Use system zlib (`find_package(ZLIB)`).
*   **Install Targets**: Configure `CMAKE_INSTALL_PREFIX` correctly (typically `/usr` or `/usr/local`).

## 3. Implementation Steps

### Phase 1: Compile Fixes
1.  [ ] **`CMakeLists.txt`**: Refactor platform logic. Add `find_package` calls for Linux libs.
2.  [ ] **`Utils/AssortedUtils.cpp`**: Implement `normalizeForCache` using `u_strToUTF8` (ICU) or `g_utf8_normalize` (GLib).
3.  [ ] **`Database/Sqlite`**: Replace all manual string concatenation with `std::filesystem::path`.

### Phase 2: Runtime Fixes
1.  [ ] **Audio Backend**: Test with ALSA and PulseAudio/PipeWire. Ensure latency is acceptable.
2.  [ ] **Themes**: Ensure themes are loaded from `/usr/share/jucyaudio/themes` or `~/.local/share/jucyaudio/themes`.
3.  [ ] **Keycodes**: Map generic media keys.

### Phase 3: Packaging (The "Distro" Phase)
We should target **Flatpak** as the primary distribution method (universal, sandboxed), plus a **DEB** for convenience.

1.  **DEB Package**:
    *   Use CPack's `DEB` generator.
    *   Add `CPACK_DEBIAN_PACKAGE_DEPENDS`.
2.  **Flatpak**:
    *   Create `com.pnandq.jucyaudio.json` manifest.
    *   Define permissions: `--device=all` (for audio), `--filesystem=home` (for music library).

## 4. Proposed `com.pnandq.jucyaudio.json` (Flatpak)

```json
{
  "app-id": "com.pnandq.jucyaudio",
  "runtime": "org.freedesktop.Platform",
  "runtime-version": "23.08",
  "sdk": "org.freedesktop.Sdk",
  "command": "jucyaudio",
  "finish-args": [
    "--share=ipc",
    "--socket=x11",
    "--socket=wayland",
    "--socket=pulseaudio",
    "--device=all",
    "--filesystem=xdg-music"
  ],
  "modules": [
    {
      "name": "jucyaudio",
      "buildsystem": "cmake-ninja",
      "sources": [ { "type": "dir", "path": "." } ]
    }
  ]
}
```

## 5. Risks
*   **Wayland**: JUCE's Wayland support is improving but can be buggy. We might need to force XWayland (`GDK_BACKEND=x11`) initially.
*   **Audio Latency**: Linux audio config is complex. We rely on JUCE to handle this, but users may need to configure JACK/PipeWire manually for low latency.

## 6. Conclusion
Porting is straightforward mostly due to JUCE. The main effort is replacing Windows-isms in path/string handling and setting up the packaging.

# Codex Comments
- Consider adopting XDG base directories for config/data paths to align with Linux conventions.
- For Flatpak, confirm which filesystem permissions are needed for music libraries outside `xdg-music`.
- The Unicode normalization solution should be shared with other platforms to keep cache behavior consistent.

# Claude Comments
- **ICU vs GLib**: Prefer ICU (`libicu-dev`) over GLib for Unicode normalization. ICU is more comprehensive and already a transitive dependency of many Linux systems. The API is `unorm2_normalize()` with `UNORM2_NFC`.
- **XDG paths implementation**: Use `$XDG_CONFIG_HOME/jucyaudio` (default `~/.config/jucyaudio`) for settings and `$XDG_DATA_HOME/jucyaudio` (default `~/.local/share/jucyaudio`) for the database. JUCE's `File::getSpecialLocation()` doesn't handle XDG; implement a custom `LinuxPaths` helper.
- **Path separator handling**: The codebase should already use `std::filesystem::path` which handles separators automatically. Audit for any raw string concatenation with `\\` and replace with `path /= component` or `path.append()`.
- **PipeWire priority**: PipeWire is now the default on Fedora, Ubuntu 22.10+, and most modern distros. JUCE's ALSA backend works through PipeWire's ALSA emulation, but test latency explicitly. Consider adding JACK support for pro-audio users.
- **Flatpak filesystem access**: `--filesystem=xdg-music` only covers `~/Music`. Many users store music elsewhere. Add `--filesystem=host:ro` for read access to all files, or provide a portal-based folder picker for explicit user consent.
- **AppImage alternative**: Consider AppImage as a secondary distribution format - it's simpler than Flatpak (no sandbox) and works on older distros. Use `linuxdeployqt` equivalent for JUCE apps.
- **CI/CD**: Add a GitHub Actions workflow that builds on Ubuntu LTS and runs basic smoke tests. This catches Linux regressions early.
