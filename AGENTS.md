# Agent Instructions

Primary project guidance lives in `CLAUDE.md`.

## Project Snapshot (updated 2026-03-07)

- Project: `jucyaudio` (C++20 + JUCE), current CMake project version is `2.1.0` (unreleased).
- Platforms: Windows and macOS (Apple Silicon + Intel packaging paths in `justfile`).
- High-level architecture follows the library-first split:
  - `Database/`: SQLite-backed library logic, scanning, background tasks
  - `Audio/`: playback/mix/export DSP, VST3 plugin hosting
  - `UI/`: JUCE application and components
  - `Config/`: TOML-backed settings/config persistence
- Notable 2.0 work already integrated in code/docs: VST3 master effects chain and projectM visualizer.

## Source Of Truth / Doc Order

1. `CLAUDE.md` for collaboration and coding philosophy.
2. `docs/build.md` for build instructions (do not use `build.md` at repo root).
3. `CHANGELOG.md` + `docs/ROADMAP.md` for release status and planned work.
4. Feature docs under `docs/features/` may contain historical plan text; verify against current code/changelog before acting.

## Build And Validation

- Primary automation uses `justfile` recipes (`just build`, `just debug`, `just release`, `just build-offline`, `just info`).
- Windows preset file is `CMakePresets.json` (Visual Studio 2026 generator entries present).
- Dependencies are fetched via CMake `FetchContent`; offline mode is supported after at least one online dependency fetch.
- Prefer non-GUI validation where possible:
  - Configure/build checks via `just`/CMake
  - No established CTest suite detected in current CMake setup
- GUI runtime behavior still requires manual human verification.

## Practical Code Map

- App bootstrap: `UI/Main.cpp`
- Main UI container: `UI/MainComponent.*`
- Playback orchestration: `UI/PlaybackController.*`
- Audio engine core: `Audio/MixPlaybackEngine.*`
- Plugin hosting: `Audio/Plugins/*` and `UI/Plugins/*`
- DB core: `Database/TrackLibrary.*`, `Database/Sqlite/*`
- Config backend: `Config/toml_backend.h`

## Working Notes For Agents

- Keep changes consistent with existing modern C++20 style and current naming conventions.
- Be careful with manual retain/release areas in navigation-related code; match retains and releases exactly.
- Prefer reporting what was validated automatically and what still needs manual UI verification after edits.
