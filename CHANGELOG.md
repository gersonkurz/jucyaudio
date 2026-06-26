# Changelog

All notable changes to JucyAudio will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - Unreleased

### Added

#### VST3 Plugin Hosting
- Master bus VST3 plugin chain with real-time audio processing
- Plugin scanning with dead-man's pedal crash recovery
- Plugin chain persistence to database (survives restarts)
- Plugin editor windows (custom UI with generic fallback)
- Per-plugin and global bypass with state persistence
- Real-time CPU load monitoring
- Configurable VST3 scan paths in Settings
- Full VST processing in WAV and MP3 exports

#### ProjectM Visualizer
- Hardware-accelerated music visualization using projectM v4
- Bundled "Cream of the Crop" preset collection (~9,800 presets)
- Configurable visualizer placement (Bottom, Left, Right panels)
- Automatic random preset switching on track change
- Configurable preset rotation interval
- Audio-to-visualizer bridge via lock-free FIFO

#### Playback Improvements
- Playlist queue with next/prev track navigation
- Now-playing track highlight in library view
- Shuffle and repeat mode persistence

#### Build System
- Offline build mode for airplane-safe development (`just build-offline`)
- Consolidated build automation in justfile (removed legacy .cmd scripts)
- Windows installer migrated from NSIS to a self-contained **MSI** built with the `msis` tool / WiX (`just package-x64`); ships the app-local MSVC runtime, so no VC++ redistributable is required
- `cmake --install` now stages a clean payload (dependency install rules suppressed via `EXCLUDE_FROM_ALL`)

#### Smart Automix
- Energy-aware, intelligent transition point discovery
- EnergyAnalyzer: full-track energy contour and phrase boundary detection
- TransitionCalculator: optimal attach points with energy matching and phrase snapping
- Lazy analysis at mix creation time with database caching
- Automatic transition recalculation on track removal
- Deterministic fallback when analysis data is unavailable
- User preference toggle for smart transitions

### Changed

### Fixed
- Fix shuffle and repeat mode persistence by saving to TOML backend
- Fix virtual dispatch bug in VirtualFolderNode causing off-by-one errors
- Fix visualizer FIFO tap point unification in PlaybackController

---

## [1.1.0] - 2026-01-03

### Fixed

#### Audio Engine
- Fix buffer overflow risk in MixPlaybackEngine by chunking large audio blocks
- Fix double-buffer use-after-free using shared_ptr garbage collection
- Fix mix duration calculation to use effective duration instead of full file duration
- Handle negative cueStart (pre-silence) in playback and export
- Remove gain clamping from export to match real-time playback behavior
- Fix equalizer parameter updates using lock-free atomic shared_ptr

#### Database
- Fix SQL injection in filter criteria by using parameterized queries
- Fix dangling pointer in SQLite blob binding by using SQLITE_TRANSIENT
- Apply filter criteria to FTS5 insert-into-working-set queries (filters were being ignored)
- Add bounds checking to SqliteStatement getText/getBlob column accessors
- Add safe JSON deserialization with defaults and warnings for MixTrack
- Fix incorrect error message in addParam(double)

#### Memory Safety
- Replace raw pointers with unique_ptr in UndoManager and fix TOCTOU race
- Replace raw new/delete with std::vector for MP3 buffer
- Replace pointer-to-integer cast with atomic counter for unique node IDs
- Remove duplicate TrackId and TagId typedef declarations

#### UI
- Fix logical vs bitwise AND in CreateMixDialogComponent::closeThisDialog
- Add numeric validation for filter criteria in FilterParser

#### Background Tasks
- Replace polling loop in BackgroundService::pause() with condition variable
- Escalate catch(...) logging from debug to error level

### Changed
- Remove deprecated MixPlaybackEngine members, migrate to PlaybackState
- Extract crossfade calculation to shared MixTrackUtils helper
- Remove redundant database cache builder implementations
- Add structured ExportResult to surface export I/O warnings to users

### Documentation
- Update roadmap with priorities, migration strategy, and review comments
- Document resampler seek limitation (RT-safety tradeoff)

## [1.0.0] - 2025-06-01

Initial release.
