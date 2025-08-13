# JucyAudio - Database Quality & AI Enrichment Plan (v9 - Phase 1 Extended)

**Objective:** This document outlines a two-part project. Part 1 involves an incremental enhancement of the JucyAudio C++ application's database to add an album-centric model alongside existing track data. Part 2 details the creation of a standalone Python script to enrich this new album data using an LLM API.

**Phase 1 Status: COMPLETE (2025-08-12)**
- Albums table created and populated with 230k+ albums
- Full C++ infrastructure implemented
- Album detection integrated into maintenance tasks
- Conservative approach successfully identifies "fully clear" album cases

**Phase 1.5 Status: IN PROGRESS (2025-08-13)**
- ✅ AlbumsNode UI implementation complete - shows all albums in a filterable table view
- ✅ Album navigation working - double-click on album navigates to its folder in the tree
- ✅ Refcounting issues in navigation system fixed
- ✅ Mix creation fixed to handle short tracks (intros) properly - adapts crossfade duration
- ✅ VUMeter crash fix - added validation for NaN/infinite values
- ✅ Schema v14 migration added - Albums table now includes bitrate column (nullable INTEGER)

## Collaboration Protocol

**This is the most important section.** You are to work as a collaborative agent, not an autonomous one.

1.  **Ask Before Acting:** For every single step outlined below (e.g., "Create the new `Albums` table"), you must first state what you are about to do and **wait for the human user's explicit approval** before writing any code.
2.  **One Step at a Time:** Do not combine steps. Complete one task, present the code or result, get verification, and then move to the next.
3.  **Verify Each Step:** The human user must be given the opportunity to review and verify every change, from SQL migration statements to C++ class modifications and Python script functions.

---

## Part 1: C++ Database Enhancement (Album-Centric Model)

**Objective:** To enhance the SQLite database schema and application UI by adding album-level management for genres, tags, and related metadata, while preserving all existing track-level data.

### Step 1.1: New Database Schema Definition ✅ COMPLETE

**Action:** Propose the SQL schema for a new `Albums` table and the modifications for the existing `Tracks` table.

**Album Identification Strategy:** 
- **Phase 1 (Conservative):** Only create album entries for "fully clear" cases - folders that contain tracks from a single album only. This will be determined by C++ logic analyzing the folder contents.
- **Future phases:** Can expand to handle more complex cases like multi-disc albums, compilations, etc.
- A unique album is defined by the combination of its `album_title` and its parent `folder_id`.

**`Albums` Table (Schema v14):**
```sql
CREATE TABLE Albums (
    album_id INTEGER PRIMARY KEY AUTOINCREMENT,
    album_artist TEXT, -- e.g., 'Artist Name' or 'Various Artists'
    title TEXT NOT NULL,
    year INTEGER,
    folder_id INTEGER NOT NULL, -- Part of the composite key for uniqueness
    genres TEXT, -- Stored as a JSON string array
    moods TEXT, -- Stored as a JSON string array
    tags TEXT, -- Stored as a JSON string array
    bandcamp_url TEXT,
    bitrate INTEGER, -- Average bitrate in kbps (added in v14)
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (folder_id) REFERENCES Folders(folder_id) ON DELETE CASCADE
);
```

**`Tracks` Table Modifications:**
* **ADD** column: `album_id INTEGER REFERENCES Albums(album_id) ON DELETE SET NULL` (nullable to handle tracks not yet associated with albums).
* **KEEP** existing columns: `album_title`, `album_artist_name`, `year` (for data safety and gradual migration).
* **KEEP** existing tables: `Tags`, `TrackTags` (can be migrated/removed in a future phase after validation).

### Step 1.2: Database Migration Plan (Schema Version 13-14) ✅ COMPLETE

**Action:** Propose the sequence of SQL statements to migrate the database within the existing C++ migration framework.

