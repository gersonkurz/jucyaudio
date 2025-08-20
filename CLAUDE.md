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

### Session 1-3: The Great Refactoring - Hierarchical Database & Cache System
*   **Database Migration:** Successfully converted the user's 1.4-million-track database to the new hierarchical schema.
*   **ICU Integration:** Statically linked the ICU library for robust, cross-platform Unicode normalization.
*   **Core Rewrite:** Rewrote the entire data access layer, SQL query engine, and UI navigation nodes to use the new "Pure Cache" model, resulting in instantaneous UI navigation.

### Session 4: Track Marker System Implementation
*   Created `TrackMarkers` table with fields: marker_id, track_id, position_ms, comment, created_at, updated_at, color, emoji
*   Implemented `IMarkerManager` interface with CRUD operations
*   Added marker rendering on waveform display with Ctrl+Click to create, click to edit/delete
*   Created `MarkerEditDialog` for adding/editing marker comments
*   Integrated tooltip support showing marker position and comment on hover

### Session 5: Bad File Detection System
*   Added `status` field to Tracks table with values: 'unknown', 'ok', 'bad_format'
*   Modified `BpmAnalysisTask` to catch decoder failures and mark bad files
*   Shows dialog after analysis with list of bad files
*   Offers to remove bad tracks from ALL working sets
*   Bad files are skipped in all future operations

### Session 6: Comprehensive Deletion System Implementation
*   Added `NodeType` enum to identify different navigation node types
*   Implemented batch deletion operations for mixes and working sets
*   Created context-aware deletion (navigation panel vs data view)
*   Proper confirmation dialogs with item counts
*   Automatic UI refresh after successful deletion

### Session 7: Navigation Tree Refactoring
*   Created dedicated `NavigationTree` class to manage all navigation-related operations
*   Improved node interface with generic `removeObjects()` and `deleteThisObject()` methods
*   Simplified MainComponent by delegating to NavigationTree
*   Fixed memory leaks in node management with proper reference counting

### Session 8: Mix Editor Drag & Drop Implementation
*   Added `DragAndDropContainer` to `DataViewComponent` for initiating drags
*   Implemented track reordering with automatic time-shifting to maintain timeline
*   Shared MixProjectLoader architecture ensures consistency between views
*   Fixed infinite retry loop for tracks with bad encoding in background analyzer

### Session 9: Library Management & Scanner Overhaul
*   Implemented `LibraryRoots` table and `ILibraryRootManager` for user-defined library roots
*   Created `LibraryRootsComponent` UI for full CRUD operations on roots
*   Re-architected `TrackScanner` with robust "mark-and-sweep" methodology
*   Fixed severe O(N*M) performance bug in folder path lookups
*   Implemented splash screen for better startup experience

### Session 10: Album UI & Enrichment Planning
*   Created `AlbumsNode` for displaying all albums in filterable table view
*   Added double-click navigation from album to its folder
*   Fixed critical refcounting bugs causing memory leaks
*   Fixed mix creation to handle short tracks (<10s) with adaptive crossfade
*   Added `bitrate` column to Albums table (schema v14)
*   Defined comprehensive enrichment strategy (see enrich.md)

### Session 11: Offline Media Management (Complete)
*   **SQLite Temp Tables:** Created during init for efficient filtering
    - `temp.OfflineFolders` - folders from offline roots
    - `temp.OfflineWorkingSets` - working sets with offline tracks
    - `temp.OfflineMixes` - mixes with offline tracks
*   **Single Computation Point:** All offline detection during startup while splash screen shows
*   **SQL-Level Filtering:** All queries respect `showOfflineTracks` setting (default: false)
*   **UI Updates:** Status indicators, gray/disabled offline items, non-expandable offline folders
*   **Simplified Design:** No error dialogs needed - offline content is simply hidden

### Session 12: DSP Effects - Equalizer & Reverb (Complete)
*   **Equalizer Implementation:**
    - 10-band parametric EQ with frequency/gain/Q controls
    - Preset management system (Factory + User presets)
    - Database migration v17 adds EQPresets table
    - Real-time spectrum analyzer visualization
    - True bypass mode to avoid phase shifts
