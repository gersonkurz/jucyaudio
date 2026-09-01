MOST IMPORTANT: ALWAYS use direct English, as a competent engineer explaining it to a colleague. Remove dramatic framing, suspense-building, hype, and buzzy metaphors (e.g. 'load-bearing assumption', 'here's the kicker', 'the most instructive part', 'this changes everything'). Plain sentences, no reveals. Keep every technical fact, number, file path, command, and code block exactly intact — only the style changes, not the substance, and do not shorten beyond what removing fluff removes. Output only the rewritten text with no preamble or commentary.

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**JucyAudio** is an open-source audio player, mix editor, and library manager for macOS (Apple Silicon + Intel) and Windows. Modern C++20 core with JUCE 9 for the UI. SQLite for persistence. GPL licensed.

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
just build-x64               # Build x64 (alias of `just build`)
just build-x86               # Build x86
just build-all               # Build x64 + x86
just package-x64             # Build + clean install + MSI installer (x64)
just package                 # Alias for package-x64
```

All Windows build recipes drive the **CMake presets** in `CMakePresets.json` (which pin the
`Visual Studio 18 2026` generator), so `just build`/`run` and Visual Studio share one configured
tree under `build-<arch>-<config>` (e.g. `build-x64-release`). This requires **VS 2026**. Presets
exist for x64 and x86 (debug/release) only — there is no Windows-arm64 preset (2.0 ships x64 only;
add an arm64 preset if that changes). macOS builds are unaffected — they use the `[macos]` justfile
recipes (`build-arm64`/`build-x86_64`/`build-universal`), not presets.

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
| `jucyaudio::config` | `Config/` | TOML backend + typed value/section machinery |

`Utils/` contains shared helpers (string utils, filter parsing, `AtomicSharedPtr`).

Note the split: `Config/` holds only the *mechanism* (`toml_backend.h`, `typed_value.h`, `section.*`).
The actual settings *schema* is declared in `UI/Settings.h` as the `config::theSettings` singleton -
that's where you add a new setting.

### Runtime data locations

Resolved in `UI/Main.cpp` (`initialise()`/`timerCallback()`) and `Utils/AssortedUtils.cpp` (`getConfigRoot()`, `getDefaultConfigRootTemplate()`, `expandPath()`):

- Config root: platform default (`%LOCALAPPDATA%\jucyaudio` on Windows, `~/Library/Application Support/jucyaudio` on macOS), overridable via the `JUCYAUDIO_CONFIG` environment variable. Main.cpp exports that variable at startup so `${JUCYAUDIO_CONFIG}` expands inside config files.
- Database: `${JUCYAUDIO_CONFIG}/jucyaudio.db` by default; `[Database] Filename` in the TOML config overrides it. `DatabaseBackupManager` runs a backup/prune check *before* the DB is opened.
- Themes are **YAML**, not TOML: `Themes/*.yaml`, parsed with `fkYAML` (`third_party/fkYAML`) in `UI/ThemeManager.cpp`.

Pointing the app at a scratch DB via `JUCYAUDIO_CONFIG` is the safe way to test schema/migration changes.

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

#### Schema versioning (easy to get wrong)

All of it lives in `Database/Sqlite/SqliteTrackDatabase.cpp`:

- `initialSqlStatements[]` (top of file) - the schema a **brand-new** database is built from.
- `convergenceSqlStatements[]` (next to it) - objects that were once created only by a migration
  rung. Run from **both** the new-database path and the v32 rung, from one definition, so a fresh
  database and a repaired one hold byte-identical schema text. A new database needs both arrays.
- `latestSchemaVersion` - a literal inside `createTablesIfNeeded()`; the version stamped into `SchemaInfo`.
- `runMigrations()` - the `if (currentVersion < N)` ladder applied to **existing** databases.

A schema change must touch the fresh schema, the version and the ladder, or new and upgraded
databases diverge silently. They already did, for years: six tables, two indexes and eight triggers
existed only in the ladder, so search, both marker tables and the EQ/reverb presets were missing from
every library created with 2.0 code - and the v6 rung's `PRIMARY KEY(mix_id, track_id)` on
`MixTracks`, which the fresh schema never had, stopped upgraded libraries from saving a mix that
holds the same track twice. Both are repaired by v32.

Adding to `convergenceSqlStatements` is for that class of object only. Anything genuinely new belongs
in `initialSqlStatements` plus its own rung, as before.

The migration self test checks that the v32 rung leaves an already-complete database untouched. It is
**not** a whole-ladder convergence check and cannot see structural differences between v4 and v31 -
that needs a frozen old-schema fixture, which is an open task.

### Precompiled Header

`pch.h` includes standard library headers and core `Database/Includes/` types. All source files use it.

## Companion tooling

Separate `uv`/Python projects, not part of the C++ build:

- `scripts/refcountd/` - parses spdlog output for unbalanced `retain()`/`release()` pairs. The first thing to reach for when debugging the manual refcounting in the navigation node system.
- `jucyaudio-python/` - read-only Python access to the jucyaudio SQLite DB (`JucyAudioDB` in `src/jucyaudio/db.py`, pydantic models in `models.py`). Handy for ad-hoc library queries without touching the app.

Run either with `uv run` from its own directory.

## For AI Assistants

- Do not attempt to run the full GUI app interactively in automation.
- Running non-GUI validation (configure/build/lint/static checks) is encouraged when feasible.
- After code changes, report what was validated automatically and what still needs manual UI testing.
- See also `docs/build.md` for detailed platform build instructions, `CHANGELOG.md` + `docs/ROADMAP.md` for release status.

### Sibling files to keep in sync

- `AGENTS.md` - parallel guidance for non-Claude agents; declares this file as source of truth. Update if collaboration philosophy changes.
- `GEMINI.md` - parallel guidance for Gemini; same caveat.
- `tasks.md` - open bugs and feature requests at repo root, with file-level pointers. Useful starting point for "what's broken" / "what's next".

REMEMBER THE MOST IMPORTANT RULE: ALWAYS use direct English, as a competent engineer explaining it to a colleague. Remove dramatic framing, suspense-building, hype, and buzzy metaphors (e.g. 'load-bearing assumption', 'here's the kicker', 'the most instructive part', 'this changes everything'). Plain sentences, no reveals. Keep every technical fact, number, file path, command, and code block exactly intact — only the style changes, not the substance, and do not shorten beyond what removing fluff removes. Output only the rewritten text with no preamble or commentary.

## Review loop

@C:/Projects/yaaadabi/protocol.md

Loop parameters:
- Verify: `just build` && `just selftest`
- Yardstick docs: docs/ROADMAP.md, docs/release-plan-2.0.md, docs/issues.md, tasks.md
- Review focus: C++ lifetime/ownership/UB and audio/message-thread safety;
  cross-platform is an invariant — every change must build and behave on both
  Windows and macOS (JUCE 9), platform-specific code only in existing
  platform seams; code style pinned in CLAUDE.md (.clang-format, spdlog).
