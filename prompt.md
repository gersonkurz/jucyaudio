## JucyAudio - AI Introduction Prompt (v4)

**Objective:** To collaboratively develop **JucyAudio**, a sophisticated audio curation tool and mixer for macOS (primary target, Apple Silicon) and Windows. The application's core logic is standard C++20, with Juce used for the application layer (UI, audio I/O).

## Collaboration Style & Preferences

*   **Discussion-first, code-later approach:** Explore the *why* and *what* before diving into the *how*.
*   **Library-first philosophy:** Core logic is structured into distinct, decoupled libraries:
    *   `jucyaudio::database`: For all database and data model logic (SQLite, `INavigationNode`s). Standard C++20 only.
    *   `jucyaudio::audio`: For all audio processing, mixing, and analysis (with Juce/Aubio dependencies).
    *   `jucyaudio::ui`: For the Juce-based frontend components.
    *   `jucyaudio::config`: For the configuration system backed by TOML files.
*   **Architecture over implementation:** Focus on clean design, interfaces, and long-term project direction.
*   Human has a strong C++20 background. AI assistance is primarily needed for JUCE/Aubio specifics and as a design sounding board.
*   Human has a strong preference for `{}` initializers and modern C++20 practices.
*   **No apologies for mistakes or sycophancy; focus on the tasks ahead.**

**Core Functional Pillars:**

1.  **Music Library:** Manages a large music library with rich metadata (ID3, BPM, ratings, etc.) stored in an SQLite database, enabling powerful, user-driven querying and filtering.
2.  **Music Mixer:** Accepts curated playlists ("Mix Projects") and enables high-quality automated/assisted transitions, with both WAV and MP3 (via LAME) export capabilities.
3.  **Curation Workflow (Current Focus):** A structured process for users to navigate large collections, identify "candidate" tracks, rate them, organize them into "Working Sets," and assemble them into "Mix Projects."

---

## Technical Architecture & Stack (Current State)

*   **Language:** C++20 (UTF-8, modern practices).
*   **Core Libraries:**
    *   `database::TrackLibrary`: Façade for database operations (tracks, folders, mixes, working sets).
    *   `audio::AudioLibrary`: Façade for audio operations (provides `IMixExporter`).
*   **Database Backend:** SQLite, accessed via custom C++ helper classes.
*   **Configuration System:** A custom, type-safe, hierarchical configuration system implemented with a backend-agnostic design (`ConfigBackend` interface). The current implementation uses a `TomlBackend` (leveraging `toml++`) for persistence. The system supports structured lists of settings (`TypedValueVector`), which is used for managing user-configurable UI columns.
*   **Primary Tagging Library:** TagLib.
*   **Primary Audio Analysis Library:** Aubio.
*   **Primary Audio/UI Framework:** Juce (v8.0.8 or later).
*   **MP3 Encoding:** LAME (linked via Homebrew on macOS, pre-built DLL/LIB on Windows).
*   **Logging:** `spdlog`.
*   **Licensing:** GPL.

---

## Application and UI Architecture

*   **Main Layout:** A 4-quadrant layout with `NavigationPanel` (left), `DataView` (right), and other panels for toolbars and playback status.
*   **`INavigationNode` System:** A sophisticated data model for the UI.
    *   `INavigationNode` is the base interface for all items displayed in the navigation tree and for data sources shown in the `DataView`.
    *   Nodes are reference-counted via a custom `IRefCounted` interface.
    *   **`TypedContainerNode`:** A generic template for parent nodes in the navigation tree (e.g., "Folders") that creates its children on demand via a `ClientCreationMethod` function pointer.
    *   **`TypedOverviewNode`:** A more advanced template that acts as both a parent in the tree *and* a data source for the `DataView` (used for "Mixes" and "Working Sets"). It uses a `TypedItemsOverview` struct (via template specialization) as a policy/strategy for type-specific logic.
*   **`DataViewComponent` (UI Table/List):**
    *   A `juce::TableListBox`-based component driven by the currently selected `INavigationNode`.
    *   **Configurable Columns:** Implements a `DynamicColumnManager` system. The visibility, order, and width of columns are user-configurable and persisted to the TOML config file.
*   **Long-Running Task Management:**
    *   A generic, reusable system for handling background tasks without freezing the UI.
    *   **`ILongRunningTask` Interface:** A ref-counted interface that defines a task's `run` method, its name, and whether it's cancellable.
    *   **`TaskDialog` Component:** A modal `juce::Component` that takes an `ILongRunningTask`, runs it on a background thread, and displays its progress and status.

---

## Project Status Highlights (as of our last session)

1.  **Refactoring Complete for Background Tasks:**
    *   **Mix Export:** Fully refactored into `CreateMixTask` (`ILongRunningTask`) using `MixExporter`. WAV and MP3/LAME export is functional.
    *   **Library Scanning:** Fully refactored into `ScanFoldersTask` (`ILongRunningTask`). The `TrackScanner` class is now a synchronous worker. Selective folder scanning is implemented.

2.  **UI Refresh Logic:**
    *   An `INavigationNode::refreshChildren()` method is implemented for `TypedContainerNode` to intelligently update its list of model children (reusing existing nodes by ID where possible).
    *   A `TypedOverviewNode::refreshCache(bool flushCache)` method allows for explicitly invalidating and reloading list view data.
    *   The **"Jump To"** feature is implemented: after a new Mix or Working Set is created, the UI automatically refreshes the navigation panel, selects the new item, and scrolls it into view.

3.  **Key Functionality Working:**
    *   **Mix Creation:** Auto-mix definition and export to WAV and MP3/LAME.
    *   **Working Set Creation:** Works from both selected tracks and from all tracks in a given view.
    *   **Configurable Columns:** The underlying system for saving/loading column visibility, order, and width is implemented using the TOML backend.
    *   **Database Maintenance:** A "Database Maintenance" task (for VACUUM) is implemented using the `TaskDialog` system.

4.  **Known Issues / Future Work:**
    *   The project's ongoing tasks, enhancements, and bugs are tracked using **Linear.app**. This will serve as the canonical list for future work.