*   **Reverb Implementation:**
    - Master reverb using JUCE's dsp::Reverb processor
    - Parameters: roomSize, damping, wetLevel, dryLevel, width, freezeMode
    - Database migration v18 adds ReverbPresets table
    - Factory presets: Small Room, Large Hall, Cathedral, Plate, Spring, Ambient, Subtle
    - Processing chain: Audio → EQ → Reverb → Output
*   **UI Integration:**
    - Both effects accessible from toolbar
    - Theme-aware dialogs matching main window
    - Independent enable/bypass controls

### Session 13: Unified Timer System (Complete)
*   **TimerMultiplexer Implementation:**
    - Single 60Hz base timer in MainComponent
    - Components register for callbacks at desired frequencies (VU meters: 25Hz, EnhancedPlayer: 20Hz, MixEditor: 60Hz)
    - Frame-based interval calculation for precise timing
    - Eliminated beat frequency interference between multiple timers
    - Smooth playhead animation now possible at 60fps

### Additional Sessions Summary:

**Mix Editor Enhancements:**
*   Fixed playback sync when editing attach points
*   4K display support with proper viewport resizing
*   Mix markers system (like SoundCloud comments)
*   Drag & drop track reordering

**Audio System:**
*   7-state unified PlaybackController implementation
*   VU meter with segmented display (green/yellow/red)
*   Fixed sample rate mismatch in export (resampling)
*   Thread-safe locking for concurrent access

**UI Polish:**
*   Theme system with dark/light modes
*   Settings dialog for MP3 export tags
*   Waveform caching (200 items)
*   Library roots component with file counts

**Critical Fixes:**
*   OrderInMix re-enumeration after track deletion
*   Concurrency crash fixes with proper locking
*   Memory leak fixes in navigation system
*   NaN/infinite value validation in VU meter

---

## Important Implementation Notes

**Database Schema:** Migration code goes up to version 18 with EQPresets (v17) and ReverbPresets (v18) tables. Note: `latestSchemaVersion` currently set to 15 in code - will be consolidated into clean production schema for 1.0 release.

**Refcounting:** The navigation system uses manual retain/release with atomic reference counting. Works well - just remember to match retains with releases!

**Mix Editor:** Uses shared `MixProjectLoader` between DataView and Timeline for consistency.

**Performance:** Waveform caching is fully implemented - no longer an issue.

**Offline Media:** Handled via temp tables created at startup. Offline content is filtered at SQL level.

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

### Required for 1.0 Release:

#### High Priority:

1. **Prepare Database Release for Latest Version:**
   - Roll all migration steps into a single clean schema
   - Start with clean-slate database for production release
   - Update latestSchemaVersion to match actual schema

2. **Folder Navigation/Selection Support:**
   - Enable folder-based navigation and selection in the UI

3. **Fix Light-Theme Issues:**
   - Address remaining visual/contrast issues in light theme mode

#### Medium Priority:

1. **Orange-Light / Orange-Dark Themes:**
   - Design new orange-based themes
   - Set one as default theme

2. **Intelligent Duplicates Detection:**
   - Improve existing working-set duplicate detection
   - Add more sophisticated duplicate identification algorithms

3. **i18n Support:**
   - Implement internationalization framework
   - Support UI translation to multiple languages

4. **Scan Dialog Enhancement:**
   - Add "I moved this folder" option for relocated library folders

5. **In-App Tagging System:**
   - mp3tag-like functionality for editing track metadata
   - Store all edits in database only (never modify source files)
   - UI for batch editing and tag management

6. **Linked Cue-Points:**
   - Implement linked movement of cue-points with attach points

7. **Code Quality Review and Refactoring:**
   - Comprehensive code review
   - Refactor for maintainability and performance

8. **MacOS: DMG Image Creation:**
   - Create distributable DMG installer for macOS

### Long-term (Version 2.0):

1. **Unified AI Enrichment Script:**
   - Single Python tool for ALL metadata enrichment (see enrich.md Part 2)
   - Album metadata (genres, moods, tags)
   - WAV path intelligence
   - Cost-controlled, batch processing

---

For enrichment implementation details, see `enrich.md`.