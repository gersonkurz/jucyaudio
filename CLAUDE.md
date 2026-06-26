# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**JucyAudio** is an open-source audio player, mix editor, and library manager for macOS (Apple Silicon + Intel) and Windows. Modern C++20 core with JUCE 8 for the UI. SQLite for persistence. GPL licensed.

## Collaboration Style

- **Discussion-first, code-later:** Explore the *why* before the *how*.
- **Modern C++20:** Prefer `{}` initializers, `const auto`/`auto`, modern idioms.
- **No apologies or sycophancy** - focus on tasks ahead.
- **Logging:** Use `spdlog` for all logging, never `std::cout` or `printf`.
- **Naming:** Follow existing patterns (`theTrackLibrary` for singletons, `m_` prefix for members).

### Code style (`.clang-format`)

Enforced and non-obvious - match these when writing new code:
- **Allman braces** - every `{` on its own line, including after `if`/`else`/`for`/`while`/`namespace`/`struct`/`class`/`enum`/lambdas.
- 4-space indent, 160-column limit, `ContinuationIndentWidth: 4`.
- `NamespaceIndentation: All` - contents of namespaces are indented.
- `PackConstructorInitializers: Never` - each ctor initializer on its own line.
- `BinPackArguments: false`, `BinPackParameters: false` - one-per-line when wrapped.
- `AccessModifierOffset: -4` - access labels align with the class keyword.

## Build Commands

