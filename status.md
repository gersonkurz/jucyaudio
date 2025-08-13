# JucyAudio - Architecture & Status (2025-08-12 End of Day)

## 1. Executive Summary

This document reflects the state of the project after two major refactoring sessions. The first session successfully replaced the library's "brittle root" file path model with a robust, high-performance hierarchical database cache.

The second session (this one) built upon that foundation by creating a complete, user-facing library management system. This includes adding/removing library root folders, initiating targeted scans, and a high-performance, correct implementation of "mark-and-sweep" logic to prune missing files and folders from the database. The core library infrastructure is now considered feature-complete and robust.

## 2. Key Architectural Changes: The "Pure Cache" Model

The application now operates on the following principles:

*   **Hierarchical Database Schema:** A `Folders` table with a `parent_id` and a cached full `path` column represents the library structure. The `Tracks` table stores only a `filename` and a `folder_id`.
*   **Application-Layer Logic:** All logic for case-insensitive lookups and uniqueness is handled in the C++ code.
*   **In-Memory Caching:** A comprehensive, multi-level, path-centric cache for the folder hierarchy is built on startup, making all navigation instantaneous. The cache is "self-healing," persisting reconstructed paths to the database to optimize subsequent startups.
*   **Native Unicode Normalization:** All case-insensitive comparisons are performed using a robust C++ utility function (`normalizeForCache`) powered by a statically-linked ICU library.

## 3. Session Accomplishments: The Great Refactoring (Previous Session)

*   **Database Migration:** Successfully converted the user's 1.4-million-track database to the new hierarchical schema.
*   **ICU Integration:** Statically linked the ICU library for robust, cross-platform Unicode normalization.
*   **Core Rewrite:** Rewrote the entire data access layer, SQL query engine, and UI navigation nodes to use the new "Pure Cache" model, resulting in instantaneous UI navigation.

## 4. Session Accomplishments: Library Management & Scanner Overhaul (This Session)

*   **Library Root Management:**
    *   Implemented a new `LibraryRoots` database table and `ILibraryRootManager` to allow users to define multiple, specific top-level folders for their library.
    *   Created the `LibraryRootsComponent`, a full-featured UI for adding, removing, and initiating scans on these root folders.
    *   Rewrote the `VirtualFoldersOverview` navigation node to be driven by this new system, correctly displaying the user's chosen folders instead of the filesystem root (e.g., `D:\`).
*   **Track Scanner Rewrite & Hardening:**
    *   Completely re-architected the `TrackScanner` to be folder-centric and robust.
    *   Implemented a correct "mark-and-sweep" algorithm: the scanner now builds a cache of all expected folders and tracks, and correctly prunes any that are not found on the filesystem during a scan.
    *   The pruning logic correctly removes both missing tracks and their orphaned parent folders in a two-phase process.
    *   Fixed a critical bug that caused duplicate track insertions when scanning overlapping root folders by implementing a session-scoped "processed paths" log.
*   **Performance Optimization:**
    *   Diagnosed and fixed a severe performance bottleneck in the `SqliteFolderDatabase` cache.
    *   Rewrote the folder cache to be path-centric and self-healing, making folder lookups during a scan a near-O(1) operation and eliminating multi-second delays when processing files.
*   **User Experience:**
    *   Implemented a PNG-based splash screen to provide immediate visual feedback during the application's multi-second startup and cache-building process.
    *   Fixed a critical object lifetime bug related to the splash screen that caused a crash on application shutdown.

## 5. Session Accomplishments: Mix Editor Playback Fix (2025-08-07 Morning)

*   **Problem Identified:** When editing attach points in the mix editor, the UI updated correctly but the playback engine continued using the old cached track positions, requiring an app restart to hear the changes.
*   **Root Cause:** The `MixPlaybackEngine` calculated track start times only once during `loadMix()` and cached them. When attach points were edited, the `MixProjectLoader` data was updated but the playback engine's cached positions were never recalculated.
*   **Solution Implemented:**
    *   Added `recalculateTrackPositions()` method to `MixPlaybackEngine` that recalculates track start times without reloading audio files.
    *   Modified `MixEditorComponent::updateCueAttachInData()` to call this method when attach points change or when the first track's cueStart changes (affects global offset).
    *   The solution is efficient - no disk I/O, just mathematical recalculation of positions.