1.  **Begin Transaction.**
2.  **Create `Albums` Table** and its `UNIQUE` index on `(title, folder_id)`.
3.  **Add `album_id` to `Tracks`** (nullable) and create its index.
4.  **C++ Logic Phase:** ✅ COMPLETE - Album detection runs during:
    - Database maintenance tasks (Tools menu)
    - After library scans
    - Analyzes each folder to identify "fully clear" album cases
    - Creates Album entries only for these clear cases
    - Updates the `album_id` in Tracks for tracks belonging to these albums
5.  **Update Schema Version** to 13 and **Commit Transaction.**
6.  **Schema v14 (2025-08-13):** Added `bitrate INTEGER` column to Albums table for storing average bitrate.

**Note:** Initial population of Albums table will be done by C++ logic, not SQL, to ensure only high-confidence albums are created.

### Step 1.2a: Full-Text Search (FTS5) Integration

**Action:** Update the existing FTS5 infrastructure to include album data.

1.  **Phase 1:** Keep existing `TracksSearchData` and `TracksSearchFTS` tables as-is.
2.  **Phase 2 (after albums are populated):** 
    - Update `TracksSearchData` to JOIN with Albums table for genre/tag data
    - Rebuild the FTS5 index using the maintenance task
3.  **Optional Future:** Create separate `AlbumsFTS` virtual table if album-specific searching is needed.

### Step 1.3: C++ Application Logic Updates ✅ COMPLETE

**Action:** Identify and propose changes to the C++ backend to support the new schema.

**Implementation Note:** Album detection was implemented using C++ logic in the folder cache building process rather than complex SQL queries. This approach proved to be more maintainable, debuggable, and performant - processing 230k albums in under a second.

* **Data Models:** 
    - Create `AlbumInfo` struct
    - Update `TrackInfo` struct to include optional `albumId` field
* **Album Detection Logic:**
    - Create `AlbumDetector` class that analyzes folders to identify "fully clear" album cases
    - Logic: A folder is "fully clear" if all tracks have the same non-null `album_title`
* **Database Manager (`IAlbumManager`):** 
    - Create new interface and implementation for album management
    - Methods: `findOrCreateAlbum(title, folderId)`, `updateAlbumMetadata()`, etc.
* **`TrackScanner` or separate task:** 
    - After scanning, run album detection logic
    - Create albums only for "fully clear" cases
    - Update track->album associations
* **Search Logic:** Initially unchanged; update in Phase 2 after albums are populated.

### Step 1.4: UI and Navigation Integration

**Action:** Propose the new navigation nodes required to browse albums in the UI.

* **Create a `RootAlbumsNode`:** This will be a new top-level node in the navigation panel (e.g., alongside "Folders", "Mixes"). It will serve as the parent container for all albums. Its children will be `AlbumNode` instances.
* **Create an `AlbumNode`:**
    * This node will represent a single album. Its display name could be in the format `"Album Artist - Album Title"`.
    * When an `AlbumNode` is selected in the navigation panel, the `DataViewComponent` on the right should display all the tracks associated with that album's `album_id`.
* **Update `NavigationTree`:** The main `NavigationTree` class must be updated to build and display these new nodes, fetching all albums from the database to populate the `RootAlbumsNode`.

---

## Part 1.6: WAV/FLAC Import Strategy (C++ Implementation)

**Objective:** Add support for FLAC and WAV files with intelligent metadata handling.

### FLAC Support (Simpler):
- JUCE already supports FLAC playback
- FLAC files contain proper metadata tags like MP3
- Extract tags using existing MP3 tag extraction logic
- No special handling needed beyond file extension recognition

### WAV Support (More Complex):
- WAV files lack embedded metadata tags
- Initial import strategy:
  1. Import WAV files with basic file info (size, duration, bitrate)
  2. Use filename as provisional title
  3. Use parent folder name as provisional album
  4. Set `needs_enrichment = true` flag in database
  5. Files are immediately playable but with minimal metadata

### Database Schema Addition (v15):
- Add `needs_enrichment BOOLEAN DEFAULT FALSE` to Tracks table
- Add `original_path TEXT` to store full path for AI parsing
- Index on `needs_enrichment` for efficient queries

### Future: In-App Tagging System
- Database-only metadata editing (never modify source files)
- Particularly important for WAV files from vinyl digitization
- Similar to mp3tag functionality but integrated into JucyAudio