The project uses `just` (https://github.com/casey/just) for build automation. All dependencies are fetched automatically via CMake `FetchContent` on first build.

```bash
just build              # Build (defaults to Release)
just debug              # Build Debug configuration
just release            # Build Release configuration
just rebuild            # just clean && just build
just build-offline      # Build without network (requires prior online build)
just run                # Build and run the application
just clean              # Remove all build directories
just info               # Show build information (OS, arch, version, cores)
just publish            # Full release build with packaging (DMG on macOS, MSI installer on Windows)
just publish-offline    # macOS only - publish using cached deps
```

**Windows-specific:**
```bash
just configure x64-release   # Configure using CMake presets (requires VS 2026)
just build-x64               # Build for specific architecture
just build-x86
just build-arm64
just build-all               # Build all three Windows architectures
just package-x64             # Build + clean install + MSI installer (x64)
just package                 # Alias for package-x64
```

`CMakePresets.json` pins the `Visual Studio 18 2026` generator, so `just configure` / `cmake --preset` requires VS 2026. The architecture-specific `just build-*` recipes invoke `cmake` directly and work with any installed VS toolchain.

The Windows installer is an **MSI** built with the [`msis`](https://github.com/gersonkurz/msis) tool (WiX 6/7 backend) from `setup/jucyaudio-x64.msis`. `package-x64` configures, builds, runs `cmake --install` (which stages a clean, self-contained payload including the app-local MSVC runtime into `install-x64-release/bin/`), then invokes `msis /BUILD /STANDALONE`. Requires `msis` on PATH (`msis /SETUP-WIX` provisions WiX). 2.0 ships x64 only; the legacy NSIS scripts under `setup/*.nsi` are superseded.

**Direct CMake (if just isn't available):**
```bash
# Windows
cmake -B build-x64 -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-x64 --config Release --parallel

# macOS
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j8
```

Build output: `build-{arch}/jucyaudio_artefacts/{Config}/JucyAudio.exe` (Windows) or `build-{arch}/{arch}-{Config}/jucyaudio.app` (macOS).

There is no test suite (no CTest targets).

### Offline builds

`just build-offline` passes `-DJUCYAUDIO_OFFLINE_BUILD=ON` to CMake, which sets `FETCHCONTENT_FULLY_DISCONNECTED=ON` and reuses dependencies cached under `build-{arch}/_deps/`.

- One successful **online** build per architecture is required first to populate the cache.
- `just clean` wipes the cache - the next build must be online.
- To set the flag manually: `cmake -B build-x64 -A x64 -DCMAKE_BUILD_TYPE=Release -DJUCYAUDIO_OFFLINE_BUILD=ON`.

## Architecture

### Library-first design

Core logic lives in decoupled namespaces, independent of the UI:

| Namespace | Directory | Purpose |
|---|---|---|
| `jucyaudio::database` | `Database/` | SQLite data layer, navigation nodes, background tasks, scanning |
| `jucyaudio::audio` | `Audio/` | Playback engine, mix export, DSP (EQ/reverb), VST3 plugin hosting |
| `jucyaudio::ui` | `UI/` | JUCE-based components, dialogs, theming, visualizer |
| `jucyaudio::config` | `Config/` | TOML-backed settings persistence |

`Utils/` contains shared helpers (string utils, filter parsing, `AtomicSharedPtr`).

### The "Pure Cache" Model (Database)

`Database/TrackLibrary.*` is the central library/DB entrypoint; everything in `Database/Sqlite/` sits behind interfaces in `Database/Includes/`.

- `Folders` table has `parent_id` and a cached `path` column for hierarchy.
- `Tracks` table stores only `filename` and `folder_id`.
- Case-insensitive lookups via `normalizeForCache()` in `Utils/AssortedUtils.h` (uses platform-native Unicode APIs).
- In-memory cache built on startup for instant navigation.
- Volume and BPM stored as integers multiplied by normalization constants (1000) to avoid floating-point precision issues (`Constants.h`).

### Navigation Node System

The tree navigation is built on `INavigationNode` (`Database/Includes/INavigationNode.h`), which extends `IRefCounted`. Nodes use **manual retain/release with atomic reference counting** - match every `retain()` with a `release()`. Use `EnsureNodeIsReleased` RAII guard for scope-bound ownership. The `expand()` method retains children; callers must release them.

Key node types in `Database/Nodes/`: `RootNode`, `LibraryNode`, `MixNode`, `WorkingSetNode`, `VirtualFolderNode`, `AlbumsNode`, `ExportedRootNode`.

### Node-Centric Command Architecture

Nodes implement a command pattern where the node itself decides behavior for UI events:
- `getCellRenderInfo()` - returns text + semantic `RenderState` (Normal/Accent/Subdued/Inactive), themed by UI
- `onRowActivated()` - returns `RowActivationResult` (navigate to node, navigate to folder, or play track)
- `analyzeDeletionRequest()` - pre-flight check returning deletable IDs + dialog text
- `getTrackInfosForOperation()` / `getAllTrackInfosForOperation()` - extract full `TrackInfo` objects for batch ops

### Audio Pipeline

- `MixPlaybackEngine` (`Audio/MixPlaybackEngine.*`) - real-time mixing and playback core
- `PluginChain` (`Audio/Plugins/PluginChain.*`) - VST3 master effects chain with state persistence
- `PlaybackController` (`UI/PlaybackController.*`) - orchestrates playback, queue management, bridges audio and UI
- Export: `MixExporter` dispatches to format-specific implementations (`ExportMixToMp3`, `ExportMixToWav`, `ExportMixToM3U`)
- Audio thread safety: use lock-free primitives or `AtomicSharedPtr` for parameters accessed by the audio thread

### Background Services

`Database/BackgroundService.*` coordinates async tasks in `Database/BackgroundTasks/`: BPM analysis, energy analysis, waveform loading, MP3 quick check, transition calculation.

### UI Layer

- App entry: `UI/Main.cpp`
- Main container: `UI/MainComponent.*` (command routing, layout)
- Custom theming: `UI/JucyLookAndFeel.*`, `UI/ThemeManager.*` (multiple dark/light themes in `Themes/`)
- `TimerMultiplexer` (`UI/TimerMultiplexer.*`) - unified UI timer updates
- Mix editor: `UI/MixEditorComponent.*`, `UI/TimelineComponent.*`, `UI/MixTrackComponent.*`
- Visualizer: `UI/Visualizer/ProjectMComponent.*` (projectM v4, hardware-accelerated OpenGL)

### SQLite Layer

`Database/Sqlite/` contains domain-specific managers (e.g., `SqliteMixManager`, `SqliteTrackDatabase`, `SqliteFolderDatabase`). Each implements a corresponding `I*` interface from `Database/Includes/`. `SqliteDatabase` is the connection manager. `SqliteStatement`/`SqliteTransaction` are the query primitives. SQLite is compiled from source (`sqlite3.c`) with FTS5, JSON1, and STAT4 enabled.

### Precompiled Header

`pch.h` includes standard library headers and core `Database/Includes/` types. All source files use it.

## For AI Assistants

- Do not attempt to run the full GUI app interactively in automation.
- Running non-GUI validation (configure/build/lint/static checks) is encouraged when feasible.
- After code changes, report what was validated automatically and what still needs manual UI testing.
- See also `docs/build.md` for detailed platform build instructions, `CHANGELOG.md` + `docs/ROADMAP.md` for release status.

### Sibling files to keep in sync

- `AGENTS.md` - parallel guidance for non-Claude agents; declares this file as source of truth. Update if collaboration philosophy changes.
- `GEMINI.md` - parallel guidance for Gemini; same caveat.
- `tasks.md` - open bugs and feature requests at repo root, with file-level pointers. Useful starting point for "what's broken" / "what's next".
