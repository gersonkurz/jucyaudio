# Database & Code Cleanup TODOs

Identified during 2.0 development review. These are non-urgent but should be addressed for cleanliness and minor performance gains.

## Database Schema Cleanup

### High Priority: Drop Unused Indexes

These indexes are maintained on every write but provide zero value (columns are 0% populated):

```sql
DROP INDEX IF EXISTS idx_tracks_rating;
DROP INDEX IF EXISTS idx_tracks_liked_status;
```

**Impact**: Improves INSERT/UPDATE performance on Tracks table.

### Medium Priority: Drop Abandoned Tables

**MixUndoHistory** — Created for persistent undo but never implemented. Undo works in-memory via `UndoManager` class.

```sql
DROP TABLE IF EXISTS MixUndoHistory;
```

**Evidence**: 996 rows exist but zero SQL operations reference this table in codebase.

### Low Priority: Unused Columns (Keep, Mark Deprecated)

These columns in `Tracks` are 0% populated. Dropping would require full table rebuild on 1.4M rows — not worth the risk.

**Will be used in 2.0 — DO NOT REMOVE:**
| Column | Future Use |
|--------|------------|
| `intro_end` | Smart Automix — intro zone marker |
| `outro_start` | Smart Automix — outro zone marker |
| `beat_locations_json` | Smart Automix — energy contour / analysis data |
| `internal_content_hash` | Dedupe feature (2.0 roadmap) |
| `key_string` | Potential future harmonic mixing |

**Truly dead columns:**
| Column | Notes |
|--------|-------|
| `rating` | User rating feature, never implemented |
| `liked_status` | Like/dislike feature, never implemented |
| `user_notes` | User notes feature, never implemented |

**Now in use (2026-06-07):**
| Column | Notes |
|--------|-------|
| `play_count` | Incremented on playback (commit 16012ed) — no longer dead |
| `last_played` | Timestamped on playback (commit 16012ed) — no longer dead |

> The `idx_tracks_rating` / `idx_tracks_liked_status` index drops below are still valid, but re-check
> populated columns against the live DB before running any migration — this doc was last verified
> against schema v21 in Feb 2026.

**Action**: Keep all columns for now. The truly dead ones are NULL/0 and cost minimal space.

### Done: explicit column lists for the positional decoders

`trackInfoFromStatement()` used to be fed `SELECT * FROM Tracks`, and `mixTrackFromStatement()`
`SELECT * FROM MixTracks`. Both walk the row with a running index, so the field each value landed in
was decided by the order the table happened to declare its columns in - and a database created from
scratch does not declare them in the same order as one that migrated up, because
`ALTER TABLE ADD COLUMN` appends. Every such query now names its columns, from
`trackColumnsForDecoding` and `mixTrackColumnsForDecoding`, each sitting beside the decoder it feeds.

Not done, and still only worth doing if something measures a reason to: removing the dead fields from
`TrackInfo`, and reading by column name rather than by index. The column list makes the order the
query's business, which is what the robustness item was really after.

**Benefit realised**: correctness, not speed. NULL columns were always cheap to transfer.

### Low Priority: Unused Column in Mixes

`Mixes.undo_stack_position` — Part of abandoned persistent undo feature.

```sql
-- Cannot easily drop column in SQLite; mark as deprecated
-- ALTER TABLE Mixes DROP COLUMN undo_stack_position; -- Future cleanup
```

### Albums Table — DO NOT REMOVE

Columns `genres`, `moods`, `tags`, `bandcamp_url`, `bitrate` are 0% populated but the table will be repurposed for Library Organizer feature.

## Migration Script (When Ready)

```sql
-- Run these in a transaction
BEGIN TRANSACTION;

-- Drop unused indexes (safe, improves write performance)
DROP INDEX IF EXISTS idx_tracks_rating;
DROP INDEX IF EXISTS idx_tracks_liked_status;

-- Drop abandoned table
DROP TABLE IF EXISTS MixUndoHistory;

COMMIT;

-- Run VACUUM separately (cannot be in transaction)
-- WARNING: Temporarily requires ~2x database size on disk
VACUUM;
```

## Code Cleanup

### UndoManager

The `MixUndoHistory` table was planned for persistent undo across sessions. Current `UndoManager` is in-memory only. Either:
1. Remove the table (done above) and document that undo is session-only
2. Future: Implement persistent undo using the table

### Deprecated Column References

Search codebase for any references to these columns and mark as deprecated:
- `key_string`
- `rating`
- `liked_status`
- `play_count`
- `internal_content_hash`
- `user_notes`

## Smart Automix: Column Reuse

The following columns can be **repurposed** for Smart Automix (no schema change needed):

| Existing Column | New Purpose |
|-----------------|-------------|
| `intro_end` | Intro zone end (ms) — already INTEGER |
| `outro_start` | Outro zone start (ms) — already INTEGER |
| `beat_locations_json` | Analysis data JSON (energy contour, phrase boundaries, version) |

These are currently 0-1% populated, so repurposing is safe.

---

*Last updated: 2026-06-07 (play_count/last_played reclassified as in-use)*
*Database analyzed: 1,437,558 tracks, schema v21 (Feb 2026 — re-verify before migrating)*
