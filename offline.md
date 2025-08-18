
# JucyAudio - Offline Media Management Implementation Plan

## 1. Objective

To architect and implement a system that gracefully handles intermittently available library root folders (e.g., external hard drives). The application must remain stable and provide a clear, intuitive user experience, distinguishing between online (accessible) and offline (inaccessible) media at all levels of the UI and application logic.

## 2. Guiding Principles

-   **The Database is the Source of Truth:** The SQLite database is the permanent, complete record of the user's library. An unavailable file is a *state*, not a reason for data deletion. The system must be non-destructive.
-   **Clarity Over Automation:** The UI must unambiguously communicate the online/offline status of all library components to the user. There should be no mystery as to why a track cannot be played.
-   **Actionable by Default:** The primary UI views should present the user with content they can interact with *right now*. Unavailable content should be hidden by default to reduce clutter.
-   **Graceful Degradation:** Functionality that depends on file access (playback, export, analysis) should fail gracefully with clear user feedback, rather than crashing or throwing unhandled exceptions.

## 3. Implementation Phases

This project will be implemented in five distinct phases to ensure a logical progression from backend data models to frontend user interaction.

### Phase 1: Backend Infrastructure - Tracking State

**Goal:** Augment the data model and backend services to track and report the online/offline status of each library root.

1.  **Modify `LibraryRootInfo.h`:**
    -   Add a transient state member to the `LibraryRootInfo` struct. This will not be stored in the database.
    ```cpp
    struct LibraryRootInfo
    {
        // ... existing members (id, path, fileCount, etc.)
        bool isOnline { false }; // Transient state, updated at runtime
    };
    ```

2.  **Update `ILibraryRootManager.h`:**
    -   Add a new method to the interface to trigger a status refresh.
    -   Add a method to query the status of a specific root.
    ```cpp
    class ILibraryRootManager
    {
    public:
        // ... existing methods
        virtual void refreshRootStatuses() = 0;
        virtual bool isRootOnline(int rootId) const = 0;
    };
    ```

3.  **Implement in `SqliteLibraryRootManager.cpp`:**
    -   Add a new member variable to cache the statuses: `std::map<int, bool> m_onlineStatusCache;`.
    -   Implement `refreshRootStatuses()`:
        -   Clear the `m_onlineStatusCache`.
        -   Load all `LibraryRootInfo` objects from the database.
        -   For each root, use `juce::File(root.path).exists()` to check for availability.
        -   Populate `m_onlineStatusCache` with the results (`root.id` -> `true/false`).
    -   Implement `isRootOnline(int rootId)`:
        -   Return the value from `m_onlineStatusCache` for the given `rootId`. Handle the case where the ID might not exist (default to `false`).
    -   **Crucially**, call `refreshRootStatuses()` once in the constructor of `SqliteLibraryRootManager` to establish the initial state on application startup.

4.  **Centralize Track Availability in `TrackLibrary.h` / `.cpp`:**
    -   The rest of the application thinks in terms of tracks, not roots. We need a central, performant way to check if a track is online.
    -   Add a new public method: `bool isTrackOnline(int trackId);`
    -   To implement this, you will need a cached mapping from `trackId` -> `rootId`. A simple approach is to query the `root_id` for a track via its `folder_id` and cache the result. *Performance is key here; avoid repeated database lookups for the same track.*

### Phase 2: Core UI - Visualizing State

**Goal:** Update the UI to visually represent the online/offline status determined in Phase 1.

1.  **Update `LibraryRootsComponent`:**
    -   Add a "Status" column to the table.
    -   In the `paintCell` method, call `m_libraryRootManager->isRootOnline()` for the given root.
    -   Draw a visual indicator (e.g., a green circle for "Online", a grey circle for "Offline").
    -   Add a "Refresh" button that calls `m_libraryRootManager->refreshRootStatuses()` and then reloads the component's data.

2.  **Update Navigation Tree (`VirtualFoldersOverview`, `VirtualFolderNode`):**
    -   These nodes must now be aware of the online status of the root they represent.
    -   In the `paintItem` method of the `TreeViewItem` associated with these nodes, check the status via the `LibraryRootManager`.
    -   If the root is offline, draw the node's text using `juce::Colours::grey` or another disabled-state color.
    -   The node should remain expandable to allow browsing of offline metadata.

