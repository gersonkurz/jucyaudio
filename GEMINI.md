# JucyAudio: Gemini Context & Instructions

JucyAudio is an open-source audio player, mix editor, and library manager for macOS (Apple Silicon) and Windows. It is designed for managing large local digital audio collections with sophisticated tools for curation and mixing.

## Project Overview

- **Core Technologies:** Modern C++20, JUCE Framework (UI), SQLite (Database), TOML (Configuration), VST3 (Plugins), projectM v4 (Visualization, OpenGL).
- **Architecture:** Decoupled modular design:
  - `jucyaudio::database`: SQLite-backed logic, data models, and background tasks.
  - `jucyaudio::audio`: Audio processing, real-time playback engine, VST3 plugin hosting, and exporting.
  - `jucyaudio::ui`: JUCE-based frontend components, projectM visualizer, and custom LookAndFeel.
  - `jucyaudio::config`: TOML-backed configuration system (handles persistence of shuffle/repeat/UI state).
- **Key Paradigms:**
  - **Pure Cache Model:** Fast library navigation using an in-memory cache and optimized SQLite schema.
  - **Library-First:** Core logic is kept separate from the UI layer.
  - **Async Tasks:** Heavy operations (BPM analysis, waveform generation, VST scanning) run in background services.
  - **Master Plugin Chain:** Real-time audio processing chain for VST3 plugins with state persistence.
  - **Hardware-Accelerated Visualization:** projectM integration for high-performance music visuals.

## Development Infrastructure

### Build System & Tasks
The project uses **CMake** and the **just** task runner for automation.
- **Configure:** `just configure` (or use CMake presets like `x64-release`).
- **Build:** `just build` (defaults to RelWithDebInfo), `just debug`, or `just release`.
- **Run:** `just run` (builds and executes the application).
- **Offline Mode:** `just build-offline` (airplane-safe; requires a prior online build to cache dependencies).
- **Clean:** `just clean`.
- **Package:** `just publish` (generates DMG on macOS or installer/ZIP on Windows for multiple architectures).

### Dependencies
Managed via CMake's `FetchContent`. Major dependencies include:
- JUCE 8, spdlog (logging), tomlplusplus, taglib (metadata), SoundTouch (BPM), LAME (MP3), projectM v4 (visualizer), GLEW (OpenGL).

## Coding Conventions & Style

- **Modern C++:** Use C++20 features; prefer `const auto`, `{}` initializers, and smart pointers.
- **Reference Counting:** Navigation nodes often use manual retain/release with atomic reference counting. Match every retain with a release.
- **Naming:** Follow existing patterns (e.g., `theTrackLibrary` for singletons, `m_` prefix for members).
- **Logging:** Use `spdlog` for all logging; avoid `std::cout` or `printf`.
- **UI:** Inherit from `jucyaudio::ui::JucyLookAndFeel` for custom styling. Use `TimerMultiplexer` for unified UI updates.
- **Audio Thread Safety:** Use lock-free primitives or atomic shared pointers for parameters accessed by the audio thread.

## Key Source Map

- `UI/Main.cpp`: Application entry point and initialization logic.
- `UI/MainComponent.h`: Primary UI container and command routing.
- `UI/PlaybackController.h`: High-level playback logic, queue management, and audio bridge.
- `Audio/MixPlaybackEngine.h`: Real-time mixing and playback core.
- `Audio/Plugins/PluginChain.h`: VST3 hosting and processing logic.
- `UI/Visualizer/ProjectMComponent.h`: projectM visualizer integration.
- `Database/TrackLibrary.h`: Central interface for library and database operations.
- `Config/toml_backend.h`: Configuration loading and management.
- `Database/Sqlite/`: Implementation of the SQLite data layer.

## Operational Notes for AI

- **Non-GUI Validation:** When possible, validate changes using build checks (`just build`) or non-interactive tests.
- **UI Testing:** Full GUI behavior must be verified manually by a human as automated UI testing is not fully established.
- **Context:** Refer to `CLAUDE.md` for specific AI collaboration guidelines and `AGENTS.md` for operational caveats.
