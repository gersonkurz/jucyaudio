# JucyAudio - AI Development Guide & Session History

**Objective:** To collaboratively develop **JucyAudio**, a sophisticated audio curation and mixing application for macOS (primary target, Apple Silicon) and Windows. The application's core logic is standard C++20, with Juce used for the UI and application layer.

## Collaboration Style & Preferences

*   **Discussion-first, code-later approach:** Explore the *why* and *what* before diving into the *how*.
*   **Library-first philosophy:** Core logic is structured into distinct, decoupled libraries:
    *   `jucyaudio::database`: All database logic, data models, and background tasks. Standard C++20.
    *   `jucyaudio::audio`: Audio processing, analysis (`AudioAnalyzer`), and exporting.
    *   `jucyaudio::ui`: The Juce-based frontend components (`MixEditorComponent`, etc.).
    *   `jucyaudio::config`: The TOML-backed configuration system.
*   **Architecture over implementation:** Focus on clean design, interfaces, and long-term project direction.
*   Human has a strong C++20 background. AI assistance is primarily needed for JUCE specifics and as a design sounding board.
*   Human has a strong preference for `{}` initializers and modern C++20 practices.
*   **No apologies for mistakes or sycophancy; focus on the tasks ahead.**
*   **Remember to use 'const auto' or 'auto' where suitable, not static types.**

**Core Functional Pillars:**

1.  **Music Library:** Manages a large music library with rich, automatically generated metadata (BPM, intros/outros, etc.) stored in a thread-safe SQLite database.
2.  **Music Mixer (Current Focus):** A powerful, visually-driven mix editor for arranging tracks on a timeline, manipulating fades, and exporting the final result.
3.  **Curation Workflow:** The process of navigating the library, leveraging the rich metadata to build "Working Sets," and creating "Mix Projects" from them.

---

## Key Architectural Principles

**The "Pure Cache" Model:**
*   **Hierarchical Database Schema:** A `Folders` table with a `parent_id` and a cached full `path` column represents the library structure. The `Tracks` table stores only a `filename` and a `folder_id`.
*   **Application-Layer Logic:** All logic for case-insensitive lookups and uniqueness is handled in the C++ code.
*   **In-Memory Caching:** A comprehensive, multi-level, path-centric cache for the folder hierarchy is built on startup, making all navigation instantaneous. The cache is "self-healing," persisting reconstructed paths to the database to optimize subsequent startups.
*   **Native Unicode Normalization:** All case-insensitive comparisons are performed using a robust C++ utility function (`normalizeForCache`) powered by a statically-linked ICU library.

---

## Session History & Major Accomplishments

### Session 1-13: (Summary - See previous entries)

### Session 14: Node-Centric Folder Navigation (Complete)
*   **Completed Phase 4 - Folder Navigation in Data View:**
    - Removed unused LogicalFolderNode class (dead code cleanup)
    - Changed VirtualFolderNode from recursive to non-recursive track display
    - Fixed critical blank row issue.
    - Fixed parent hierarchy bugs.
    - Implemented navigation tree synchronization.
    - Enhanced VirtualFoldersOverview (Folders root).
*   **Parent Navigation Bug Fixed:** The issue with ".." navigation from a root folder has been resolved.

### Session 15: UI Polish & Settings Expansion
*   **Progress Bar Theming:** Updated progress bar to use the orange accent color, consistent with the application theme.
*   **Database Backup System:**
    - Implemented a robust, automated weekly backup system.
    - Backups are stored alongside the main database with a `##-YYYY-MM-DD.sqlite` naming scheme.
    - Logic is decoupled from JUCE and runs safely before the database is opened.
    - Pruning of old backups is implemented but currently in a safe "dry-run" mode.
*   **Settings Dialog Enhancement:**
    - Repurposed the "General" tab to house new, advanced settings.
    - Added UI controls for all `MixEditingSettings`, `BackupSettings`, and `LoggingSettings`.
    - Fixed theme-related rendering issues for checkboxes in the light theme.
    - Corrected layout and sizing issues to ensure all text is readable.
*   **Critical Bugfix - Singleton Dialog Lifecycle:**
    - Identified and fixed a critical dangling-pointer bug in the `SingletonDialog` and `SingletonComponentDialog` base classes.
    - The issue prevented dialogs (Settings, Equalizer, Reverb, etc.) from being reopened after being closed.
    - Unified the close logic to ensure cleanup is performed correctly whether the dialog is closed via the mouse or the ESC key.

### Session 16: Theme System Overhaul & Semantic Colors
*   **Semantic Color System Implementation:**
    - Migrated from hardcoded colors to semantic color mappings in `ThemeManager`.
    - Introduced `semanticColourMap` that defines which UI elements use which semantic colors.
    - Themes now only need to define semantic colors (accent, mainBackground, alternateBackground, mainForeground, disabledForeground).
    - The mapping of semantic colors to UI elements is hardcoded in the application, not configurable per theme.
*   **Accent Color for Icons:**
    - Added `accentColourId` that dynamically colors all SVG icons.
    - Icons automatically update when themes change via `lookAndFeelChanged()`.
    - Fixed startup issue where icons appeared black before theme was applied.
