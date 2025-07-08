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

**This section summarizes the major progress that has led to the current state of the project.**

1.  **Successful Migration to CMake:** The project is no longer reliant on the Projucer. The entire build is managed by a clean `CMakeLists.txt` file, making it more robust and portable.

2.  **Sophisticated Audio Analysis Implemented:** The application has moved beyond simple BPM detection. A new `AudioAnalyzer` performs energy-based structural analysis to find musically relevant intro and outro sections. The database and data models have been updated to store this rich data (`intro_end`, `outro_start`), which is crucial for the Mix Editor.

3.  **Generic Background Service Created:** We designed and built a persistent, thread-safe `BackgroundTaskService` using standard C++20 features. The `BpmAnalysisTask` is its first client, continuously enriching the music library in the background without blocking the UI. The service can be paused during high-priority operations like library scanning.

4.  **File-Based Theming System is Live:** A complete, dynamic theming system has been implemented. Users can switch between different looks (`.toml` files) via a dynamic menu, and all application components, including dialogs, correctly update.

5.  **`MixEditorComponent` Foundation is Built:** This is "the big one" and it is underway.
    *   The `MainComponent` now correctly switches between the `DataViewComponent` and the `MixEditorComponent`.
    *   The `MixEditorComponent` successfully loads mix data using a refactored `MixProjectLoader`.
    *   The `TimelineComponent` can now **visually render a static mix**. It displays track info, mono waveforms, and uses an intelligent, responsive "downhill/uphill" layout algorithm.

6.  **"Create Mix" Workflow Refactored:** The user workflow is now more powerful. "Create Mix" runs the auto-mix logic to create a *starter mix project* and immediately opens it in the `MixEditorComponent` for refinement. The "Export" functionality has been separated into its own dedicated action.

---

## Current Roadmap / Future Work

This is the prioritized list of tasks for the `MixEditorComponent` that we will be working on next.

*   **Trimming:** Allow non-destructive trimming of a track's start/end points by dragging handles.
*   **Playback Engine:** Implement the audible playback of the mix sequence.
*   **Information Display:** Add UI elements for total mix length, rulers, etc.
*   **Volume Envelope Editing:** Interactive editing of fade points and curves.

