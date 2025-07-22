# JucyAudio - AI Introduction Prompt (v5)

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

**Core Functional Pillars:**

1.  **Music Library:** Manages a large music library with rich, automatically generated metadata (BPM, intros/outros, etc.) stored in a thread-safe SQLite database.
2.  **Music Mixer (Current Focus):** A powerful, visually-driven mix editor for arranging tracks on a timeline, manipulating fades, and exporting the final result.
3.  **Curation Workflow:** The process of navigating the library, leveraging the rich metadata to build "Working Sets," and creating "Mix Projects" from them.

---

## Technical Architecture & Stack (Current State)

*   **Language:** C++20 (UTF-8, modern practices).
*   **Build System:** CMake (v3.22+). Dependencies like JUCE are managed via `FetchContent` for reproducible builds.
*   **Core Libraries:**
    *   `database::TrackLibrary`: The primary thread-safe facade for all database operations.
    *   `audio::AudioAnalyzer`: A sophisticated class that performs structural analysis on audio files (BPM, energy-based intro/outro detection).
*   **Database Backend:** SQLite. Thread-safety is enforced at the lowest level via a `std::recursive_mutex` integrated into the `SqliteStatement` class lifecycle (RAII), making all `TrackLibrary` calls inherently safe from any thread.
*   **Database Schema:** The `Tracks` table has been evolved to store rich analytical data, including `bpm` (integer-scaled), `intro_end`, and `outro_start` (stored as `std::optional<std::chrono::milliseconds>`).
*   **Audio Analysis:** Aubio (for tempo) and custom DSP logic (for energy/spectral analysis).
*   **Audio/UI Framework:** Juce (v8.0.8 or later).

---

## Application and UI Architecture

*   **Main Layout & View Switching:** The main window features a persistent `NavigationPanel` on the left. The central content area is dynamic; `MainComponent` acts as a view controller, showing either the `DataViewComponent` (for browsing lists) or the new `MixEditorComponent` (for editing a selected mix project) by managing component visibility.
*   **`MixEditorComponent`:** The heart of the mixing workflow. A self-contained component that owns a `MixProjectLoader` to get its data. It contains the main `TimelineComponent`.
    *   **`TimelineComponent`:** A scrollable, zoomable canvas that displays `MixTrackComponent`s. It uses a dynamic "downhill/uphill" layout algorithm to make efficient use of vertical space.
    *   **`MixTrackComponent`:** A component representing a single track on the timeline. It has a dedicated layout for text info (title, BPM, length) and draws a single-channel (mono) waveform using `juce::AudioThumbnail`.
*   **Theming System:** A fully data-driven theming engine.
    *   Themes are defined in `.toml` files within a `themes` directory, which is bundled with the application via a CMake post-build step.
    *   A global `ThemeManager` scans this directory at startup.
    *   The menu provides a dynamic list of available themes. Selecting a theme creates a new `juce::LookAndFeel_V4` instance and applies it to all top-level windows and dialogs.
*   **Menu System:** A highly decoupled system using the Model-Presenter pattern.
    *   `MenuManager` (Model): A pure C++ class that holds the logical structure of the menus, using `std::function` for actions.
    *   `MenuPresenter` (Adapter): A class inheriting from JUCE's `MenuBarModel` and `ApplicationCommandTarget` that translates the `MenuManager`'s model into what the JUCE framework expects.
    *   `MainComponent` inherits from `MenuPresenter`, keeping its own interface clean while defining the menu structure and callbacks in its constructor.
*   **`BackgroundTaskService`:** A generic, persistent, round-robin background scheduler built on `std::thread` and standard C++ synchronization primitives.
    *   It manages a list of persistent `IBackgroundTask` providers.
    *   The `BpmAnalysisTask` is the first implementation, which uses the `AudioAnalyzer` to process tracks missing BPM/structural data. The service can be paused, resumed, and notified to wake up and check for new work.

---

## Project Status Highlights (as of our last session)

**This file is used to keep track of the prompts and the general context of the project.

## Session 1: BPM Analysis Optimization

**Objective:** Improve the performance of the BPM analysis feature, which was underutilizing CPU resources.

**Initial State:** A `BpmAnalysisTask` was created to run analysis in the background, but it was slow and inefficient.

**Work Done:**

1.  **Identified Bottlenecks:** Through analysis of the code and logs, we identified several key issues:
    *   **Database Contention:** The initial multi-threaded approach caused severe database locking as many threads tried to read and write to SQLite simultaneously.
    *   **I/O Bottleneck:** The architecture was reading files one by one within the worker threads, making the process I/O-bound, especially on an HDD, and leaving the CPU idle.
    *   **Inefficient Analysis:** The code was reading and analyzing the *entire* audio file for every track, which is unnecessary for BPM detection.
    *   **Redundant Work:** The task was processing all tracks passed to it, even if they already had BPM data.