*   **Result:** Mix playback now immediately reflects attach point changes without requiring an application restart.

## 6. Session Accomplishments: Mix Editor UI Improvements (2025-08-07 Evening)

*   **Mutex Deadlock Fix:**
    *   Fixed a critical deadlock in `MixPlaybackEngine::recalculateTrackPositions()` that occurred when editing attach points.
    *   The method was holding a mutex while calling `setPosition()` which tried to lock the same mutex.
    *   Solution: Created `setPositionInternal()` method that assumes the mutex is already locked.
    
*   **4K Display Support:**
    *   **Problem:** On 4K displays, the mix editor's vertical grid lines and lane calculations only used 2/3 of the available screen space.
    *   **Root Cause:** The TimelineComponent wasn't resizing to match the viewport when the window was maximized.
    *   **Solution:** 
        *   Added `viewportResized()` public method to TimelineComponent.
        *   Made MixEditorComponent notify the timeline when viewport resizes.
        *   Timeline now adjusts its height to match viewport, calculating lanes based on actual available space.
    *   **Result:** Full screen utilization with 9-10 lanes on 4K displays instead of just 4-8 lanes.

## 7. Session Accomplishments: MP3 Export Settings Dialog (2025-08-07 Late Evening)

*   **Problem:** MP3 exports had hardcoded ID3 tags with no way for users to customize them.
*   **Solution Implemented:**
    *   Created a multi-tab Settings Dialog accessible from the toolbar.
    *   Added ExportSettings section to the configuration system (RootSettings structure).
    *   Implemented ExportSettingsTab with fields for Artist, Album, Year, Genre, and Comment.
    *   Integrated with the existing TOML-based configuration system (TomlBackend).
    *   Modified ExportMixToMp3 to read tags from the new configuration settings.
*   **Technical Challenges Overcome:**
    *   Initially created the dialog using a non-existent ConfigFile::getInstance() pattern (hallucination).
    *   Corrected to use the actual config system: config::theSettings with TypedValue's get() and set() methods.
    *   Properly integrated with TomlBackend for persistence.
*   **Result:** Users can now configure default MP3 export tags through the Settings dialog, which persist across application sessions.

## 8. Current Functionality Status

*   **(✓) Application Build & Startup:** The application builds cleanly and starts up, displaying a splash screen during initialization.
*   **(✓) Library Root Management:** The `LibraryRootsComponent` successfully adds, removes, and displays user-defined library root folders.
*   **(✓) Library Navigation:** The folder hierarchy in the navigation panel displays correctly, driven by the user's defined roots.
*   **(✓) Library Scanning & Pruning:** The `TrackScanner` correctly adds new files, updates existing ones, and removes missing tracks and folders from the database. The process is performant.
*   **(✓) Data Display:** Selecting a folder node correctly displays the list of tracks within it.
*   **(✓) Mix Playback:** Audio playback from mixes works correctly and updates immediately when attach points are edited. No more deadlocks.
*   **(✓) Mix Editing:** The mix editor allows editing of cue points, attach points, and envelope points with immediate visual and audio feedback.
*   **(✓) 4K Display Support:** Mix editor properly utilizes full screen height on high-resolution displays.
*   **(Untested) Remaining Systems:** BPM Analysis and Mix Exporting have not been tested since the refactoring.

## 8. Future Enhancement Ideas:

*   **Persistent Waveform Caching:**
    *   Current situation: Mix editor takes a few seconds to generate waveforms for 70-80 tracks on each app start.
    *   Database schema already prepared (WaveformCache table in schema v9) but reverted.
    *   Challenge: JUCE's AudioThumbnailCache is only 5-10 items by default. Mixes need 70-80 tracks cached.
    *   Memory impact: ~4MB for 80 tracks is negligible, but persistence across restarts would improve UX.
    *   Implementation approach: Use AudioThumbnail's `saveToStream()` and `loadFrom()` to serialize to database BLOBs.
    *   Consider hybrid approach: Large in-memory cache (200+ tracks) plus persistent cache for most-recently-used mixes.

## 9. Next Session TODO:

