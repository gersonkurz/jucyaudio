# Changelog

All notable changes to JucyAudio will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - Unreleased

### Added
- *(Planned)* ProjectM Integration - hardware-accelerated music visualization
- *(Planned)* VST3 Support - professional audio plugin hosting
- *(Planned)* Smart Automix - beat-aware, intelligent mixing
- *(Planned)* Library Organizer - automated file organization based on metadata

### Changed
- *TBD*

### Fixed
- *TBD*

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