2.  **Implemented Solutions:**
    *   **Producer-Consumer Pattern:** Refactored the task to use a dedicated reader thread (the producer) that loads audio data into a bounded, thread-safe queue. A pool of worker threads (the consumers) now pulls from this queue, ensuring the disk can read ahead sequentially while the CPUs remain saturated with data to process.
    *   **Batched Database Writes:** Implemented a dedicated writer mechanism. Worker threads push analysis *results* to a separate queue. The main task thread collects these results in batches and writes them to the database in a single transaction, dramatically reducing database overhead.
    *   **Optimized Audio Reading:** The reader thread was modified to only read a 60-second segment from the middle of each audio file, significantly reducing I/O and the amount of data passed to the analysis algorithm.
    *   **Pre-filtering:** The task now begins by querying the database to build a list of only those tracks that actually require analysis, skipping redundant work.
    *   **Code Cleanup:** Implemented `SqliteStatement::reset()` to allow prepared statements to be reused efficiently in loops and removed verbose logging from the core analysis functions to clean up the output.

**Outcome:** The BPM analysis is now significantly faster and makes much more effective use of CPU resources, processing multiple tracks per second.

## Session 2: "Finalize & Export" Workflow

**Objective:** Implement a robust workflow to automate the cleanup of a `Working Set` after a `Mix Project` created from it is finalized and exported.

**Work Done:**

1.  **Database Migration (Schema V1 -> V2):**
    *   To avoid data loss on the existing database, a migration path was implemented in `SqliteTrackDatabase::runMigrations()`.
    *   The migration checks the current schema version and, if it's V1, executes `ALTER TABLE` statements within a transaction to add the new columns.
    *   The schema version is then updated to `2`.

2.  **Schema & Data Model Updates:**
    *   **`Mixes` Table:** Added `source_ws_id` (to link back to the origin `WorkingSet`) and `status` (e.g., 'New', 'Finalized').
    *   **`MixTracks` Table:** Added `is_active` to enable non-destructive "soft deletes".
    *   Updated the C++ data models (`MixInfo`, `MixTrack`) to match the new schema.

3.  **Soft Delete Implementation:**
    *   Modified `SqliteMixManager::removeTrackFromMix` to `UPDATE` the `is_active` flag to `0` instead of performing a hard `DELETE`.
    *   Updated `getMixTracks` to only retrieve tracks where `is_active = 1`, ensuring the UI and export logic only see active tracks.

4.  **"Finalize & Export" Logic:**
    *   Created a new transactional function, `SqliteMixManager::finalizeMix`, which contains the core backend logic:
        *   It checks if the mix `status` is 'New'.
        *   It identifies all tracks (both active and inactive) that were part of the mix creation.
        *   It prunes these tracks from the source `Working Set` referenced by `source_ws_id`.
        *   It updates the mix `status` to 'Finalized'.
    *   Integrated this logic into the UI by creating a new `FinalizeAndExportTask` that is triggered by the "Export" button. This task calls `finalizeMix` and then proceeds with the audio export.

5.  **Connecting the Workflow:**
    *   Updated the `CreateMixDialogComponent` and its underlying logic (`createAndSaveAutoMix`) to accept and store the `source_ws_id` when a mix is first created from a working set.

**Outcome:** The user workflow is now seamless. When a user exports a mix for the first time, the source working set is automatically cleaned up, and the mix is marked as complete, eliminating a tedious and error-prone manual step.
**

1.  **Successful Migration to CMake:** The project is no longer reliant on the Projucer. The entire build is managed by a clean `CMakeLists.txt` file, making it more robust and portable.

2.  **Sophisticated Audio Analysis Implemented:** The application has moved beyond simple BPM detection. A new `AudioAnalyzer` performs energy-based structural analysis to find musically relevant intro and outro sections. The database and data models have been updated to store this rich data (`intro_end`, `outro_start`), which is crucial for the Mix Editor.

3.  **Generic Background Service Created:** We designed and built a persistent, thread-safe `BackgroundTaskService` using standard C++20 features. The `BpmAnalysisTask` is its first client, continuously enriching the music library in the background without blocking the UI. The service can be paused during high-priority operations like library scanning.

4.  **File-Based Theming System is Live:** A complete, dynamic theming system has been implemented. Users can switch between different looks (`.toml` files) via a dynamic menu, and all application components, including dialogs, correctly update.

5.  **`MixEditorComponent` is Functional:** The mix editor is now fully implemented.
    *   The `MainComponent` correctly switches between the `DataViewComponent` and the `MixEditorComponent`.
    *   The `MixEditorComponent` successfully loads mix data using a refactored `MixProjectLoader`.
    *   The `TimelineComponent` can now **visually render and edit mixes**. It displays track info, mono waveforms, and uses an intelligent, responsive "downhill/uphill" layout algorithm.
    *   Interactive timeline editing is supported with drag-and-drop functionality for repositioning tracks.

6.  **"Create Mix" Workflow Refactored:** The user workflow is now more powerful. "Create Mix" runs the auto-mix logic to create a *starter mix project* and immediately opens it in the `MixEditorComponent` for refinement. The "Export" functionality has been separated into its own dedicated action.

7.  **Virtual Folders Optimized:** The folder browsing system has been converted to a virtual folder structure with pre-calculated recursive track counts for improved performance.

8.  **Working Set Creation Enhanced:** Fixed issue where creating working sets from folders with only subfolders would fail. Added recursive track collection support for virtual folders via a new `createWorkingSetFromVirtualFolder` method that uses SQLite's recursive CTE capabilities.