1.  **Continue System Verification:**
    *   ~~Test playing a track from the library.~~ (Verified working)
    *   ~~Test opening an existing mix.~~ (Verified working)
    *   ~~Test all mix editor interactions (drag/drop, cue points, etc.).~~ (Verified working, fixed playback sync issue)
    *   Test running a BPM analysis.
    *   Test mix exporting functionality.
2.  **Fix Any Remaining Issues:** Address any bugs discovered during verification of BPM analysis and mix exporting.
3.  **Code Cleanup:** Physically delete the now-obsolete `LogicalFolderNode` files and any other dead code from the pre-refactor era.

## 9. Session Accomplishments: UI Polish & Bug Fixes (2025-08-08)

*   **LibraryRootsComponent Enhancement:**
    *   Extended `LibraryRootInfo` with `fileCount` and `lastScanned` timestamp fields
    *   Added database migration to schema version 10 (with defensive handling of version 8/9 mismatch)
    *   Implemented automatic file count updates after library scans
    *   Added "Files" and "Last Scanned" columns to the UI with proper sorting
    *   Widened dialog from 700px to 800px to prevent horizontal scrolling
    
*   **Settings Menu Integration:**
    *   Moved Settings from toolbar button to View menu (View → Settings...)
    *   Added proper separator in menu for better organization
    *   Removed standalone Settings button from DynamicToolbarComponent
    
*   **Waveform Cache Expansion:**
    *   Increased AudioThumbnailCache from 10/5 items to 200 items
    *   Now sufficient for large mixes with 70-80 tracks
    *   Prevents constant waveform regeneration when scrolling
    
*   **Track Deletion Dialog Fix:**
    *   Fixed confirmation dialog to properly support Cancel option
    *   Changed from post-deletion dialog to pre-deletion confirmation
    *   Now uses 3-button dialog: "Remove from Both", "Remove from Mix Only", "Cancel"
    *   Dialog now appears BEFORE any deletion occurs, making Cancel truly functional
    
*   **Mix Playback Bug Fixes (Partial):**
    *   Added code to stop mix playback before track deletion to prevent audio engine corruption
    *   Attempted to fix persistent playback issue after track deletion by:
        - Stopping playback before deletion
        - Unloading and reloading mix in playback engine after changes
    *   **ISSUE REMAINS:** Audio still won't resume after track deletion until playing something else
    
*   **Mix Track Display Enhancement:**
    *   Updated MixTrackComponent to show: Artist - Album - Title (duration)
    *   Gracefully handles missing artist/album information

## 10. Session Accomplishments: 7-State Player & OrderInMix Fix (2025-08-09)

*   **7-State Player System Implementation:**
    *   Successfully integrated the new unified PlaybackController with 7 distinct states
    *   Fixed 70+ compilation errors from incomplete implementation
    *   Corrected method name mismatches (setMasterGain vs setGain, getCurrentState vs getState)
    *   Added missing callbacks for mix playback (onMixPlaybackAlwaysRequested, onSeekRequested)
    *   Fixed timer-based position updates for mix playback
    
*   **Critical Bug Discovery: Duplicate OrderInMix Values:**
    *   **Problem:** Database corruption where multiple tracks had the same `orderInMix` value
    *   **Example:** Tracks 15111 and 22650 both had orderInMix=33
    *   **Impact:** When editing attach points, the wrong track would be updated
    *   **Root Cause:** Track deletion operations were NOT re-enumerating `orderInMix` values
    
*   **OrderInMix Re-enumeration Fix:**
    *   **Investigation Results:**
        - Track deletion: ❌ NOT re-enumerating (FIXED)
        - Paste operations: ✅ Correctly re-enumerating 
        - Drag & drop reordering: ✅ Correctly re-enumerating
    *   **Solution Implemented in `SqliteMixManager.cpp`:**
        - `removeTrackFromMix()`: Now queries deleted track's orderInMix, then decrements all higher values
        - `removeTracksFromMix()`: Collects all deleted positions, sorts them, then correctly shifts remaining tracks
    *   **Result:** OrderInMix values now remain sequential (0, 1, 2, ...) after any operation

## 11. Known Issues to Address:

1.  **Critical: Mix Playback After Track Deletion**
    *   Deleting a track during playback stops audio and prevents resumption
    *   Red playback position bar continues moving but no audio plays
    *   Requires playing a different track to "reset" the audio engine
    *   Multiple fix attempts haven't fully resolved the issue
    *   Likely needs deeper investigation of MixPlaybackEngine state management
    *   **Status:** Partially addressed, needs further work