---

## Part 2: Python AI Enrichment Script (Expanded Scope)

**Note:** This will be a unified enrichment tool handling multiple AI-powered tasks. All AI operations are database-only, with no UI requirements.

**Objective:** To create a high-quality, standalone Python command-line script that connects to the `jucyaudio.sqlite` database and uses an LLM API for multiple enrichment tasks:

1. **Album Metadata Enrichment** - Add genres, moods, tags to albums
2. **WAV Path Intelligence** - Parse complex file paths to extract metadata for WAV files
3. **Missing Metadata Enhancement** - Fill gaps in track information using contextual clues

**Key Design Principles:**
- **Cost Control:** Process in batches, run on-demand
- **Progressive Enhancement:** Library remains usable while enrichment happens over time
- **Database-Only:** All changes stored in database, never modifying source files
- **Selective Processing:** Target specific folders, time periods, or items marked as `needs_enrichment`

### Step 2.1: Project Setup & Dependencies

**Action:** Propose the initial project structure and `pyproject.toml` file.

* **Environment Manager:** Use `uv`. The script should be runnable via `uv run ...`.
* **Python Version:** Must be `3.13`.
* **Dependencies:**
    * `typer[all]`: For the command-line interface.
    * `loguru`: For elegant and powerful logging.
    * `anthropic`: The client library for the Claude API.
    * `pydantic`: For data validation of API responses.
    * `python-dotenv`: For managing environment variables (like API keys).
* **Code Quality Tools (to be configured in `pyproject.toml`):**
    * `black`: For code formatting.
    * `isort`: For import sorting.
    * `pylint`: For static analysis.
    * `mypy`: For type checking.

### Step 2.2: Script Implementation Plan

**Action:** Propose the structure and logic for the Python script's functions.

1.  **Configuration:**
    * Load configuration (e.g., database path, Anthropic API key) from a `.env` file using `python-dotenv`.
    
2.  **CLI Interface (`typer`):**
    * Multiple commands for different enrichment tasks:
      - `enrich-albums` - Add genres/moods/tags to albums
      - `parse-wav-paths` - Extract metadata from WAV file paths
      - `fill-missing` - Enhance tracks with incomplete metadata
    * Common options: `--db-path`, `--limit`, `--dry-run`, `--folder` (target specific folder)
    
3.  **Database Module:**
    * Create a `DatabaseManager` class to handle all SQLite interactions:
      - `get_albums_to_process()` - Albums needing genre/mood data
      - `get_wav_tracks_needing_parsing()` - WAV files with minimal metadata
      - `get_tracks_with_missing_data()` - Tracks flagged as `needs_enrichment`
      - `update_album_metadata()` - Update album genres, moods, tags
      - `update_track_metadata()` - Update track artist, title, album from path parsing
      - `mark_enrichment_complete()` - Clear the `needs_enrichment` flag
      
4.  **AI Module:**
    * Create an `EnrichmentService` class with multiple methods:
      
      a. **Album Enrichment:**
      ```python
      def fetch_album_metadata(artist: str, title: str) -> AlbumMetadata:
          # Returns: genres, moods, tags, possible year
      ```
      
      b. **Path Intelligence:**
      ```python
      def parse_music_path(full_path: str) -> TrackMetadata:
          # Example: "E:\AUS\part3\Teil 1\Taksi Inc- nachtschleife - Force Inc\..."
          # Returns: artist, title, album, label, catalog_number
      ```
      
      c. **Metadata Enhancement:**
      ```python
      def enhance_track_metadata(partial_info: dict) -> TrackMetadata:
          # Given: filename, duration, maybe partial artist
          # Returns: likely genre, year range, related artists
      ```
      
5.  **Batch Processing:**
    * Process in configurable batch sizes to control API costs
    * Save progress after each batch (checkpoint system)
    * Resume capability for interrupted runs
    
6.  **Cost Tracking:**
    * Log estimated token usage and costs
    * Daily/monthly limits configurable in .env
    * Warning when approaching limits