*   **Orange Themes Added:**
    - Successfully created Orange-Dark and Orange-Light theme variants.
    - All themes now use the semantic color system for easier maintenance.

### Session 17: Audio DSP & UI Bug Fixes
*   **Fixed Equalizer Stereo/Mono Handling:**
    - Resolved assertion failures in debug mode when processing stereo audio
    - Updated Equalizer to use `ProcessorDuplicator` for IIR filters to handle multi-channel audio
    - Mix engine already converts mono tracks to stereo, ensuring consistent stereo processing
*   **Fixed Checkbox Rendering in Dialogs:**
    - Created shared `CheckboxLookAndFeel` singleton for consistent checkbox rendering across all themes
    - Applied to EqualizerComponent, ReverbComponent, SettingsDialog, and LibraryRootsComponent
    - Resolved LookAndFeel lifetime issues that caused assertions in debug mode
    - Checkboxes now properly render in both light and dark themes

---

## Important Implementation Notes

**Database Schema:** Migration code goes up to version 18 with EQPresets (v17) and ReverbPresets (v18) tables. Note: `latestSchemaVersion` currently set to 15 in code - will be consolidated into clean production schema for 1.0 release.

**Refcounting:** The navigation system uses manual retain/release with atomic reference counting. Works well - just remember to match retains with releases!

**Build Instructions:** DO NOT BUILD - The human will handle all builds. Make code changes only.

---

## Next Major Tasks

### Completed:
✅ FLAC/WAV support  
✅ Export with sample rate mismatch  
✅ Offline media management  
✅ Album UI and navigation  
✅ Waveform caching  
✅ Equalizer with presets  
✅ Reverb with presets  
✅ Unified Timer System (60Hz base with multiplexing)
✅ Folder Navigation/Selection Support
✅ Fix Light-Theme Issues
✅ Implement Dynamic Log Level

### Known Bugs / Issues for 1.0:

1. **Mix Waveform Loading Progress:**
   - While loading the waveforms of a mix, show a progress dialog until all are loaded and cached
   - Display percentage of waveforms loaded
   
2. **VU Meters During Mix Playback:**
   - VU meters don't work while playing a mix (only work during single track playback)
   
3. **Mix Playback During Export:**
   - If mix is playing, it doesn't stop when export starts
   - Cannot stop mix playback during export even if desired
   - Need to coordinate playback state between mix player and exporter
   
4. **Unmounted Mix Folders:**
   - If not all mixes can be shown because a folder is unmounted, show them as disabled/grayed out
   - When selected, show special screen: "Not available because [folder path] is not mounted"
   - Preserve mix entries even when source folders are temporarily unavailable
   
5. **CRITICAL - Mix Editor Performance:**
   - Major performance issues in the mix editor need analysis and optimization
   - Likely related to waveform rendering or timeline updates
   - Profile to identify bottlenecks

### Scheduled for 1.0 Release:

1. **Prepare Database Release for Latest Version:**
   - Roll all migration steps into a single clean schema
   - Start with clean-slate database for production release
   - Update latestSchemaVersion to match actual schema

2. **Intelligent Duplicates Detection:**
   - Configuration-based duplicate management system
   - Quality scoring for automatic keep/remove decisions
   - Three-tier approach: Working Sets, Mixes, Library
   - See `dedupe.md` for detailed design specification

3. **i18n Support:**
   - Implement internationalization framework
   - Support UI translation to multiple languages

4. **Scan Dialog Enhancement:**
   - Add "I moved this folder" option for relocated library folders

5. **Linked Cue-Points:**
   - Implement linked movement of cue-points with attach points

6. **Code Quality Review and Refactoring:**
   - Comprehensive code review
   - Refactor for maintainability and performance

7. **BPM Scanner Control:**
   - Add ability to disable/re-enable automated BPM scanner in background
   - Provide UI controls in Settings dialog

8. **Library View Channel Mode Column:**
   - Add a DataColumn showing audio channel configuration (Stereo/Mono)
   - Display channel mode for each track in the library view

9. **Mix Editor Track Context Menu:**
   - Add right-click context menu for tracks in the Mix Editor
   - Include "Show Track Details" option
   - Display dialog showing filename and all TrackInfo data (BPM, duration, sample rate, bit depth, etc.)

### Nice to Have for 1.0 (if time permits):

1. **In-App Tagging System:**
   - mp3tag-like functionality for editing track metadata
   - Store all edits in database only (never modify source files)
   - UI for batch editing and tag management

2. **MacOS: DMG Image Creation:**
   - Create distributable DMG installer for macOS

### Long-term (Version 2.0):

1. **Unified AI Enrichment Script:**
   - Single Python tool for ALL metadata enrichment (see enrich.md Part 2)
   - Album metadata (genres, moods, tags)
   - WAV path intelligence
   - Cost-controlled, batch processing

---

## Design Documents

- For AI enrichment implementation details, see `enrich.md`
- For duplicate detection system design, see `dedupe.md`