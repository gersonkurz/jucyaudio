# JucyAudio - Library Organization & Auto-Sorting

## Goal
Solve the "Download Folder Chaos" by providing a robust, automated way to sort, rename, and organize music files based on their metadata.

## 1. Problem Statement
Users often dump files into a generic `Downloads/` or `Unsorted/` folder. Manually moving them to `Artist/Album/` structures is tedious.
Existing tools (Picard, Beets) are powerful but external; integrating this logic allows for seamless "Import & Sort" workflows.

## 2. Architecture: The "Organizer" Module

This feature will be a new C++ subsystem in JucyAudio, leveraging the metadata already in our database (or enriched via the AI tool).

### 2.1 Core Components
1.  **Rule Engine**: A string interpolation engine to generate target paths.
2.  **File Mover**: A safe, transactional file operation manager (Copy/Move with Rollback).
3.  **Database Sync**: Ensures the SQLite database tracks the file to its new location without losing play history/cues.

### 2.2 Configuration (Settings)

**Target Root**: User selects the destination (e.g., `D:/Music/Library`).

**Naming Pattern**:
Users can define patterns using variables:
*   `%artist%`, `%album%`, `%title%`, `%year%`, `%genre%`, `%bpm%`, `%key%`
*   **Conditionals**: `{if %year%}(%year%){endif}`
*   **Sanitization**: Auto-replace illegal chars (`:`, `/`, `?`) with `_`.

**Example Patterns**:
*   *Genre-based*: `%genre%/%artist%/[%year%] %album%/%track% - %title%`
*   *Flat*: `%artist% - %title%`
*   *BPM-based*: `Sets/%bpm% bpm/%artist% - %title%`

**Action Mode**:
*   **Move**: Original file is gone (Default for cleanup).
*   **Copy**: Safer, leaves original in Downloads.
*   **Hard Link**: Saves space (Advanced users, same filesystem only).

## 3. Workflow

1.  **Select Source**: User selects a folder (e.g., "Downloads") in the Library View.
2.  **Preview**:
    *   System calculates target paths for all selected files.
    *   Shows a "Before -> After" list.
    *   Highlights conflicts (e.g., file already exists).
3.  **Execute**:
    *   Files are moved/renamed.
    *   Empty source folders are optionally deleted.
    *   Database `Tracks` table is updated with new `file_path`.
    *   **Crucial**: Cue points, Loops, and Mix references are preserved because we update the DB path, not delete/re-import.

## 4. Integration with AI Enrichment (`enrich.md`)

This feature pairs perfectly with the AI Enrichment tool.
*   **Step 1**: Run AI Enrichment to fix Genres/Artists in the "Unsorted" folder.
*   **Step 2**: Run Library Organizer to physically move files to `Techno/Artist/Album/`.

## 5. Technical Implementation Steps

### Phase 1: Path Generator
*   Implement `PatternEvaluator` class in C++.
*   Unit tests for variable replacement and path sanitization.

### Phase 2: The "Move" Transaction
*   Implement `FileMover` class.
*   Logic:
    1.  Check source exists.
    2.  Check destination collision (Auto-rename `file (1).mp3`?).
    3.  Copy file.
    4.  Verify hash (optional).
    5.  Update Database.
    6.  Delete source.
*   **Rollback**: If DB update fails, delete destination.

### Phase 3: UI
*   `OrganizeDialog`:
    *   Pattern editor with live preview.
    *   List view of pending changes.
    *   "Go" button with progress bar.

## 6. Risks
*   **Data Loss**: Moving files is dangerous.
    *   *Mitigation*: Default to "Copy" initially? Or distinct "Safe Mode".
*   **Database Desync**: If the file moves but DB update fails.
    *   *Mitigation*: Wrap in a transaction.

## 7. Future Enhancements
*   **Auto-Import**: Watch a "Drop Folder", auto-tag, auto-move.
*   **Deduping**: Integrate with `dedupe.md` logic during organization.

# Codex Comments
- For cross-volume moves, a rename is not atomic; specify how rollback works if copy succeeds but DB update fails.
- Hard links only work on the same filesystem; the UI should surface this constraint clearly.
- Add a conflict policy (skip/overwrite/rename) for destination collisions in the preview step.
