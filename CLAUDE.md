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
*   **Remember to use 'const auto' or 'auto' where suitable, not static types.**

**Core Functional Pillars:**

1.  **Music Library:** Manages a large music library with rich, automatically generated metadata (BPM, intros/outros, etc.) stored in a thread-safe SQLite database.
2.  **Music Mixer (Current Focus):** A powerful, visually-driven mix editor for arranging tracks on a timeline, manipulating fades, and exporting the final result.
3.  **Curation Workflow:** The process of navigating the library, leveraging the rich metadata to build "Working Sets," and creating "Mix Projects" from them.

---

## Session 4: Track Marker System Implementation

**Objective:** Implement a comprehensive marker system for the audio player, allowing users to mark specific points in tracks with comments.

**Work Done:**

1. **Database Schema & Migration (V4):**
   - Created `TrackMarkers` table with fields: marker_id, track_id, position_ms, comment, created_at, updated_at, color (optional), emoji (optional)
   - Implemented migration from schema version 3 to 4
   - Added foreign key constraint to cascade delete markers when tracks are deleted

2. **Data Models & Interfaces:**
   - Created `TrackMarker` struct with all marker fields
   - Created `IMarkerManager` interface with CRUD operations
   - Implemented `SqliteMarkerManager` with full database operations
   - Integrated marker manager into `TrackLibrary` and `SqliteTrackDatabase`

3. **UI Implementation:**
   - **Waveform Display Enhancement:**
     - Added marker rendering as orange/yellow vertical lines with triangular indicators
     - Implemented hover detection with visual feedback (color change)
     - Added Ctrl+Click to create new markers at clicked position
     - Click on existing marker to edit/delete
   - **MarkerEditDialog:**
     - Created dialog for adding/editing marker comments
     - Implemented timer-based focus grabbing (following WorkingSetMetaDataEditorDialog pattern)
     - Added save, delete, and cancel functionality
     - Shows marker position in M:SS.mmm format
   - **Tooltip Support:**
     - Made WaveformDisplay inherit from TooltipClient
     - Added TooltipWindow to MainWindow
     - Shows marker position and comment on hover

4. **Integration:**
   - Markers automatically load when tracks are played
   - Markers persist across application sessions
   - Marker operations update the display in real-time

**Technical Highlights:**
- Thread-safe database operations maintained through existing SqliteDatabase mutex
- Modern C++20 practices with `const auto`, aggregate initialization
- JUCE-specific implementations for tooltips and dialog focus management
- Clean separation between database layer and UI components

**Current Status:** 
The marker system is fully functional with create, read, update, delete operations. Users can:
- Ctrl+Click on waveform to create markers
- Click existing markers to edit/delete them
- See tooltips when hovering over markers
- Have all marker data persist in the database

## Session 5: Bad File Detection System

**Objective:** Implement a system to detect and handle audio files with unsupported decoder formats that cannot be played or analyzed.

**Problem:** Some audio files in the library use exotic encoders that neither JucyAudio nor other players (like AIMP) can decode, causing failures during BPM analysis and playback.

**Work Done:**

1. **Database Schema Update (v4→v5):**
   - Added `status` field to Tracks table with three values: 'unknown', 'ok', 'bad_format'
   - Created index on status field for efficient queries
   - Updated existing tracks with BPM data to 'ok' status during migration
   - Fixed initialization issue where index creation in initialSqlStatements happened before migration

2. **Data Model & Interface Updates:**
   - Added `TrackStatus` enum to TrackInfo.h
   - Updated TrackInfo struct to include status field
   - Added `updateTrackStatus()` method to ITrackDatabase interface
   - Modified `getNextTrackForBpmAnalysis()` to exclude bad_format tracks

3. **Bad File Detection During BPM Analysis:**
   - Modified `BpmAnalysisTask` to catch decoder failures (createReaderFor returns null)
   - Collects bad files in thread-safe list during analysis
   - Updates track status to 'bad_format' when decoding fails
   - Shows dialog after analysis with list of bad files

4. **Playback Status Tracking:**
   - Updates track status to 'ok' when playback succeeds
   - Updates track status to 'bad_format' when playback fails
   - Only updates if status hasn't already been set

5. **Working Set Management:**
   - Bad files dialog offers to remove tracks from ALL working sets
   - Iterates through all working sets and removes bad tracks
   - Files remain in library but marked as bad_format
   - Clear user messaging: "Remove from Working Sets" vs "Keep in Working Sets"
   - Status message shows number of bad files and affected working sets

6. **Library Scan Optimization:**
   - Skip files with 'bad_format' status during library scans
   - Prevents re-attempting to process known bad files
   - Improves scan performance by avoiding decoder failures

**Technical Implementation:**
- Thread-safe bad file collection using mutex in BpmAnalysisTask
- Database transactions for status updates
- Batch removal from working sets for efficiency
- Proper UI refresh after bad file operations

**Current State:**
- System successfully detects bad files during BPM analysis and playback
- Bad files are removed from working sets when user chooses
- Files marked as bad_format are skipped in all future operations
- UI properly refreshes after bad file removal
- Clear user communication about what happens to bad files

**Remaining Work for Future Sessions:**

1. **Visual Indicators:**
   - Show bad_format status in data view (red icon or strikethrough)
   - Add status column to track list views

2. **User Controls:**
   - Add ability to manually mark files as bad/good from context menu
   - Add filter option to show/hide bad files in library view
   - Consider adding "retry" functionality for bad files after codec updates

3. **Enhanced Features:**
   - Add deleteTrack method to completely remove tracks from library
   - Batch operations for managing multiple bad files
   - Statistics view showing bad file counts per folder
   - Export list of bad files for user review

4. **Integration:**
   - Consider checking status during file drop/import operations
   - Add bad file warnings during mix creation
   - Prevent bad files from being added to new working sets