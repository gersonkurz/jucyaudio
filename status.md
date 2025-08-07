# JucyAudio - Architecture & Status (2025-08-07)

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

## 5. Session Accomplishments: Mix Editor Playback Fix (2025-08-07)

*   **Problem Identified:** When editing attach points in the mix editor, the UI updated correctly but the playback engine continued using the old cached track positions, requiring an app restart to hear the changes.
*   **Root Cause:** The `MixPlaybackEngine` calculated track start times only once during `loadMix()` and cached them. When attach points were edited, the `MixProjectLoader` data was updated but the playback engine's cached positions were never recalculated.
*   **Solution Implemented:**
    *   Added `recalculateTrackPositions()` method to `MixPlaybackEngine` that recalculates track start times without reloading audio files.
    *   Modified `MixEditorComponent::updateCueAttachInData()` to call this method when attach points change or when the first track's cueStart changes (affects global offset).
    *   The solution is efficient - no disk I/O, just mathematical recalculation of positions.
*   **Result:** Mix playback now immediately reflects attach point changes without requiring an application restart.

## 6. Current Functionality Status

*   **(✓) Application Build & Startup:** The application builds cleanly and starts up, displaying a splash screen during initialization.
*   **(✓) Library Root Management:** The `LibraryRootsComponent` successfully adds, removes, and displays user-defined library root folders.
*   **(✓) Library Navigation:** The folder hierarchy in the navigation panel displays correctly, driven by the user's defined roots.
*   **(✓) Library Scanning & Pruning:** The `TrackScanner` correctly adds new files, updates existing ones, and removes missing tracks and folders from the database. The process is performant.
*   **(✓) Data Display:** Selecting a folder node correctly displays the list of tracks within it.
*   **(✓) Mix Playback:** Audio playback from mixes works correctly and updates immediately when attach points are edited.
*   **(✓) Mix Editing:** The mix editor allows editing of cue points, attach points, and envelope points with immediate visual and audio feedback.
*   **(Untested) Remaining Systems:** BPM Analysis and Mix Exporting have not been tested since the refactoring.

## 7. Next Session TODO:

1.  **Continue System Verification:**
    *   ~~Test playing a track from the library.~~ (Verified working)
    *   ~~Test opening an existing mix.~~ (Verified working)
    *   ~~Test all mix editor interactions (drag/drop, cue points, etc.).~~ (Verified working, fixed playback sync issue)
    *   Test running a BPM analysis.
    *   Test mix exporting functionality.
2.  **Fix Any Remaining Issues:** Address any bugs discovered during verification of BPM analysis and mix exporting.
3.  **Code Cleanup:** Physically delete the now-obsolete `LogicalFolderNode` files and any other dead code from the pre-refactor era.

## 8. Message to Next Instance

The foundational library work is complete and stable. Mix playback and editing have been verified and a critical synchronization bug has been fixed - the playback engine now properly updates when attach points are edited. Your next tasks are to verify BPM analysis and mix exporting functionality. The application is approaching full stability after the major refactoring sessions. Continue with systematic testing and fix any remaining issues.