3.  **Filter Content Views (The "Hide by Default" Rule):**
    -   Modify the data-loading methods in content-providing nodes (`LibraryNode`, `AlbumsNode`, `WorkingSetNode`, `MixNode`).
    -   When fetching lists of tracks or albums, iterate through the results and use `TrackLibrary::isTrackOnline()` to filter out items that are offline.
    -   Only the filtered, online list should be passed to the `DataViewComponent` for display.

4.  **Update Container Node State (`WorkingSetNode`, `MixNode`):**
    -   These nodes need to determine their own aggregate status.
    -   When a node is created, it should check the status of all its constituent tracks.
    -   If **all** tracks are offline, the node itself should render as "disabled" (grayed out) in the navigation tree. If even one track is online, the node should appear fully active.

### Phase 3: Functional Logic - Handling Offline Actions

**Goal:** Modify application behavior to gracefully handle user interactions with offline content.

1.  **Single Track Playback (`EnhancedPlayerComponent` or `PlaybackController`):**
    -   Before attempting to load a track for playback, call `TrackLibrary::isTrackOnline(trackId)`.
    -   If it returns `false`, do not proceed with loading.
    -   Show a `juce::AlertWindow` with an informative message (e.g., "Cannot play track. Please connect the drive containing '/path/to/root'.").

2.  **Mix Playback (`MixPlaybackEngine`):**
    -   In the method that loads the next track in the mix (`loadNextTrack` or similar), perform the `TrackLibrary::isTrackOnline()` check.
    -   If the track is offline:
        -   Do not attempt to create an `AudioFormatReader`.
        -   Log a message to the console/status bar (e.g., "Skipping offline track...").
        -   Treat the track's duration as a period of silence and schedule the next track to play after it.
        -   **Do not stop the entire mix playback.**

3.  **Mix Exporting (`MixExporter`):**
    -   Before the export process begins, add a verification step.
    -   Iterate through all `TrackId`s in the mix to be exported.
    -   For each `TrackId`, call `TrackLibrary::isTrackOnline()`.
    -   If any track is offline, abort the export immediately.
    -   Show an `AlertWindow` that lists the first few missing tracks and tells the user which drives need to be connected.

### Phase 4: Advanced UI - The "Show Unavailable Items" Toggle

**Goal:** Implement the "power user" feature to override the default "hide offline" behavior.

1.  **Add Global State:**
    -   In your application's `Settings` system, add a new boolean value, e.g., `showUnavailableItems`. Default it to `false`.

2.  **Add Menu Item (`MenuManager`):**
    -   Add a new checkable item to the "View" menu: "Show Unavailable Items".
    -   The command for this item should toggle the `showUnavailableItems` setting.

3.  **Modify Content View Logic:**
    -   The content-providing nodes (`LibraryNode`, `AlbumsNode`, etc.) must now respect this setting.
    -   When fetching their data, they will first check the value of `settings::theSettings.showUnavailableItems.get()`.
    -   If `false`, they will perform the filtering as implemented in Phase 2.3.
    -   If `true`, they will skip the filtering and pass the full list of items (both online and offline) to the `DataViewComponent`.

4.  **Update `DataViewComponent` Painting:**
    -   The `DataViewComponent` now needs to be able to render rows in a disabled state.
    -   In its `paintRowBackground` or similar method, it must check if the "Show Unavailable" toggle is on.
    -   If it is, it must then call `TrackLibrary::isTrackOnline()` for the track in that row.
    -   If the track is offline, it should draw the row text in `juce::Colours::grey`.

### Phase 5: Final Polish & Verification

**Goal:** Ensure the system is robust and the user experience is seamless.

1.  **Drive Connection/Disconnection:** The current design primarily checks status on startup. A more advanced feature would be to detect volume mount/unmount events at runtime and trigger `refreshRootStatuses()`. This is a complex "nice-to-have" and can be postponed. For now, the manual "Refresh" button in the `LibraryRootsComponent` is sufficient.
2.  **Testing:**
    -   Test with an external drive connected and disconnected.
    -   Verify that all UI states update correctly after a refresh.
    -   Verify that playback of online tracks works, offline tracks show a dialog, and mixes correctly skip offline tracks.
    -   Verify that the "Show Unavailable Items" toggle works as expected across all content views.
    -   Verify that exporting a mix with an offline track is correctly blocked.

---