2.  **Database Cleanup Needed:**
    *   Existing user databases may have corrupt `orderInMix` values from previous deletion operations
    *   Should implement a one-time migration to detect and fix duplicate values
    *   Could add integrity check on startup

3.  **Performance: Waveform Loading**
    *   Takes ~1 minute to load waveforms for 70-track mixes
    *   Happens every time user navigates to a mix
    *   HIGH PRIORITY: Need persistent file-based waveform caching
    *   Database groundwork exists (schema v9) but needs implementation

4.  **Efficiency: Track Deletion UI Rebuild**
    *   Deleting a single track rebuilds entire timeline (all waveforms regenerate)
    *   Not easily fixable due to complex recalculation of attach points and positions
    *   Accepted for now but should be optimized in future

## 12. Session Accomplishments: Theme System & Bug Fixes (2025-08-10)

**Theme System Improvements:**
- Created custom color ID system with `CustomColourIds.h` for application-specific colors
- Added `waveformColourId` to theme system for configurable waveform colors
- Updated dark theme: Fixed blue-tinted backgrounds, made all accents consistently orange, white/gray waveforms
- Updated light theme: Dark gray waveforms for better contrast
- Fixed scrollbar colors and button text colors in dark theme

**Library Roots Component Fix:**
- Fixed track count not updating after scanning
- Changed to use folder database's recursive track count calculation
- Calls `invalidateCache()` after scan to force recalculation
- UI properly refreshes with `loadRoots()` after scan completion

## 13. Session Accomplishments: Bug Fixes & New Features (2025-08-11)

*   **Mix Display Refresh:** Fixed a bug where appending tracks to a mix would not update the UI until a restart. The `onMixCreatedCallback` now correctly identifies when the current view is affected and forces a refresh.
*   **Appended Track Initialization:** Fixed an issue where tracks appended to a mix did not have their envelope points initialized correctly. The logic now mirrors the automix creation, providing a proper crossfade envelope.
*   **Theme System Fixes:** Corrected an issue in the light theme where text in edit controls and on checkboxes was invisible (white-on-white). Added the missing `textColourId` definitions to the theme manager and the `light.toml` file.
*   **Mix Export Robustness:**
    *   Fixed a critical bug where exporting a mix that used the same audio file for multiple segments would result in silent audio for the second segment.
    *   Refactored the audio export engine (`ExportMixImplementation`) to correctly handle repeated `TrackId`s by using a vector for timeline positions instead of a map, ensuring each segment is processed independently.
*   **M3U Export Enhancement:**
    *   Corrected the timeline calculation to account for `cueStart`, fixing incorrect negative start times for repeated tracks.
    *   Added a comment before each track indicating its absolute start time in the mix in `HH:MM:SS` format.
    *   Removed the unnecessary and incorrect `#EXTSTART` tag.
*   **Crash Fix (Preventative):** Addressed a potential release-only crash when using "Remove all following tracks". The logic now correctly identifies the selected track by its unique `orderInMix` instead of the potentially duplicated `trackId`, preventing data inconsistencies.
*   **New Feature: VU Meter:**
    *   Implemented a real-time stereo VU meter in the main status panel.
    *   Created a new `VUMeterComponent` with a smooth peak decay animation.
    *   Integrated thread-safe peak level calculation into the `PlaybackController` to drive the meters without affecting audio performance.

## 14. Session Accomplishments: VU Meter & Critical Crash Fix (2025-08-12)

*   **VU Meter Enhancement:**
    *   Improved the `VUMeterComponent` to display a standard, segmented bar with distinct color regions.
    *   The meter now shows green up to 70%, yellow from 70-85%, and red for the top 15%, providing a much clearer indication of audio levels.
*   **Release Build Debugging:**
    *   Modified the `CMakeLists.txt` to automatically add debug symbol generation (`/Zi`) to MSVC release builds. This enables meaningful, symbolicated call stacks in release mode, which was essential for diagnosing the following crash.
