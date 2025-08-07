# JucyAudio - Architecture & Status (2025-08-05)

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

## 5. Current Functionality Status

*   **(✓) Application Build & Startup:** The application builds cleanly and starts up, displaying a splash screen during initialization.
*   **(✓) Library Root Management:** The `LibraryRootsComponent` successfully adds, removes, and displays user-defined library root folders.
*   **(✓) Library Navigation:** The folder hierarchy in the navigation panel displays correctly, driven by the user's defined roots.
*   **(✓) Library Scanning & Pruning:** The `TrackScanner` correctly adds new files, updates existing ones, and removes missing tracks and folders from the database. The process is performant.
*   **(✓) Data Display:** Selecting a folder node correctly displays the list of tracks within it.
*   **(Untested) Core Functionality:** All other major systems, while compiling, have not been tested since the refactoring. This includes **Playback, Mix Loading/Editing, BPM Analysis, and Mix Exporting.** These systems remain the highest priority for verification.

## 6. Next Session TODO:

1.  **Full System Verification (Highest Priority):** We have now completed two massive foundational refactoring sessions. The absolute next step is to methodically test all core functionality to identify any fallout.
    *   Test playing a track from the library.
    *   Test opening an existing mix.
    *   Test all mix editor interactions (drag/drop, cue points, etc.).
    *   Test running a BPM analysis.
2.  **Fix Core Functionality:** Address any bugs discovered during verification. The most likely point of failure will be in code that previously expected a `TrackInfo` object to contain a direct `filepath`. This code must be updated to use the new `track.reconstructFullPath(db)` method.
3.  **Code Cleanup:** Physically delete the now-obsolete `LogicalFolderNode` files and any other dead code from the pre-refactor era.

## 7. Message to Next Instance

The foundational work on the library is now complete and considered stable. The architecture for storing, navigating, and scanning is robust and performant. Your primary, non-negotiable task is to pivot to **stabilization and verification** of the application's core purpose: audio playback and mixing. Do not add new features. Work with the user to test the systems listed in the TODO section and fix any regressions.