*   **Critical Concurrency Crash Fix:**
    *   **Problem:** Identified and fixed a critical race condition that occurred when deleting tracks from a mix (e.g., "Remove all following tracks") while the audio thread was active. The audio thread could attempt to access a partially-deleted track from the mix, leading to a crash when reading its empty envelope points.
    *   **Solution:** Implemented a robust, thread-safe locking strategy.
        *   Replaced the `std::mutex` in `MixPlaybackEngine` with a `juce::CriticalSection` for better integration with the JUCE message loop.
        *   Exposed a safe locking mechanism through the `PlaybackController` by adding a `withMixEngineLock()` method that takes a lambda.
        *   Wrapped the track removal logic in `MainComponent` inside this lock, ensuring that the audio thread and the UI thread cannot access the mix data simultaneously during a modification.
    *   **Result:** The application is now stable when performing destructive operations on a mix that is loaded in the playback engine.

## 15. Known Issues to Address:

1.  **Playback State After Deletion:**
    *   While the crash is fixed, deleting a track during playback stops audio and may prevent it from resuming without "resetting" the engine (e.g., playing a different track).
    *   This is likely a state management issue within the `PlaybackController` or `MixPlaybackEngine` that needs further investigation.
    *   **Status:** Needs further work.

2.  **Database Cleanup Needed:**
    *   Existing user databases may have corrupt `orderInMix` values from previous deletion operations.
    *   Should implement a one-time migration to detect and fix duplicate values.
    *   Could add integrity check on startup.

3.  **Performance: Waveform Loading**
    *   Takes ~1 minute to load waveforms for 70-track mixes.
    *   Happens every time user navigates to a mix.
    *   HIGH PRIORITY: Need persistent file-based waveform caching.
    *   Database groundwork exists (schema v9) but needs implementation.

4.  **Efficiency: Track Deletion UI Rebuild**
    *   Deleting a single track rebuilds entire timeline (all waveforms regenerate).
    *   Not easily fixable due to complex recalculation of attach points and positions.
    *   Accepted for now but should be optimized in future.

## 16. Session Accomplishments: Album Data Model & FTS5 Search (2025-08-12)

*   **Album-Centric Data Model Implementation:**
    *   Successfully added Albums table (schema v12→v13) with proper foreign key relationships
    *   Created full C++ infrastructure: `AlbumInfo.h`, `IAlbumManager.h`, `SqliteAlbumManager` implementation
    *   Integrated AlbumManager throughout the system (SqliteTrackDatabase, TrackLibrary)
    *   Added automatic album detection after library scans
    *   Implemented "conservative" album detection - only creates albums for folders containing tracks from a single album
    *   Successfully populated 230k albums from existing track data
    
*   **Album Detection Philosophy:**
    *   **Key Learning:** Complex SQL queries for data processing are harder to debug and maintain than C++ implementations
    *   Moved album detection logic from convoluted SQL to clean C++ code in folder cache building
    *   Leverages existing high-performance folder cache infrastructure
    *   Album artist population handled efficiently in C++ during cache rebuild
    *   Entire process (including 230k albums) takes less than a second on startup
    
*   **FTS5 Search Enhancement:**
    *   Extended FTS5 search to include album data (future work, schema prepared)
    *   Conservative approach ensures data quality over quantity
    *   Only "fully clear" cases create albums initially, avoiding ambiguous data

*   **StatusBar Refactoring (from feature branch):**
    *   Merged feature/refactor-status-bar branch successfully
    *   Refactored status messages to use new StatusBarComponent with separate message types
    *   Cleaned up and removed feature branch after merge

*   **Critical Bug Fixes:**
    *   Fixed race condition in envelope point editing that caused crashes during mix playback
    *   Added proper thread-safe locking when modifying envelope points
    *   Used existing `withMixEngineLock` pattern for safe concurrent access

## 17. Next Session TODO:

1.  **Nice to Have:** Implement linked movement of cue-points with attach points.
2.  **Performance:** Continue work on persistent waveform caching.
3.  **Known Issue:** Investigate remaining mix playback issue after track deletion.
4.  **Database:** Implement a one-time migration/check for legacy databases with corrupt `orderInMix` values.
5.  **Album UI:** Create navigation nodes and UI for browsing by album.
6.  **Album Enrichment:** Implement Python script for Bandcamp metadata enrichment (Phase 3 of enrich.md).
