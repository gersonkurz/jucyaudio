#include <Database/Includes/ITagManager.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/EQPreset.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteStatementConstruction.h>
#include <Database/Sqlite/SqliteTrackDatabase.h>
#include <Database/Sqlite/SqliteMixSummary.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/Sqlite/SqliteAlbumManager.h>
#include <Utils/AssortedUtils.h>
#include <Utils/StringWriter.h>
#include <algorithm> // For std::reverse
#include <cassert>   // For assert
#include <cctype>    // For ::isdigit
#include <cstring>  // For std::memcmp/std::memcpy
#include <format>   // For the per-column album merge statements in the v31 migration
#include <ranges>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace
{
    using namespace jucyaudio;
    using namespace database;

    /// @brief True when a cached waveform blob is a thumbnail that decoded nothing.
    ///
    /// juce::AudioThumbnail reports isFullyLoaded() as true for a source it could not read -
    /// the test is numSamplesFinished >= totalSamples - samplesPerThumbSample, and 0 >= -2048 -
    /// so an empty thumbnail gets serialised and stored as though it were a real waveform. The
    /// result is a valid 52-byte header with no samples in it, which every caller then treats as
    /// a cache hit and never regenerates. That is how a track whose file was missing keeps a
    /// blank waveform after the file comes back.
    ///
    /// Read as bytes rather than through juce::AudioThumbnail: this layer has no JUCE dependency,
    /// and the header is a fixed little-endian layout - "jatm", samplesPerThumbSample as int32,
    /// then totalSamples as int64.
    bool isEmptyWaveformBlob(const std::vector<unsigned char> &blob)
    {
        static constexpr size_t headerSize = 16;
        static constexpr size_t totalSamplesOffset = 8;

        if (blob.size() < headerSize || std::memcmp(blob.data(), "jatm", 4) != 0)
        {
            // Not something this function understands. Left to the caller, which has always had to
            // cope with a blob juce::AudioThumbnail::loadFrom refuses.
            return false;
        }

        int64_t totalSamples = 0;
        std::memcpy(&totalSamples, blob.data() + totalSamplesOffset, sizeof(totalSamples));
        return totalSamples <= 0;
    }

    // Array of initial SQL statements for schema creation
    const char *maintenanceSqlStatements[] = {
        "PRAGMA optimize;",  // Optimize query planner statistics
        
        // FTS5 maintenance - rebuild the full-text search index
        // This ensures the index is completely accurate and up-to-date
        "INSERT INTO TracksSearchFTS(TracksSearchFTS) VALUES('rebuild');",
        
        // FTS5 maintenance - optimize the full-text search index
        // This merges b-tree structures in the FTS index for better performance
        "INSERT INTO TracksSearchFTS(TracksSearchFTS) VALUES('optimize');",
        
        "VACUUM;"  // Reclaim unused space and defragment the database file
    };

    // --- THIS IS THE FINAL, CORRECTED SCHEMA FOR A NEW DATABASE ---
    /**
     * @brief Initial genre vocabulary for the Albums.genres headline genre.
     *
     * Seeded from the top-level folder names under D:\MP3\Resorted - the curated, current-taste
     * vocabulary (ghetto-tech folded into ghettotech). The user can add to it from the UI; this is
     * only the starting point, deliberately not the union with the older D:\MP3\Tracks folder names.
     */
    const char *defaultGenreVocabulary[] = {
        "acid house",
        "acid techno",
        "alternative",
        "ambient",
        "atmospheric drum & bass",
        "blackgaze",
        "bleep techno",
        "braindance",
        "breakcore",
        "chillout",
        "coldwave",
        "crust punk",
        "dance",
        "dark ambient",
        "dark folk",
        "dark rock",
        "dark techno",
        "darkwave",
        "detroit techno",
        "deutsch punk",
        "dream pop",
        "dream punk",
        "drone rock",
        "dub",
        "ebm",
        "ebm oldskool",
        "electro",
        "electro oldschool",
        "electronic",
        "experimental",
        "folk-rock",
        "french",
        "french hip-hop",
        "funk",
        "garage rock",
        "ghettotech",
        "gothic",
        "grindcore",
        "hard rock",
        "hard techno",
        "heavy psych",
        "idm",
        "indie dance",
        "indie folk",
        "indie pop",
        "indie rock",
        "industrial",
        "krautrock",
        "martial",
        "metal",
        "minimal techno",
        "modern classical",
        "new wave",
        "noise pop",
        "noise rock",
        "oldschool acid",
        "oldschool breakbeat",
        "oldschool eurodance",
        "oldschool hiphop",
        "oldschool rave",
        "oldschool techno",
        "oldschool trance",
        "post-hardcore",
        "post-metal",
        "post-punk",
        "post-rock",
        "power electronics",
        "psychedelia",
        "psychedelic rock",
        "psychedelic trance",
        "punk rock",
        "rock",
        "sadcore",
        "screamo",
        "shoegaze",
        "slowcore",
        "sludge",
        "space rock",
        "stoner rock",
        "synth-punk",
        "synthpop",
        "synthwave",
        "tech house",
        "techno",
        "trance",
        "uk bass",
        "uk trance",
        "vaporwave",
        "witchhouse",
        "witchtrap",
    };

    /// @brief Collapses duplicate Tracks rows sharing (folder_id, filename) onto the lowest track_id,
    ///        remapping every reference, and puts UNIQUE(folder_id, filename) back afterwards.
    ///
    /// Written for the v24 migration and reused by v31, which merges duplicate folder rows and so
    /// moves tracks into a folder that may already hold a row for the same filename. The index has to
    /// come off before that move and go back on after this, which is the last two steps here.
    ///
    /// Only run it when there is something to collapse: the final step rebuilds the whole FTS index.
    const char *trackDedupeSteps[] = {
        "CREATE TEMP TABLE _dup_map AS "
        "SELECT t.track_id AS old_id, m.canonical_id FROM Tracks t "
        "JOIN (SELECT folder_id, filename, MIN(track_id) AS canonical_id FROM Tracks "
        "GROUP BY folder_id, filename HAVING COUNT(*) > 1) m "
        "ON t.folder_id = m.folder_id AND t.filename = m.filename WHERE t.track_id <> m.canonical_id;",
        "CREATE INDEX _dup_map_idx ON _dup_map(old_id);",
        "UPDATE MixTracks SET track_id=(SELECT canonical_id FROM _dup_map WHERE old_id=MixTracks.track_id) "
        "WHERE track_id IN (SELECT old_id FROM _dup_map);",
        "UPDATE TrackMarkers SET track_id=(SELECT canonical_id FROM _dup_map WHERE old_id=TrackMarkers.track_id) "
        "WHERE track_id IN (SELECT old_id FROM _dup_map);",
        "INSERT OR IGNORE INTO WorkingSetTracks(ws_id, track_id) "
        "SELECT ws_id,(SELECT canonical_id FROM _dup_map WHERE old_id=w.track_id) FROM WorkingSetTracks w "
        "WHERE w.track_id IN (SELECT old_id FROM _dup_map);",
        "DELETE FROM WorkingSetTracks WHERE track_id IN (SELECT old_id FROM _dup_map);",
        "INSERT OR IGNORE INTO TrackTags(track_id, tag_id) "
        "SELECT (SELECT canonical_id FROM _dup_map WHERE old_id=g.track_id),tag_id FROM TrackTags g "
        "WHERE g.track_id IN (SELECT old_id FROM _dup_map);",
        "DELETE FROM TrackTags WHERE track_id IN (SELECT old_id FROM _dup_map);",
        "DELETE FROM WaveformCache WHERE track_id IN (SELECT old_id FROM _dup_map);",
        "DELETE FROM Tracks WHERE track_id IN (SELECT old_id FROM _dup_map);",
        "DROP TABLE _dup_map;",
        "DROP INDEX IF EXISTS idx_tracks_parent_filename;",
        "CREATE UNIQUE INDEX idx_tracks_parent_filename ON Tracks(folder_id, filename);",
        "INSERT INTO TracksSearchFTS(TracksSearchFTS) VALUES('rebuild');",
    };

    /// @brief The objects that used to exist only inside a migration rung.
    ///
    /// TrackMarkers (v4), idx_tracks_status (v5), the search tables and their triggers (v12, v25),
    /// MixMarkers (v16), EQPresets (v17) and ReverbPresets (v18) were each added to the ladder and
    /// never to the schema a new database is built from. A new database is stamped at the latest
    /// version and skips the ladder entirely, so every library created since has been missing all of
    /// them - and full-text search, track markers, mix markers and the EQ/reverb presets do not
    /// degrade without their tables, they fail.
    ///
    /// One array, run from two places: the new-database path in createTablesIfNeeded, and the v32 rung
    /// that repairs the databases already created without them. The same statements, not merely
    /// equivalent ones, so a fresh database and a repaired one hold byte-identical definitions of
    /// these objects by construction rather than by two texts being kept in step by hand.
    ///
    /// Note what that does not buy. The migration self test checks that the v32 rung leaves an
    /// already-complete database untouched; it is not a whole-ladder convergence check and cannot see
    /// structural differences between v4 and v31 - the v6 primary key on MixTracks is one it missed.
    /// A frozen old-schema fixture is what that needs, and is an open task.
    ///
    /// Everything here is IF NOT EXISTS, because the repair runs against databases that have some of
    /// it (anything that migrated up through the ladder) and databases that have none of it.
    const char *convergenceSqlStatements[] = {
        R"SQL(
        CREATE TABLE IF NOT EXISTS TrackMarkers (
            marker_id  INTEGER PRIMARY KEY AUTOINCREMENT,
            track_id   INTEGER NOT NULL,
            position_ms INTEGER NOT NULL,
            comment    TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            color      TEXT,
            emoji      TEXT,
            FOREIGN KEY (track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_trackmarkers_track_id ON TrackMarkers (track_id);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_status ON Tracks (status);",
        R"SQL(
        CREATE TABLE IF NOT EXISTS MixMarkers (
            marker_id  INTEGER PRIMARY KEY AUTOINCREMENT,
            mix_id     INTEGER NOT NULL,
            position_ms INTEGER NOT NULL,
            comment    TEXT NOT NULL,
            color      TEXT,
            emoji      TEXT,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            FOREIGN KEY (mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_mix_markers_mix_id ON MixMarkers(mix_id);",
        "CREATE INDEX IF NOT EXISTS idx_mix_markers_position ON MixMarkers(mix_id, position_ms);",
        R"SQL(
        CREATE TABLE IF NOT EXISTS EQPresets (
            preset_id     INTEGER PRIMARY KEY,
            name          TEXT NOT NULL UNIQUE,
            is_deletable  INTEGER NOT NULL DEFAULT 1,
            settings_json TEXT NOT NULL
        );)SQL",
        R"SQL(
        CREATE TABLE IF NOT EXISTS ReverbPresets (
            preset_id     INTEGER PRIMARY KEY,
            name          TEXT NOT NULL UNIQUE,
            is_deletable  INTEGER NOT NULL DEFAULT 1,
            settings_json TEXT NOT NULL,
            created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );)SQL",
        // The search content table, then the FTS index over it, then the triggers - in that order,
        // because CREATE TRIGGER checks that the table it fires on exists.
        R"SQL(
        CREATE TABLE IF NOT EXISTS TracksSearchData (
            track_id       INTEGER PRIMARY KEY,
            search_content TEXT NOT NULL,
            FOREIGN KEY (track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
        );)SQL",
        R"SQL(
        CREATE VIRTUAL TABLE IF NOT EXISTS TracksSearchFTS USING fts5(
            search_content,
            content='TracksSearchData',
            content_rowid='track_id',
            tokenize='unicode61'
        );)SQL",
        R"SQL(
        CREATE TRIGGER IF NOT EXISTS tracks_search_insert
        AFTER INSERT ON Tracks
        BEGIN
            INSERT INTO TracksSearchData (track_id, search_content)
            SELECT
                NEW.track_id,
                COALESCE(NEW.title, '') || ' ' ||
                COALESCE(NEW.artist_name, '') || ' ' ||
                COALESCE(NEW.album_title, '') || ' ' ||
                COALESCE(NEW.filename, '') || ' ' ||
                COALESCE((SELECT root_path FROM Folders WHERE folder_id = NEW.folder_id), '') || ' ' ||
                COALESCE((SELECT name FROM Folders WHERE folder_id = NEW.folder_id), '') || ' ' ||
                COALESCE(
                    (SELECT GROUP_CONCAT(Tags.name, ' ')
                     FROM TrackTags tt
                     JOIN Tags ON tt.tag_id = Tags.tag_id
                     WHERE tt.track_id = NEW.track_id),
                    ''
                );
        END;)SQL",
        R"SQL(
        CREATE TRIGGER IF NOT EXISTS tracks_search_update
        AFTER UPDATE OF title, artist_name, album_title, filename, folder_id ON Tracks
        BEGIN
            UPDATE TracksSearchData
            SET search_content = (
                SELECT
                    COALESCE(NEW.title, '') || ' ' ||
                    COALESCE(NEW.artist_name, '') || ' ' ||
                    COALESCE(NEW.album_title, '') || ' ' ||
                    COALESCE(NEW.filename, '') || ' ' ||
                    COALESCE((SELECT root_path FROM Folders WHERE folder_id = NEW.folder_id), '') || ' ' ||
                    COALESCE((SELECT name FROM Folders WHERE folder_id = NEW.folder_id), '') || ' ' ||
                    COALESCE(
                        (SELECT GROUP_CONCAT(Tags.name, ' ')
                         FROM TrackTags tt
                         JOIN Tags ON tt.tag_id = Tags.tag_id
                         WHERE tt.track_id = NEW.track_id),
                        ''
                    )
            )
            WHERE track_id = NEW.track_id;
        END;)SQL",
        R"SQL(
        CREATE TRIGGER IF NOT EXISTS tracks_search_delete
        AFTER DELETE ON Tracks
        BEGIN
            DELETE FROM TracksSearchData WHERE track_id = OLD.track_id;
        END;)SQL",
        R"SQL(
        CREATE TRIGGER IF NOT EXISTS tracktags_search_insert
        AFTER INSERT ON TrackTags
        BEGIN
            UPDATE TracksSearchData
            SET search_content = (
                SELECT
                    COALESCE(t.title, '') || ' ' ||
                    COALESCE(t.artist_name, '') || ' ' ||
                    COALESCE(t.album_title, '') || ' ' ||
                    COALESCE(t.filename, '') || ' ' ||
                    COALESCE(f.root_path, '') || ' ' ||
                    COALESCE(f.name, '') || ' ' ||
                    COALESCE(
                        (SELECT GROUP_CONCAT(Tags.name, ' ')
                         FROM TrackTags tt
                         JOIN Tags ON tt.tag_id = Tags.tag_id
                         WHERE tt.track_id = NEW.track_id),
                        ''
                    )
                FROM Tracks t
                LEFT JOIN Folders f ON t.folder_id = f.folder_id
                WHERE t.track_id = NEW.track_id
            )
            WHERE track_id = NEW.track_id;
        END;)SQL",
        R"SQL(
        CREATE TRIGGER IF NOT EXISTS tracktags_search_delete
        AFTER DELETE ON TrackTags
        BEGIN
            UPDATE TracksSearchData
            SET search_content = (
                SELECT
                    COALESCE(t.title, '') || ' ' ||
                    COALESCE(t.artist_name, '') || ' ' ||
                    COALESCE(t.album_title, '') || ' ' ||
                    COALESCE(t.filename, '') || ' ' ||
                    COALESCE(f.root_path, '') || ' ' ||
                    COALESCE(f.name, '') || ' ' ||
                    COALESCE(
                        (SELECT GROUP_CONCAT(Tags.name, ' ')
                         FROM TrackTags tt
                         JOIN Tags ON tt.tag_id = Tags.tag_id
                         WHERE tt.track_id = OLD.track_id),
                        ''
                    )
                FROM Tracks t
                LEFT JOIN Folders f ON t.folder_id = f.folder_id
                WHERE t.track_id = OLD.track_id
            )
            WHERE track_id = OLD.track_id;
        END;)SQL",
        // TracksSearchData maintains the content table; these keep the external-content FTS index in
        // step with it. Without them nothing added by a scan is ever searchable - see the v25 rung.
        R"SQL(CREATE TRIGGER IF NOT EXISTS tracksdata_fts_ai AFTER INSERT ON TracksSearchData BEGIN
                            INSERT INTO TracksSearchFTS(rowid, search_content) VALUES (new.track_id, new.search_content);
                        END;)SQL",
        R"SQL(CREATE TRIGGER IF NOT EXISTS tracksdata_fts_ad AFTER DELETE ON TracksSearchData BEGIN
                            INSERT INTO TracksSearchFTS(TracksSearchFTS, rowid, search_content) VALUES ('delete', old.track_id, old.search_content);
                        END;)SQL",
        R"SQL(CREATE TRIGGER IF NOT EXISTS tracksdata_fts_au AFTER UPDATE ON TracksSearchData BEGIN
                            INSERT INTO TracksSearchFTS(TracksSearchFTS, rowid, search_content) VALUES ('delete', old.track_id, old.search_content);
                            INSERT INTO TracksSearchFTS(rowid, search_content) VALUES (new.track_id, new.search_content);
                        END;)SQL",
    };

    const char *initialSqlStatements[] = {
        "PRAGMA foreign_keys = ON;",
        R"SQL(
        CREATE TABLE IF NOT EXISTS Folders (
            folder_id   INTEGER PRIMARY KEY,
            parent_id   INTEGER,
            name        TEXT NOT NULL,
            root_path   TEXT,
            track_count INTEGER,
            actual_path TEXT,
            FOREIGN KEY (parent_id) REFERENCES Folders(folder_id) ON DELETE CASCADE
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_folders_parent_name ON Folders(parent_id, name);",
        // UNIQUE so one directory is one Folders row. Nothing used to say that, and the cost of a
        // second row for a path that already has one does not stay small: buildCacheIfNeeded refuses
        // to finish a cache holding two rows for one path, so from the first duplicate onwards the
        // cache can never be rebuilt, every lookup misses, and every folder touched after that gets
        // another row of its own.
        //
        // NULL is left unconstrained, which SQLite gives for free by treating NULLs as distinct:
        // rows old enough to have no computed path exist, and buildCacheIfNeeded fills them in. It
        // detects a collision between two of them itself, before it writes either, so such a pair
        // never reaches this index. An empty string would collide where NULL does not, which is why
        // the v31 migration turns the blanks into NULLs and addFolder refuses an empty path.
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_folders_root_path ON Folders(root_path);",
        R"SQL(
        CREATE TABLE IF NOT EXISTS Albums (
            album_id INTEGER PRIMARY KEY AUTOINCREMENT,
            album_artist TEXT,
            title TEXT NOT NULL,
            year INTEGER,
            folder_id INTEGER NOT NULL,
            genres TEXT,
            moods TEXT,
            tags TEXT,
            bandcamp_url TEXT,
            bitrate INTEGER,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (folder_id) REFERENCES Folders(folder_id) ON DELETE CASCADE
        );)SQL",
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_albums_title_folder ON Albums(title, folder_id);",
        R"SQL(
        CREATE TABLE IF NOT EXISTS Tracks (
            track_id            INTEGER PRIMARY KEY,
            folder_id           INTEGER NOT NULL,
            filename            TEXT NOT NULL,
            last_modified_fs    INTEGER,
            filesize_bytes      INTEGER,
            date_added          INTEGER,
            last_scanned        INTEGER,
            title               TEXT,
            artist_name         TEXT,
            album_title         TEXT,
            album_artist_name   TEXT,
            track_number        INTEGER,
            disc_number         INTEGER,
            year                INTEGER,
            duration            INTEGER,
            samplerate          INTEGER,
            channels            INTEGER,
            bitrate             INTEGER,
            codec_name          TEXT,
            bpm                 INTEGER,
            intro_end           INTEGER,
            outro_start         INTEGER,
            key_string          TEXT,
            beat_locations_json TEXT,
            rating              INTEGER DEFAULT 0,
            liked_status        INTEGER DEFAULT 0,
            play_count          INTEGER DEFAULT 0,
            last_played         INTEGER,
            internal_content_hash TEXT,
            user_notes          TEXT,
            is_missing          INTEGER DEFAULT 0,
            status              TEXT NOT NULL DEFAULT 'unknown',
            album_id            INTEGER,
            FOREIGN KEY (folder_id) REFERENCES Folders(folder_id) ON DELETE CASCADE,
            FOREIGN KEY (album_id) REFERENCES Albums(album_id) ON DELETE SET NULL
        );)SQL",
        // UNIQUE so one physical file is one track row (overlapping roots / re-scans can't duplicate).
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_tracks_parent_filename ON Tracks(folder_id, filename);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_artist ON Tracks (artist_name COLLATE NOCASE);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_album ON Tracks (album_title COLLATE NOCASE);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_title ON Tracks (title COLLATE NOCASE);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_bpm ON Tracks (bpm);",
        // Note: idx_tracks_rating and idx_tracks_liked_status removed in v22 (unused)
        "CREATE INDEX IF NOT EXISTS idx_tracks_album_id ON Tracks(album_id);",
        R"SQL(CREATE TABLE IF NOT EXISTS Tags (tag_id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE);)SQL",
        R"SQL(
        CREATE TABLE IF NOT EXISTS TrackTags (
            track_id INTEGER NOT NULL, tag_id INTEGER NOT NULL, PRIMARY KEY (track_id, tag_id),
            FOREIGN KEY (track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE,
            FOREIGN KEY (tag_id) REFERENCES Tags(tag_id) ON DELETE CASCADE
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_tracktags_tag_id ON TrackTags(tag_id);",
        R"SQL(CREATE TABLE IF NOT EXISTS Genres (genre_id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE);)SQL",
        R"SQL(CREATE TABLE IF NOT EXISTS SchemaInfo (key TEXT PRIMARY KEY, value TEXT);)SQL",
        R"SQL(
        CREATE TABLE IF NOT EXISTS WorkingSets(
            ws_id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE,
            timestamp INTEGER, sort_order TEXT, next_mix_number INTEGER DEFAULT 1
        );)SQL",
        R"SQL(
        CREATE TABLE IF NOT EXISTS WorkingSetTracks(
            ws_id INTEGER NOT NULL, track_id INTEGER NOT NULL, PRIMARY KEY(ws_id, track_id),
            FOREIGN KEY(ws_id) REFERENCES WorkingSets(ws_id) ON DELETE CASCADE,
            FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
        );)SQL",
        R"SQL(
        CREATE TABLE IF NOT EXISTS Mixes(
            mix_id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE, timestamp INTEGER,
            track_count INTEGER, total_length INTEGER, source_ws_id INTEGER, status TEXT DEFAULT 'New',
            undo_stack_position INTEGER DEFAULT 0,
            exported_at INTEGER,
            export_folder TEXT,
            pending_export_settings TEXT,
            FOREIGN KEY(source_ws_id) REFERENCES WorkingSets(ws_id)
        );)SQL",
        R"SQL(
        CREATE TABLE IF NOT EXISTS MixTracks(
            mix_id INTEGER NOT NULL, 
            track_id INTEGER NOT NULL, 
            order_in_mix INTEGER NOT NULL,
            mix_data TEXT NOT NULL,
            FOREIGN KEY(mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE,
            FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_mixtracks_mix_order ON MixTracks(mix_id, order_in_mix);",
        "CREATE INDEX IF NOT EXISTS idx_mixtracks_track ON MixTracks(track_id);",

        // What a mix contained when it was exported, kept so it survives the loss of what it describes.
        //
        // Note the deliberate asymmetry against MixTracks above: mix_id carries a cascading foreign key,
        // track_id carries none at all.
        //
        // A track_id foreign key would cascade exactly when a track is deleted, destroying the rows whose
        // whole purpose is to outlive that deletion - which is how mixes silently lost tracks in the first
        // place. Here track_id is data, not a reference: a record of which id this used to be. It cannot be
        // trusted as identity either, because Tracks.track_id is INTEGER PRIMARY KEY without AUTOINCREMENT
        // and SQLite reuses the highest deleted rowid.
        //
        // mix_id is different. Deleting a track leaves the Mixes row alone, so this key cannot fire in the
        // case being protected against; it fires only when the mix itself is deliberately deleted, which is
        // what we want, and it closes the same id-reuse hole structurally rather than by convention.
        R"SQL(
        CREATE TABLE IF NOT EXISTS MixRecovery(
            mix_id          INTEGER NOT NULL,
            order_in_mix    INTEGER NOT NULL,
            captured_at     INTEGER NOT NULL,
            mix_name        TEXT NOT NULL,
            total_duration  INTEGER,
            track_id        INTEGER,
            artist_name     TEXT,
            album_title     TEXT,
            title           TEXT,
            filename        TEXT,
            folder_path     TEXT,
            duration        INTEGER,
            filesize_bytes  INTEGER,
            bpm             INTEGER,
            mix_data        TEXT,
            -- 0 when the mix was already missing rows when this was captured, so the record
            -- describes what survived rather than the whole mix. Defaults to 1: every row
            -- written before this column existed came from a capture that refused anything
            -- incomplete, so they are all whole by construction.
            is_complete     INTEGER NOT NULL DEFAULT 1,
            -- Where the mix said the track was, as opposed to where it sits in this record. The
            -- two differ only for a damaged mix, where the jumps are the evidence of what was
            -- lost. Nullable: rows written before this column existed do not know, though the
            -- v30 migration fills it in for them, since order_in_mix was the source value then.
            source_order_in_mix INTEGER,
            PRIMARY KEY (mix_id, order_in_mix),
            FOREIGN KEY (mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_mixrecovery_track ON MixRecovery(track_id);",
        // filename + filesize is the fingerprint for recognising a file that moved: a folder reorganisation
        // moves folders without renaming files, and internal_content_hash is empty for every track in the
        // library, so there is no real hash to match on. A candidate key, never proof on its own.
        "CREATE INDEX IF NOT EXISTS idx_mixrecovery_fileident ON MixRecovery(filename, filesize_bytes);",
        // Note: MixUndoHistory table removed in v22 (was never used - undo is in-memory)
        R"SQL(
        CREATE TABLE IF NOT EXISTS ExportFolders (
            folder_id INTEGER PRIMARY KEY,
            name TEXT NOT NULL UNIQUE COLLATE NOCASE,
            display_order INTEGER,
            created_at INTEGER NOT NULL,
            description TEXT
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_export_folders_order ON ExportFolders(display_order);",
        "CREATE TABLE IF NOT EXISTS LibraryRoots (root_id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, file_count INTEGER DEFAULT 0, last_scanned INTEGER);",
        R"SQL(
        CREATE TABLE IF NOT EXISTS WaveformCache (
            track_id INTEGER PRIMARY KEY NOT NULL,
            waveform_blob BLOB NOT NULL,
            FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
        );)SQL",
        R"SQL(
        CREATE TABLE IF NOT EXISTS MasterChainPlugins (
            order_index INTEGER PRIMARY KEY,
            plugin_format TEXT NOT NULL,
            identifier TEXT NOT NULL,
            name TEXT,
            manufacturer TEXT,
            version TEXT,
            is_enabled INTEGER NOT NULL DEFAULT 1,
            state_blob BLOB
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_masterchain_identifier ON MasterChainPlugins(plugin_format, identifier);",
    };

    TrackInfo trackInfoFromStatement(const SqliteStatement &stmt)
    {
        TrackInfo info{};
        int col = 0;
        info.trackId = stmt.getInt64(col++);
        info.folderId = stmt.getInt64(col++);
        if (!stmt.isNull(col))
            info.filename = stmt.getText(col);
        col++;
        info.last_modified_fs = timestampFromInt64(stmt.getInt64(col++));
        info.filesize_bytes = static_cast<std::uintmax_t>(stmt.getInt64(col++));
        info.date_added = timestampFromInt64(stmt.getInt64(col++));
        info.last_scanned = timestampFromInt64(stmt.getInt64(col++));
        if (!stmt.isNull(col))
            info.title = stmt.getText(col);
        col++;
        if (!stmt.isNull(col))
            info.artist_name = stmt.getText(col);
        col++;
        if (!stmt.isNull(col))
            info.album_title = stmt.getText(col);
        col++;
        if (!stmt.isNull(col))
            info.album_artist_name = stmt.getText(col);
        col++;
        info.track_number = stmt.getInt32(col++);
        info.disc_number = stmt.getInt32(col++);
        info.year = stmt.getInt32(col++);
        info.duration = durationFromInt64(stmt.getInt64(col++));
        info.samplerate = stmt.getInt32(col++);
        info.channels = stmt.getInt32(col++);
        info.bitrate = stmt.getInt32(col++);
        if (!stmt.isNull(col))
            info.codec_name = stmt.getText(col);
        ++col;
        if (stmt.isNull(col))
            info.bpm = std::nullopt;
        else
            info.bpm = stmt.getInt32(col);
        ++col;
        if (stmt.isNull(col))
            info.intro_end = std::nullopt;
        else
            info.intro_end = durationFromInt64(stmt.getInt64(col));
        ++col;
        if (stmt.isNull(col))
            info.outro_start = std::nullopt;
        else
            info.outro_start = durationFromInt64(stmt.getInt64(col));
        ++col;
        if (!stmt.isNull(col))
            info.key_string = stmt.getText(col);
        col++;
        if (!stmt.isNull(col))
            info.beat_locations_json = stmt.getText(col);
        col++;
        col++; // Skip rating (unused)
        col++; // Skip liked_status (unused)
        col++; // Skip play_count (unused)
        info.last_played = timestampFromInt64(stmt.getInt64(col++));
        if (!stmt.isNull(col))
            info.internal_content_hash = stmt.getText(col);
        col++;
        col++; // Skip user_notes (unused)
        info.is_missing = stmt.getInt32(col++) != 0;

        // Read status field
        if (!stmt.isNull(col))
        {
            const auto statusStr = stmt.getText(col);
            if (statusStr == "ok")
                info.status = TrackStatus::Ok;
            else if (statusStr == "bad_format")
                info.status = TrackStatus::BadFormat;
            else
            {
                assert(info.status == TrackStatus::Unknown);
            }
                
        }
        col++;

        return info;
    }

    bool bindTrackInfoToStatement(SqliteStatement &stmt, const TrackInfo &info, bool forUpdate = false)
    {
        bool ok = true;
        ok &= stmt.addParam(info.folderId);
        ok &= stmt.addParam(info.filename);
        ok &= stmt.addParam(timestampToInt64(info.last_modified_fs));
        ok &= stmt.addParam(static_cast<int64_t>(info.filesize_bytes));
        ok &= stmt.addParam(timestampToInt64(info.date_added));
        ok &= stmt.addParam(timestampToInt64(info.last_scanned));
        ok &= stmt.addParam(info.title);
        ok &= stmt.addParam(info.artist_name);
        ok &= stmt.addParam(info.album_title);
        ok &= stmt.addParam(info.album_artist_name);
        ok &= stmt.addParam(info.track_number);
        ok &= stmt.addParam(info.disc_number);
        ok &= stmt.addParam(info.year);
        ok &= stmt.addParam(durationToInt64(info.duration));
        ok &= stmt.addParam(info.samplerate);
        ok &= stmt.addParam(info.channels);
        ok &= stmt.addParam(info.bitrate);
        ok &= stmt.addParam(info.codec_name);
        ok &= info.bpm.has_value() ? stmt.addParam((int64_t)info.bpm.value()) : stmt.addNullParam();
        ok &= info.intro_end.has_value() ? stmt.addParam(durationToInt64(info.intro_end.value())) : stmt.addNullParam();
        ok &= info.outro_start.has_value() ? stmt.addParam(durationToInt64(info.outro_start.value())) : stmt.addNullParam();
        ok &= stmt.addParam(info.key_string);
        ok &= stmt.addParam(info.beat_locations_json);
        ok &= stmt.addParam(0); // rating (unused, kept in DB for compatibility)
        ok &= stmt.addParam(0); // liked_status (unused, kept in DB for compatibility)
        ok &= stmt.addParam(0); // play_count (unused, kept in DB for compatibility)
        ok &= stmt.addParam(timestampToInt64(info.last_played));
        ok &= stmt.addParam(info.internal_content_hash);
        ok &= stmt.addParam(std::string{}); // user_notes (unused, kept in DB for compatibility)
        ok &= stmt.addParam(info.is_missing ? 1 : 0);

        // Add status field
        std::string statusStr;
        switch (info.status)
        {
        case TrackStatus::Unknown:
            statusStr = "unknown";
            break;
        case TrackStatus::Ok:
            statusStr = "ok";
            break;
        case TrackStatus::BadFormat:
            statusStr = "bad_format";
            break;
        }
        ok &= stmt.addParam(statusStr);

        if (forUpdate)
        {
            ok &= stmt.addParam(info.trackId); // For the WHERE track_id = ?
        }
        if (!ok)
        {
            spdlog::error("Failed to bind one or more parameters for TrackInfo: {}", pathToString(info.reconstructFullPath()));
        }
        return ok;
    }

} // anonymous namespace

namespace jucyaudio
{
    namespace database
    {

        SqliteTrackDatabase::SqliteTrackDatabase()
            : m_db{},
              m_tagManager{m_db},
              m_mixManager{m_db},
              m_libraryRoootManager{m_db},
              m_workingSetManager{m_db},
              m_folderDatabase{m_db},
              m_markerManager{m_db},
              m_mixMarkerManager{m_db},
              m_albumManager{m_db},
              m_eqPresetManager{m_db},
              m_reverbPresetManager{m_db},
              m_masterPluginChainManager{m_db},
              m_databaseFilePath{},
              m_lastErrorMessage{},
              m_cachedTotalTrackCount{0},
              m_cachedTotalTrackCountValid{false}

        {
            spdlog::debug("SqliteTrackDatabase created.");
        }

        SqliteTrackDatabase::~SqliteTrackDatabase()
        {
            close(); // m_db.close() will be called by SqliteDatabase destructor //-V1053
                     // if not already
            spdlog::debug("SqliteTrackDatabase destroyed.");
        }

        IFolderDatabase &SqliteTrackDatabase::getFolderDatabase() const
        {
            assert(isOpen() && "Cannot get folder database when database is not open");
            return m_folderDatabase; // Return reference to the tag manager
        }

        ITagManager &SqliteTrackDatabase::getTagManager()
        {
            assert(isOpen() && "Cannot get tag manager when database is not open");
            return m_tagManager; // Return reference to the tag manager
        }

        const ITagManager &SqliteTrackDatabase::getTagManager() const
        {
            assert(isOpen() && "Cannot get tag manager when database is not open");
            return m_tagManager; // Return const reference to the tag manager
        }

        IMixManager &SqliteTrackDatabase::getMixManager()
        {
            assert(isOpen() && "Cannot get mix manager when database is not open");
            return m_mixManager;
        }

        const IMixManager &SqliteTrackDatabase::getMixManager() const
        {
            assert(isOpen() && "Cannot get mix manager when database is not open");
            return m_mixManager;
        }

        IWorkingSetManager &SqliteTrackDatabase::getWorkingSetManager()
        {
            assert(isOpen() && "Cannot get working-set manager when database is not open");
            return m_workingSetManager;
        }

        const IWorkingSetManager &SqliteTrackDatabase::getWorkingSetManager() const
        {
            assert(isOpen() && "Cannot get working-set manager when database is not open");
            return m_workingSetManager;
        }

        ILibraryRootManager &SqliteTrackDatabase::getLibraryRootManager()
        {
            assert(isOpen() && "Cannot get marker manager when database is not open");
            return m_libraryRoootManager;
        }

        const ILibraryRootManager &SqliteTrackDatabase::getLibraryRootManager() const
        {
            assert(isOpen() && "Cannot get marker manager when database is not open");
            return m_libraryRoootManager;
        }

        IMarkerManager &SqliteTrackDatabase::getMarkerManager()
        {
            assert(isOpen() && "Cannot get marker manager when database is not open");
            return m_markerManager;
        }

        const IMarkerManager &SqliteTrackDatabase::getMarkerManager() const
        {
            assert(isOpen() && "Cannot get marker manager when database is not open");
            return m_markerManager;
        }

        IMixMarkerManager &SqliteTrackDatabase::getMixMarkerManager()
        {
            assert(isOpen() && "Cannot get mix marker manager when database is not open");
            return m_mixMarkerManager;
        }

        const IMixMarkerManager &SqliteTrackDatabase::getMixMarkerManager() const
        {
            assert(isOpen() && "Cannot get mix marker manager when database is not open");
            return m_mixMarkerManager;
        }

        IAlbumManager &SqliteTrackDatabase::getAlbumManager()
        {
            assert(isOpen() && "Cannot get album manager when database is not open");
            return m_albumManager;
        }

        const IAlbumManager &SqliteTrackDatabase::getAlbumManager() const
        {
            assert(isOpen() && "Cannot get album manager when database is not open");
            return m_albumManager;
        }

        std::string SqliteTrackDatabase::getLastError() const
        {
            return m_db.isValid() ? m_db.getLastError() : m_lastErrorMessage; // Prefer m_db's error if open
        }

        bool SqliteTrackDatabase::isOpen() const
        {
            return m_db.isValid();
        }

        void SqliteTrackDatabase::close()
        {
            if (isOpen())
            {
                spdlog::info("Closing SQLite database: {}", pathToString(m_databaseFilePath));
            }
            m_db.close();
            m_databaseFilePath.clear(); // Clear path only if close was
                                        // intentional by this class
        }

        DbResult SqliteTrackDatabase::connect(const std::filesystem::path &databaseFilePath)
        {
            if (isOpen())
            {
                close();
            }
            m_databaseFilePath = databaseFilePath;
            m_lastErrorMessage.clear();

            std::filesystem::path parentDir = databaseFilePath.parent_path();
            if (!parentDir.empty() && !std::filesystem::exists(parentDir))
            {
                try
                {
                    if (!std::filesystem::create_directories(parentDir))
                    {
                        m_lastErrorMessage = "Could not create parent directory: " + pathToString(parentDir);
                        return DbResult::failure(DbResultStatus::ErrorIO, m_lastErrorMessage);
                    }
                }
                catch (const std::filesystem::filesystem_error &e)
                {
                    m_lastErrorMessage = "Filesystem error creating parent directory " + pathToString(parentDir) + ": " + e.what();
                    return DbResult::failure(DbResultStatus::ErrorIO, m_lastErrorMessage);
                }
            }

            if (!m_db.open(pathToString(databaseFilePath)))
            {                                             // Use u8string for cross-platform path safety
                m_lastErrorMessage = m_db.getLastError(); // SqliteDatabase::open should set its
                                                          // error
                return DbResult::failure(DbResultStatus::ErrorConnection, m_lastErrorMessage);
            }
            spdlog::info("SQLite database opened: {}", pathToString(databaseFilePath));

            if (!m_db.execute("PRAGMA journal_mode=WAL;"))
            {
                spdlog::warn("Failed to set WAL mode (continuing). Error: {}", m_db.getLastError());
            }
            if (!m_db.execute("PRAGMA foreign_keys=ON;"))
            {
                m_lastErrorMessage = "Failed to enable foreign keys: " + m_db.getLastError();
                m_db.close(); // Close on critical pragma failure
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }
            
            // Check auto_vacuum setting
            SqliteStatement checkVacuum{m_db, "PRAGMA auto_vacuum;"};
            if (checkVacuum.getNextResult())
            {
                const auto mode = checkVacuum.getInt32(0);
                if (mode == 0) // NONE - database will bloat
                {
                    spdlog::warn("Database has auto_vacuum=NONE. Consider running VACUUM to enable incremental vacuum.");
                }
            }
            
            // Moderate performance optimizations
            m_db.execute("PRAGMA cache_size=-16000;");   // 16MB cache (was default ~2MB)
            m_db.execute("PRAGMA mmap_size=268435456;"); // 256MB memory-mapped I/O for faster reads

            DbResult schemaResult = createTablesIfNeeded();
            if (!schemaResult.isOk())
            {
                m_db.close();
                return schemaResult;
            }
            m_folderDatabase.initialize();
            return DbResult::success();
        }

        bool SqliteTrackDatabase::runMaintenanceTasks(std::atomic<bool> &shouldCancel)
        {
            // Call the overloaded version with no progress callback
            return runMaintenanceTasks(shouldCancel, nullptr);
        }
        
        bool SqliteTrackDatabase::runMaintenanceTasks(std::atomic<bool> &shouldCancel, MaintenanceProgressCallback progressCb)
        {
            if (!isOpen())
            {
                spdlog::error("Database not open for maintenance tasks.");
                return false;
            }
            
            spdlog::info("Starting database maintenance tasks...");
            
            const int totalSteps = 5; // 4 SQL operations + 1 WAV enrichment
            int currentStep = 0;
            
            for (const auto *sql : maintenanceSqlStatements)
            {
                if (shouldCancel)
                {
                    spdlog::info("Database maintenance cancelled by user.");
                    return false;
                }
                
                // Determine which step we're on and report progress
                std::string statusMessage;
                if (std::string(sql).find("PRAGMA optimize") != std::string::npos)
                {
                    statusMessage = "Optimizing query planner statistics...";
                    spdlog::info("Step 1/4: {}", statusMessage);
                }
                else if (std::string(sql).find("TracksSearchFTS") != std::string::npos && 
                         std::string(sql).find("rebuild") != std::string::npos)
                {
                    statusMessage = "Rebuilding search index (this may take a moment)...";
                    spdlog::info("Step 2/4: {}", statusMessage);
                }
                else if (std::string(sql).find("TracksSearchFTS") != std::string::npos && 
                         std::string(sql).find("optimize") != std::string::npos)
                {
                    statusMessage = "Optimizing search index...";
                    spdlog::info("Step 3/4: {}", statusMessage);
                }
                else if (std::string(sql).find("VACUUM") != std::string::npos)
                {
                    statusMessage = "Vacuuming database (this may take a while)...";
                    spdlog::info("Step 4/4: {}", statusMessage);
                }
                
                // Report progress if callback provided
                if (progressCb)
                {
                    progressCb((currentStep * 100) / totalSteps, statusMessage);
                }
                
                if (!m_db.execute(sql))
                {
                    // FTS5 optimize might fail if the table doesn't exist yet (pre-migration databases)
                    if (std::string(sql).find("TracksSearchFTS") != std::string::npos)
                    {
                        spdlog::warn("FTS5 maintenance skipped (table may not exist): {}", m_db.getLastError());
                        currentStep++;  // Still count this as a completed step
                        continue;  // Don't fail the whole maintenance for this
                    }
                    
                    m_lastErrorMessage = "Maintenance statement failed [" + std::string(sql) + "] Error: " + m_db.getLastError();
                    spdlog::error("Maintenance task failed: {}", m_lastErrorMessage);
                    return false;
                }
                
                currentStep++;
            }
            
            // Step 5: Run WAV metadata enrichment
            if (progressCb)
            {
                progressCb((currentStep * 100) / totalSteps, "Enriching WAV metadata...");
            }
            spdlog::info("Running WAV metadata enrichment...");
            
            if (!enrichWavMetadata(shouldCancel))
            {
                spdlog::warn("WAV metadata enrichment failed, but continuing maintenance.");
            }
            
            spdlog::info("Database maintenance tasks completed successfully.");
            return true;
        }

        DbResult SqliteTrackDatabase::createTablesIfNeeded()
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for schema creation.");
            }

            spdlog::info("Verifying/Creating database schema...");
            int currentVersion = getDBSchemaVersion();
            const int latestSchemaVersion = 32;

            if (currentVersion == 0)
            {
                // This is a new database, create the full schema from scratch.
                spdlog::info("No schema found. Creating new database with latest schema (version {}).", latestSchemaVersion);

                if (SqliteTransaction transaction{m_db})
                {
                    // Two arrays, and both of them. convergenceSqlStatements holds what used to be
                    // created only by a migration rung, which a new database never runs - leaving every
                    // library created since without search, markers or the presets.
                    for (const auto *sql : initialSqlStatements)
                    {
                        if (!m_db.execute(sql))
                        {
                            m_lastErrorMessage = "Schema creation failed on SQL: [" + std::string(sql) + "] Error: " + m_db.getLastError();
                            transaction.rollback();
                            return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                        }
                    }

                    for (const auto *sql : convergenceSqlStatements)
                    {
                        if (!m_db.execute(sql))
                        {
                            m_lastErrorMessage = "Schema creation failed on SQL: [" + std::string(sql) + "] Error: " + m_db.getLastError();
                            transaction.rollback();
                            return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                        }
                    }

                    if (const auto seedResult{seedDefaultGenres()}; !seedResult.isOk())
                    {
                        transaction.rollback();
                        return seedResult;
                    }

                    if (const auto seedResult{seedDefaultEQPresets()}; !seedResult.isOk())
                    {
                        transaction.rollback();
                        return seedResult;
                    }

                    if (const auto seedResult{seedDefaultReverbPresets()}; !seedResult.isOk())
                    {
                        transaction.rollback();
                        return seedResult;
                    }

                    // After creating tables, set the schema version to the latest.
                    SqliteStatement stmt{m_db, "INSERT INTO SchemaInfo (key, value) VALUES ('schema_version', ?);"};
                    stmt.addParam(std::to_string(latestSchemaVersion));
                    if (!stmt.execute())
                    {
                        m_lastErrorMessage = "Failed to set initial schema version: " + m_db.getLastError();
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit new schema creation transaction.");
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin new schema creation transaction.");
                }
            }
            else if (currentVersion < latestSchemaVersion)
            {
                // An existing database was found, run migrations if necessary.
                const auto migrationResult{runMigrations()};
                if (!migrationResult.isOk())
                {
                    return migrationResult;
                }
            }

            spdlog::info("Database schema verified/created successfully.");
            return DbResult::success();
        }

        std::filesystem::path SqliteTrackDatabase::reconstructFullPath(FolderId folderId) const
        {
            if (folderId == m_lastKnownFolderId)
            {
                return m_lastKnownFolderPath;
            }

            auto folderOpt = m_folderDatabase.getFolderById(folderId);
            if (!folderOpt)
            {
                spdlog::error("reconstructFullPath FAIL: Could not find folder with ID {}.", folderId);
                return {};
            }
            
            // Use actual_path if available, otherwise fall back to normalized path
            if (!folderOpt->actualPath.empty())
            {
                return folderOpt->actualPath;
            }
            return folderOpt->path;
        }

        std::filesystem::path SqliteTrackDatabase::reconstructFullPath(const TrackInfo &trackInfo) const
        {
            if (trackInfo.folderId == -1)
                return {};
            return reconstructFullPath(trackInfo.folderId) / trackInfo.filename;
        }

        int SqliteTrackDatabase::getDBSchemaVersion() const
        {
            if (!isOpen())
                return 0; // Or -1 to indicate error
            if (!m_db.doesTableExist("SchemaInfo"))
                return 0;
            SqliteStatement stmt{m_db, "SELECT value FROM SchemaInfo WHERE key = 'schema_version';"};
            if (stmt.isValid() && stmt.getNextResult())
            {
                if (!stmt.isNull(0))
                {
                    try
                    {
                        return std::stoi(stmt.getText(0));
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::error("Failed to parse schema_version '{}': {}", stmt.getText(0), e.what());
                        return 0; // Or error code
                    }
                }
            }
            spdlog::warn("Could not retrieve schema_version or table is empty.");
            return 0; // Default to 0 if not found or error
        }

        DbResult SqliteTrackDatabase::setDBSchemaVersion(int version)
        {
            if (!isOpen())
                return DbResult::failure(DbResultStatus::ErrorConnection, "Database not open.");
            SqliteStatement stmt{m_db,
                "UPDATE SchemaInfo SET value = ? WHERE "
                "key = 'schema_version';"};
            if (!stmt.isValid())
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "Prepare failed for setDBSchemaVersion: " + m_db.getLastError());
            }
            stmt.addParam(std::to_string(version));
            if (!stmt.execute())
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "Execute failed for setDBSchemaVersion: " + m_db.getLastError());
            }
            return DbResult::success();
        }

        DbResult SqliteTrackDatabase::seedDefaultGenres()
        {
            SqliteStatement stmt{m_db, "INSERT OR IGNORE INTO Genres (name) VALUES (?);"};
            if (!stmt.isValid())
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "Prepare failed for genre seeding: " + m_db.getLastError());
            }

            for (const auto *genre : defaultGenreVocabulary)
            {
                stmt.reset();
                stmt.addParam(std::string_view{genre});
                if (!stmt.execute())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to seed genre '" + std::string{genre} + "': " + m_db.getLastError());
                }
            }

            spdlog::info("Seeded {} default genres.", std::size(defaultGenreVocabulary));
            return DbResult::success();
        }

        DbResult SqliteTrackDatabase::reconstructMissingFolderPaths()
        {
            // A row whose root_path was never computed is invisible to a unique index on that column -
            // SQLite treats NULLs as distinct - so two of them that name the same directory would slip
            // past v31 and keep the exact failure it exists to end: buildCacheIfNeeded reconstructs
            // their paths on the way past, finds the collision, and returns false *before* writing
            // either one, so the pair survives every rebuild. initialize() discards that refusal, so
            // the library just navigates without a cache forever.
            //
            // Computing the paths here means the duplicate map below sees them like any other pair.
            // The rule is buildCacheIfNeeded's, and has to stay that way: a root is
            // normalizeForCache(name), a child is normalizeForCache(parent path + "\" + name). It
            // cannot be done in SQL, because normalizeForCache is Unicode-aware and platform-native.
            struct FolderRow
            {
                FolderId parentId{-1};
                std::string name;
                std::string path;
            };

            std::unordered_map<FolderId, FolderRow> rows;
            {
                SqliteStatement stmt{m_db, "SELECT folder_id, COALESCE(parent_id, -1), name, COALESCE(root_path, '') FROM Folders;"};
                if (!stmt.isValid())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "v31 could not read Folders: " + m_db.getLastError());
                }

                while (stmt.getNextResult())
                {
                    FolderRow row{};
                    const auto folderId{stmt.getInt64(0)};
                    row.parentId = stmt.getInt64(1);
                    row.name = stmt.getText(2);
                    row.path = stmt.getText(3);
                    rows[folderId] = std::move(row);
                }

                // The loop above ends the same way on "no more rows" and on "the step failed", and a
                // read that stopped early would leave rows looking like they have no parent.
                if (stmt.hasError())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "v31 read of Folders stopped early: " + m_db.getLastError());
                }
            }

            std::vector<std::pair<FolderId, std::string>> backfill;
            std::vector<FolderId> chain;
            for (const auto &[folderId, row] : rows)
            {
                if (!row.path.empty())
                {
                    continue;
                }

                // Up to the first ancestor that already knows its path, or to a root.
                chain.clear();
                bool resolvable{true};
                for (auto cursor{folderId}; cursor > 0;)
                {
                    const auto it{rows.find(cursor)};
                    if (it == rows.end() || chain.size() > rows.size())
                    {
                        // A missing parent, or a parent_id cycle. Both are broken in a way this rung is
                        // not about, and leaving root_path NULL keeps the row out of the index.
                        spdlog::warn("v31: cannot reconstruct the path of folder {} - its parent chain is broken.", folderId);
                        resolvable = false;
                        break;
                    }
                    if (!it->second.path.empty())
                    {
                        break;
                    }
                    chain.push_back(cursor);
                    cursor = it->second.parentId;
                }

                if (!resolvable)
                {
                    continue;
                }

                // Downwards, so each row's parent already has its path.
                for (const auto id : std::views::reverse(chain))
                {
                    const auto it{rows.find(id)};
                    if (it == rows.end())
                    {
                        continue;
                    }

                    auto &entry{it->second};
                    if (entry.parentId > 0)
                    {
                        const auto parentIt{rows.find(entry.parentId)};
                        if (parentIt == rows.end())
                        {
                            continue;
                        }
                        entry.path = normalizeForCache(parentIt->second.path + "\\" + entry.name);
                    }
                    else
                    {
                        entry.path = normalizeForCache(entry.name);
                    }

                    // A folder with no name reconstructs to nothing, and writing that back would turn
                    // "unknown" into an empty string - which, unlike NULL, collides with the next one.
                    if (entry.path.empty())
                    {
                        spdlog::warn("v31: folder {} has no name, so it keeps no path.", id);
                        continue;
                    }
                    backfill.emplace_back(id, entry.path);
                }
            }

            if (backfill.empty())
            {
                return DbResult::success();
            }

            spdlog::info("v31: reconstructing the path of {} folder row(s) that never had one.", backfill.size());

            SqliteStatement update{m_db, "UPDATE Folders SET root_path = ? WHERE folder_id = ?;"};
            if (!update.isValid())
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "v31 could not prepare the path backfill: " + m_db.getLastError());
            }

            for (const auto &[folderId, path] : backfill)
            {
                if (!update.addParam(path) || !update.addParam(folderId) || !update.execute())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "v31 could not write a reconstructed path: " + m_db.getLastError());
                }
                update.reset();
            }

            return DbResult::success();
        }

        DbResult SqliteTrackDatabase::rebuildMixTracksWithoutPrimaryKey()
        {
            // The v6 rung gave MixTracks PRIMARY KEY(mix_id, track_id); the schema a new database is
            // built from has no uniqueness on those columns at all. That is not a cosmetic difference:
            // a mix may legitimately contain the same track twice - MissingFileScanTask says so in as
            // many words, and it is why that task returns indices rather than track ids - and
            // saveMix inserts each row with a plain INSERT. On a library that migrated up the ladder
            // the second row fails the primary key and rolls the whole save back, so a mix that plays
            // fine cannot be saved. On a library created from scratch it works.
            //
            // Only when the old shape is actually there. pragma_index_list reports the implicit index
            // a composite primary key creates with origin 'pk', which is the structural question -
            // rather than looking for the words PRIMARY KEY in the stored DDL.
            int64_t primaryKeyIndexes{0};
            {
                SqliteStatement stmt{m_db, "SELECT COUNT(*) FROM pragma_index_list('MixTracks') WHERE origin = 'pk';"};
                if (!stmt.isValid() || !stmt.getNextResult())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "v32 could not inspect MixTracks: " + m_db.getLastError());
                }
                primaryKeyIndexes = stmt.getInt64(0);
            }

            if (primaryKeyIndexes == 0)
            {
                return DbResult::success();
            }

            spdlog::warn("v32: MixTracks still carries the v6 primary key; rebuilding it so a mix can hold a track twice.");

            // Copy, drop, rename. Nothing in the schema references MixTracks, so dropping it cascades
            // to nothing - it is a child of Mixes and Tracks, never a parent - and the rename has no
            // foreign key clauses elsewhere to fix up. All of it inside the migration's transaction,
            // so a failure anywhere leaves the original table untouched.
            const char *rebuildSteps[] = {
                R"SQL(
                CREATE TABLE MixTracks_v32(
                    mix_id INTEGER NOT NULL,
                    track_id INTEGER NOT NULL,
                    order_in_mix INTEGER NOT NULL,
                    mix_data TEXT NOT NULL,
                    FOREIGN KEY(mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE,
                    FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
                );)SQL",
                "INSERT INTO MixTracks_v32 (mix_id, track_id, order_in_mix, mix_data) "
                "SELECT mix_id, track_id, order_in_mix, mix_data FROM MixTracks;",
                "DROP TABLE MixTracks;",
                "ALTER TABLE MixTracks_v32 RENAME TO MixTracks;",
            };

            for (const char *step : rebuildSteps)
            {
                if (!m_db.execute(step))
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "v32 MixTracks rebuild failed: " + m_db.getLastError());
                }
            }

            return DbResult::success();
        }

        DbResult SqliteTrackDatabase::seedDefaultEQPresets()
        {
            // INSERT OR IGNORE, not INSERT: this runs from the new-database path, from the v17 rung and
            // from the v32 repair, and the repair meets databases that already have some or all of
            // them. name is UNIQUE, so the conflict is on exactly the identity that matters.
            SqliteStatement stmt{m_db, "INSERT OR IGNORE INTO EQPresets (name, is_deletable, settings_json) VALUES (?, 0, ?);"};
            if (!stmt.isValid())
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "Prepare failed for EQ preset seeding: " + m_db.getLastError());
            }

            const std::vector<database::model::EQPreset> defaultPresets = {database::model::EQPreset::createFlatPreset(),
                database::model::EQPreset::createRockPreset(),
                database::model::EQPreset::createDancePreset(),
                database::model::EQPreset::createVocalBoostPreset(),
                database::model::EQPreset::createBassBoostPreset(),
                database::model::EQPreset::createTrebleBoostPreset()};

            for (const auto &preset : defaultPresets)
            {
                stmt.reset();
                if (!stmt.addParam(preset.name.toStdString()) || !stmt.addParam(preset.settings.toJson().toStdString()) || !stmt.execute())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB,
                        "Failed to seed EQ preset '" + preset.name.toStdString() + "': " + m_db.getLastError());
                }
            }

            spdlog::info("Seeded {} default EQ presets.", defaultPresets.size());
            return DbResult::success();
        }

        DbResult SqliteTrackDatabase::seedDefaultReverbPresets()
        {
            struct FactoryPreset
            {
                const char *name;
                const char *json;
            };

            static constexpr FactoryPreset factoryPresets[] = {
                {"Small Room", R"({"roomSize":0.2,"damping":0.7,"wetLevel":0.25,"dryLevel":0.75,"width":0.8,"freezeMode":0.0,"isActive":true})"},
                {"Large Hall", R"({"roomSize":0.8,"damping":0.5,"wetLevel":0.35,"dryLevel":0.65,"width":1.0,"freezeMode":0.0,"isActive":true})"},
                {"Cathedral", R"({"roomSize":0.95,"damping":0.3,"wetLevel":0.4,"dryLevel":0.6,"width":1.0,"freezeMode":0.0,"isActive":true})"},
                {"Plate", R"({"roomSize":0.4,"damping":0.9,"wetLevel":0.3,"dryLevel":0.7,"width":1.0,"freezeMode":0.0,"isActive":true})"},
                {"Spring", R"({"roomSize":0.3,"damping":0.6,"wetLevel":0.35,"dryLevel":0.65,"width":0.5,"freezeMode":0.0,"isActive":true})"},
                {"Ambient", R"({"roomSize":0.85,"damping":0.2,"wetLevel":0.5,"dryLevel":0.5,"width":1.0,"freezeMode":0.0,"isActive":true})"},
                {"Subtle", R"({"roomSize":0.15,"damping":0.8,"wetLevel":0.15,"dryLevel":0.85,"width":0.7,"freezeMode":0.0,"isActive":true})"}};

            SqliteStatement stmt{m_db, "INSERT OR IGNORE INTO ReverbPresets (name, is_deletable, settings_json) VALUES (?, 0, ?);"};
            if (!stmt.isValid())
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "Prepare failed for reverb preset seeding: " + m_db.getLastError());
            }

            for (const auto &preset : factoryPresets)
            {
                stmt.reset();
                if (!stmt.addParam(std::string_view{preset.name}) || !stmt.addParam(std::string_view{preset.json}) || !stmt.execute())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB,
                        "Failed to seed reverb preset '" + std::string{preset.name} + "': " + m_db.getLastError());
                }
            }

            spdlog::info("Seeded {} factory reverb presets.", std::size(factoryPresets));
            return DbResult::success();
        }

        DbResult SqliteTrackDatabase::runMigrations()
        {
            if (!isOpen())
                return DbResult::failure(DbResultStatus::ErrorConnection, "Database not open.");

            int currentVersion = getDBSchemaVersion();
            spdlog::info("Current DB schema version: {}", currentVersion);

            if (currentVersion < 2)
            {
                spdlog::info("Migrating database from version 1 to 2...");
                if (SqliteTransaction transaction{m_db})
                {
                    if (!m_db.execute("ALTER TABLE Mixes ADD COLUMN source_ws_id INTEGER REFERENCES WorkingSets(ws_id);") ||
                        !m_db.execute("ALTER TABLE Mixes ADD COLUMN status TEXT DEFAULT 'New';") ||
                        !m_db.execute("ALTER TABLE MixTracks ADD COLUMN is_active INTEGER DEFAULT 1;"))
                    {
                        m_lastErrorMessage = "Failed to alter tables for V2 schema: " + m_db.getLastError();
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                    }

                    if (auto result = setDBSchemaVersion(2); !result.isOk())
                    {
                        m_lastErrorMessage = "Failed to update schema version to 2: " + result.errorMessage;
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 2.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }

                currentVersion = 2; // Update for next check
            }

            if (currentVersion < 3)
            {
                spdlog::info("Migrating database from version 2 to 3...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Add sort_order column to WorkingSets table for persistent sort configuration
                    if (!m_db.execute("ALTER TABLE WorkingSets ADD COLUMN sort_order TEXT;"))
                    {
                        m_lastErrorMessage = "Failed to add sort_order column to WorkingSets table: " + m_db.getLastError();
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                    }

                    if (auto result = setDBSchemaVersion(3); !result.isOk())
                    {
                        m_lastErrorMessage = "Failed to update schema version to 3: " + result.errorMessage;
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 3.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }

                currentVersion = 3; // Update for next check
            }

            if (currentVersion < 4)
            {
                spdlog::info("Migrating database from version 3 to 4 - Adding TrackMarkers table...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Create TrackMarkers table
                    if (!m_db.execute(R"SQL(
                        CREATE TABLE IF NOT EXISTS TrackMarkers (
                            marker_id INTEGER PRIMARY KEY AUTOINCREMENT,
                            track_id INTEGER NOT NULL,
                            position_ms INTEGER NOT NULL,
                            comment TEXT NOT NULL,
                            created_at INTEGER NOT NULL,
                            updated_at INTEGER NOT NULL,
                            color TEXT,
                            emoji TEXT,
                            FOREIGN KEY (track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
                        );
                    )SQL"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create TrackMarkers table.");
                    }

                    // Create index for efficient track lookups
                    if (!m_db.execute("CREATE INDEX IF NOT EXISTS idx_trackmarkers_track_id ON TrackMarkers (track_id);"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create TrackMarkers index.");
                    }

                    // Update schema version
                    if (auto result = setDBSchemaVersion(4); !result.isOk())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 4.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 4.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }

                currentVersion = 4; // Update for next check
            }

            if (currentVersion < 5)
            {
                spdlog::info("Migrating database from version 4 to 5 - Adding status field to Tracks table...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Add status column to Tracks table
                    if (!m_db.execute("ALTER TABLE Tracks ADD COLUMN status TEXT NOT NULL DEFAULT 'unknown';"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to add status column to Tracks table.");
                    }

                    // Update existing tracks that have BPM data to 'ok' status
                    if (!m_db.execute("UPDATE Tracks SET status = 'ok' WHERE bpm IS NOT NULL AND bpm > 0;"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update existing track statuses.");
                    }

                    // Create index for efficient status lookups
                    if (!m_db.execute("CREATE INDEX IF NOT EXISTS idx_tracks_status ON Tracks (status);"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create status index.");
                    }

                    // Update schema version
                    if (auto result = setDBSchemaVersion(5); !result.isOk())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 5.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 5.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
            }

            if (currentVersion < 6)
            {
                spdlog::info("Migrating database from version 5 to 6 - Transition to ATTACH-based mix model...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Drop all mix data (as discussed, it's test data)
                    spdlog::info("Dropping existing mix data for clean transition...");

                    // Delete all mix tracks first (due to foreign key constraints)
                    if (!m_db.execute("DELETE FROM MixTracks;"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to delete existing MixTracks.");
                    }

                    // Delete all mixes
                    if (!m_db.execute("DELETE FROM Mixes;"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to delete existing Mixes.");
                    }

                    // Drop the old MixTracks table
                    if (!m_db.execute("DROP TABLE IF EXISTS MixTracks;"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to drop old MixTracks table.");
                    }

                    // Create new MixTracks table with JSON structure
                    const char *createMixTracksV6 = R"(
CREATE TABLE MixTracks(
    mix_id INTEGER NOT NULL,
    track_id INTEGER NOT NULL,
    order_in_mix INTEGER NOT NULL,
    mix_data TEXT NOT NULL, -- JSON with cue, attach, envelope data
    PRIMARY KEY(mix_id, track_id),
    FOREIGN KEY(mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE,
    FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
);)";

                    if (!m_db.execute(createMixTracksV6))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create new MixTracks table.");
                    }

                    // Create index on order_in_mix for efficient sorting
                    if (!m_db.execute("CREATE INDEX idx_mixtracks_order ON MixTracks(mix_id, order_in_mix);"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create order index.");
                    }

                    // Update schema version
                    if (auto result = setDBSchemaVersion(6); !result.isOk())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 6.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 6 - ATTACH-based model ready.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }

                currentVersion = 6; // Update for next check
            }

            if (currentVersion < 7)
            {
                spdlog::info("Migrating database from version 6 to 7 - Adding MixUndoHistory table...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Create MixUndoHistory table for undo/redo functionality
                    const char *createMixUndoHistory = R"SQL(
CREATE TABLE IF NOT EXISTS MixUndoHistory (
    undo_id INTEGER PRIMARY KEY AUTOINCREMENT,
    mix_id INTEGER NOT NULL,
    operation_type TEXT NOT NULL,      -- 'INSERT', 'UPDATE', 'DELETE'
    table_name TEXT NOT NULL,          -- 'MixTracks' or 'Mixes'
    record_id INTEGER,                  -- track_id or mix_id
    old_state TEXT,                     -- JSON of previous state
    new_state TEXT,                     -- JSON of new state
    FOREIGN KEY (mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE
);)SQL";

                    if (!m_db.execute(createMixUndoHistory))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create MixUndoHistory table.");
                    }

                    // Create index for efficient mix lookups
                    if (!m_db.execute("CREATE INDEX IF NOT EXISTS idx_mixundohistory_mix_id ON MixUndoHistory (mix_id);"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create MixUndoHistory index.");
                    }

                    // Update schema version
                    if (auto result = setDBSchemaVersion(7); !result.isOk())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 7.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 7 - MixUndoHistory table ready.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
            }

            if (currentVersion < 8)
            {
                spdlog::info("Migrating database from version 7 to 8 - Adding undo stack tracking...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Drop and recreate MixUndoHistory with operation_id
                    if (!m_db.execute("DROP TABLE IF EXISTS MixUndoHistory;"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to drop old MixUndoHistory table.");
                    }

                    // Recreate with operation_id
                    const char *createMixUndoHistory = R"SQL(
CREATE TABLE MixUndoHistory (
    undo_id INTEGER PRIMARY KEY AUTOINCREMENT,
    mix_id INTEGER NOT NULL,
    operation_id INTEGER NOT NULL,
    operation_type TEXT NOT NULL,
    table_name TEXT NOT NULL,
    record_id INTEGER,
    old_state TEXT,
    new_state TEXT,
    timestamp INTEGER DEFAULT (strftime('%s', 'now')),
    FOREIGN KEY (mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE
);)SQL";

                    if (!m_db.execute(createMixUndoHistory))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to recreate MixUndoHistory table.");
                    }

                    // Create indexes
                    if (!m_db.execute("CREATE INDEX idx_mixundohistory_mix_id ON MixUndoHistory (mix_id);") ||
                        !m_db.execute("CREATE INDEX idx_mixundohistory_operation_id ON MixUndoHistory (operation_id);"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create MixUndoHistory indexes.");
                    }

                    // Add undo_stack_position to Mixes table
                    if (!m_db.execute("ALTER TABLE Mixes ADD COLUMN undo_stack_position INTEGER DEFAULT 0;"))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to add undo_stack_position to Mixes table.");
                    }

                    // Update schema version
                    if (auto result = setDBSchemaVersion(8); !result.isOk())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 8.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 8 - Stack-based undo/redo ready.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
            }

            // Handle version 8 or 9 - both need the LibraryRoots columns upgrade
            if (currentVersion < 10)
            {
                spdlog::info("Migrating database from version {} to 10 - Adding file_count and last_scanned to LibraryRoots...", currentVersion);
                if (SqliteTransaction transaction{m_db})
                {
                    // Check if columns already exist to avoid errors
                    bool needsFileCount = true;
                    bool needsLastScanned = true;
                    
                    // Check existing columns using PRAGMA table_info
                    SqliteStatement checkStmt{m_db, "PRAGMA table_info(LibraryRoots);"};
                    while (checkStmt.getNextResult())
                    {
                        const auto columnName = checkStmt.getText(1); // Column name is at index 1
                        if (columnName == "file_count")
                            needsFileCount = false;
                        if (columnName == "last_scanned")
                            needsLastScanned = false;
                    }
                    
                    // Add file_count column if it doesn't exist
                    if (needsFileCount)
                    {
                        if (!m_db.execute("ALTER TABLE LibraryRoots ADD COLUMN file_count INTEGER DEFAULT 0;"))
                        {
                            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to add file_count column to LibraryRoots.");
                        }
                        spdlog::info("Added file_count column to LibraryRoots table.");
                    }
                    
                    // Add last_scanned column if it doesn't exist
                    if (needsLastScanned)
                    {
                        if (!m_db.execute("ALTER TABLE LibraryRoots ADD COLUMN last_scanned INTEGER;"))
                        {
                            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to add last_scanned column to LibraryRoots.");
                        }
                        spdlog::info("Added last_scanned column to LibraryRoots table.");
                    }

                    // Update schema version to 10
                    if (auto result = setDBSchemaVersion(10); !result.isOk())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 10.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 10 - LibraryRoots enhanced with file_count and last_scanned.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                
                currentVersion = 10; // Update for any future migrations
            }

            if (currentVersion < 11)
            {
                spdlog::info("Migrating database from version 10 to 11 - Adding WaveformCache table...");
                if (SqliteTransaction transaction{m_db})
                {
                    const char* createWaveformCacheTable = R"SQL(
                        CREATE TABLE IF NOT EXISTS WaveformCache (
                            track_id INTEGER PRIMARY KEY NOT NULL,
                            waveform_blob BLOB NOT NULL,
                            FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
                        );
                    )SQL";

                    if (!m_db.execute(createWaveformCacheTable))
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create WaveformCache table.");
                    }

                    if (auto result = setDBSchemaVersion(11); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 11.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 11.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 11;
            }

            if (currentVersion < 12)
            {
                spdlog::info("Migrating database from version 11 to 12 - Adding comprehensive FTS5 search...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Create the search data table
                    spdlog::info("Creating TracksSearchData table...");
                    const char* createSearchDataTable = R"SQL(
                        CREATE TABLE IF NOT EXISTS TracksSearchData (
                            track_id INTEGER PRIMARY KEY,
                            search_content TEXT NOT NULL,
                            FOREIGN KEY (track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
                        );
                    )SQL";

                    if (!m_db.execute(createSearchDataTable))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to create TracksSearchData table: {}", m_db.getLastError());
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create TracksSearchData table: " + m_db.getLastError());
                    }

                    // Populate the search data table with existing data
                    spdlog::info("Populating TracksSearchData with comprehensive search content...");
                    
                    // First check how many tracks we have
                    SqliteStatement trackCountStmt{m_db, "SELECT COUNT(*) FROM Tracks;"};
                    if (trackCountStmt.getNextResult())
                    {
                        spdlog::info("Found {} tracks to process", trackCountStmt.getInt32(0));
                    }
                    
                    const char* populateSearchData = R"SQL(
                        INSERT INTO TracksSearchData (track_id, search_content)
                        SELECT 
                            t.track_id,
                            COALESCE(t.title, '') || ' ' ||
                            COALESCE(t.artist_name, '') || ' ' ||
                            COALESCE(t.album_title, '') || ' ' ||
                            COALESCE(t.filename, '') || ' ' ||
                            COALESCE(f.root_path, '') || ' ' ||
                            COALESCE(f.name, '') || ' ' ||
                            COALESCE(
                                (SELECT GROUP_CONCAT(tags.name, ' ') 
                                 FROM TrackTags tt 
                                 JOIN Tags ON tt.tag_id = Tags.tag_id 
                                 WHERE tt.track_id = t.track_id), 
                                ''
                            ) as search_content
                        FROM Tracks t
                        LEFT JOIN Folders f ON t.folder_id = f.folder_id;
                    )SQL";

                    if (!m_db.execute(populateSearchData))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to populate TracksSearchData: {}", m_db.getLastError());
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to populate TracksSearchData: " + m_db.getLastError());
                    }
                    
                    spdlog::info("Successfully executed TracksSearchData population query");

                    // Create the FTS5 virtual table
                    spdlog::info("Creating FTS5 virtual table with comprehensive search...");
                    const char* createFTS5Table = R"SQL(
                        CREATE VIRTUAL TABLE TracksSearchFTS USING fts5(
                            search_content,
                            content='TracksSearchData',
                            content_rowid='track_id',
                            tokenize='unicode61'
                        );
                    )SQL";

                    if (!m_db.execute(createFTS5Table))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to create FTS5 table: {}", m_db.getLastError());
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create FTS5 table: " + m_db.getLastError());
                    }
                    spdlog::info("Successfully created FTS5 virtual table");
                    
                    // IMPORTANT: With external content tables, we need to explicitly populate the FTS5 index
                    spdlog::info("Rebuilding FTS5 index from content table...");
                    const char* rebuildFTS = "INSERT INTO TracksSearchFTS(TracksSearchFTS) VALUES('rebuild');";
                    if (!m_db.execute(rebuildFTS))
                    {
                        spdlog::error("Failed to rebuild FTS5 index: {}", m_db.getLastError());
                        // Try an alternative method - manually insert all rows
                        spdlog::info("Trying alternative: manually populating FTS5 index...");
                        const char* populateFTS = R"SQL(
                            INSERT INTO TracksSearchFTS(rowid, search_content)
                            SELECT track_id, search_content FROM TracksSearchData;
                        )SQL";
                        
                        if (!m_db.execute(populateFTS))
                        {
                            transaction.rollback();
                            spdlog::error("Failed to populate FTS5 index: {}", m_db.getLastError());
                            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to populate FTS5 index: " + m_db.getLastError());
                        }
                    }
                    spdlog::info("FTS5 index rebuild complete");

                    // Create trigger for track inserts
                    const char* createInsertTrigger = R"SQL(
                        CREATE TRIGGER tracks_search_insert 
                        AFTER INSERT ON Tracks
                        BEGIN
                            INSERT INTO TracksSearchData (track_id, search_content)
                            SELECT 
                                NEW.track_id,
                                COALESCE(NEW.title, '') || ' ' ||
                                COALESCE(NEW.artist_name, '') || ' ' ||
                                COALESCE(NEW.album_title, '') || ' ' ||
                                COALESCE(NEW.filename, '') || ' ' ||
                                COALESCE((SELECT root_path FROM Folders WHERE folder_id = NEW.folder_id), '') || ' ' ||
                                COALESCE((SELECT name FROM Folders WHERE folder_id = NEW.folder_id), '') || ' ' ||
                                COALESCE(
                                    (SELECT GROUP_CONCAT(Tags.name, ' ') 
                                     FROM TrackTags tt 
                                     JOIN Tags ON tt.tag_id = Tags.tag_id 
                                     WHERE tt.track_id = NEW.track_id), 
                                    ''
                                );
                        END;
                    )SQL";

                    // Create trigger for track updates
                    const char* createUpdateTrigger = R"SQL(
                        CREATE TRIGGER tracks_search_update
                        AFTER UPDATE OF title, artist_name, album_title, filename, folder_id ON Tracks
                        BEGIN
                            UPDATE TracksSearchData 
                            SET search_content = (
                                SELECT 
                                    COALESCE(NEW.title, '') || ' ' ||
                                    COALESCE(NEW.artist_name, '') || ' ' ||
                                    COALESCE(NEW.album_title, '') || ' ' ||
                                    COALESCE(NEW.filename, '') || ' ' ||
                                    COALESCE((SELECT root_path FROM Folders WHERE folder_id = NEW.folder_id), '') || ' ' ||
                                    COALESCE((SELECT name FROM Folders WHERE folder_id = NEW.folder_id), '') || ' ' ||
                                    COALESCE(
                                        (SELECT GROUP_CONCAT(Tags.name, ' ') 
                                         FROM TrackTags tt 
                                         JOIN Tags ON tt.tag_id = Tags.tag_id 
                                         WHERE tt.track_id = NEW.track_id), 
                                        ''
                                    )
                            )
                            WHERE track_id = NEW.track_id;
                        END;
                    )SQL";

                    // Create trigger for track deletes
                    const char* createDeleteTrigger = R"SQL(
                        CREATE TRIGGER tracks_search_delete
                        AFTER DELETE ON Tracks
                        BEGIN
                            DELETE FROM TracksSearchData WHERE track_id = OLD.track_id;
                        END;
                    )SQL";

                    // Create trigger for tag changes (insert)
                    const char* createTagInsertTrigger = R"SQL(
                        CREATE TRIGGER tracktags_search_insert
                        AFTER INSERT ON TrackTags
                        BEGIN
                            UPDATE TracksSearchData 
                            SET search_content = (
                                SELECT 
                                    COALESCE(t.title, '') || ' ' ||
                                    COALESCE(t.artist_name, '') || ' ' ||
                                    COALESCE(t.album_title, '') || ' ' ||
                                    COALESCE(t.filename, '') || ' ' ||
                                    COALESCE(f.root_path, '') || ' ' ||
                                    COALESCE(f.name, '') || ' ' ||
                                    COALESCE(
                                        (SELECT GROUP_CONCAT(Tags.name, ' ') 
                                         FROM TrackTags tt 
                                         JOIN Tags ON tt.tag_id = Tags.tag_id 
                                         WHERE tt.track_id = NEW.track_id), 
                                        ''
                                    )
                                FROM Tracks t
                                LEFT JOIN Folders f ON t.folder_id = f.folder_id
                                WHERE t.track_id = NEW.track_id
                            )
                            WHERE track_id = NEW.track_id;
                        END;
                    )SQL";

                    // Create trigger for tag changes (delete)
                    const char* createTagDeleteTrigger = R"SQL(
                        CREATE TRIGGER tracktags_search_delete
                        AFTER DELETE ON TrackTags
                        BEGIN
                            UPDATE TracksSearchData 
                            SET search_content = (
                                SELECT 
                                    COALESCE(t.title, '') || ' ' ||
                                    COALESCE(t.artist_name, '') || ' ' ||
                                    COALESCE(t.album_title, '') || ' ' ||
                                    COALESCE(t.filename, '') || ' ' ||
                                    COALESCE(f.root_path, '') || ' ' ||
                                    COALESCE(f.name, '') || ' ' ||
                                    COALESCE(
                                        (SELECT GROUP_CONCAT(Tags.name, ' ') 
                                         FROM TrackTags tt 
                                         JOIN Tags ON tt.tag_id = Tags.tag_id 
                                         WHERE tt.track_id = OLD.track_id), 
                                        ''
                                    )
                                FROM Tracks t
                                LEFT JOIN Folders f ON t.folder_id = f.folder_id
                                WHERE t.track_id = OLD.track_id
                            )
                            WHERE track_id = OLD.track_id;
                        END;
                    )SQL";

                    // Execute all triggers
                    spdlog::info("Creating database triggers for FTS5 synchronization...");
                    if (!m_db.execute(createInsertTrigger))
                    {
                        spdlog::error("Failed to create insert trigger: {}", m_db.getLastError());
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create insert trigger: " + m_db.getLastError());
                    }
                    
                    if (!m_db.execute(createUpdateTrigger))
                    {
                        spdlog::error("Failed to create update trigger: {}", m_db.getLastError());
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create update trigger: " + m_db.getLastError());
                    }
                    
                    if (!m_db.execute(createDeleteTrigger))
                    {
                        spdlog::error("Failed to create delete trigger: {}", m_db.getLastError());
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create delete trigger: " + m_db.getLastError());
                    }
                    
                    if (!m_db.execute(createTagInsertTrigger))
                    {
                        spdlog::error("Failed to create tag insert trigger: {}", m_db.getLastError());
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create tag insert trigger: " + m_db.getLastError());
                    }
                    
                    if (!m_db.execute(createTagDeleteTrigger))
                    {
                        spdlog::error("Failed to create tag delete trigger: {}", m_db.getLastError());
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create tag delete trigger: " + m_db.getLastError());
                    }
                    
                    spdlog::info("Successfully created all FTS5 sync triggers");

                    // Debug: Check how many rows we have in the search data
                    spdlog::info("Verifying TracksSearchData population...");
                    SqliteStatement checkStmt{m_db, "SELECT COUNT(*) FROM TracksSearchData;"};
                    if (checkStmt.getNextResult())
                    {
                        const auto count = checkStmt.getInt32(0);
                        spdlog::info("TracksSearchData populated with {} rows", count);
                        
                        if (count == 0)
                        {
                            spdlog::error("WARNING: TracksSearchData is empty after population!");
                        }
                    }
                    
                    // Debug: Check a few samples of the search content
                    spdlog::info("Checking sample search content...");
                    SqliteStatement sampleStmt{m_db, "SELECT track_id, search_content FROM TracksSearchData LIMIT 3;"};
                    int sampleCount = 0;
                    while (sampleStmt.getNextResult())
                    {
                        const auto trackId = sampleStmt.getInt32(0);
                        const auto sample = sampleStmt.getText(1);
                        spdlog::info("Track {} search_content (first 300 chars): {}", trackId, 
                                     sample.length() > 300 ? sample.substr(0, 300) + "..." : sample);
                        sampleCount++;
                    }
                    
                    if (sampleCount == 0)
                    {
                        spdlog::error("No sample data found in TracksSearchData!");
                    }
                    
                    // First verify the FTS5 table is properly linked
                    spdlog::info("Verifying FTS5 virtual table linkage...");
                    SqliteStatement verifyStmt{m_db, "SELECT COUNT(*) FROM TracksSearchFTS;"};
                    if (verifyStmt.getNextResult())
                    {
                        const auto ftsCount = verifyStmt.getInt32(0);
                        spdlog::info("TracksSearchFTS virtual table has {} searchable rows", ftsCount);
                        if (ftsCount == 0)
                        {
                            spdlog::error("FTS5 virtual table is empty! Content table might not be linked properly.");
                        }
                    }
                    
                    // Test FTS5 with a simple query
                    spdlog::info("Testing FTS5 with sample queries...");
                    
                    // Test 1: Simple word that should exist
                    SqliteStatement test1{m_db, "SELECT COUNT(*) FROM TracksSearchFTS WHERE TracksSearchFTS MATCH 'mp3';"};
                    if (test1.getNextResult())
                    {
                        spdlog::info("FTS5 test: 'mp3' found {} matches", test1.getInt32(0));
                    }
                    
                    // Test 2: Try a known artist
                    SqliteStatement test2{m_db, "SELECT COUNT(*) FROM TracksSearchFTS WHERE TracksSearchFTS MATCH 'Walker';"};
                    if (test2.getNextResult())
                    {
                        spdlog::info("FTS5 test: 'Walker' found {} matches", test2.getInt32(0));
                    }
                    
                    // Test 3: Path component
                    SqliteStatement test3{m_db, "SELECT COUNT(*) FROM TracksSearchFTS WHERE TracksSearchFTS MATCH 'amazon';"};
                    if (test3.getNextResult())
                    {
                        spdlog::info("FTS5 test: 'amazon' found {} matches", test3.getInt32(0));
                    }
                    
                    // Test 4: Check if the table is actually working
                    SqliteStatement test4{m_db, "SELECT COUNT(*) FROM TracksSearchData WHERE search_content LIKE '%dark%';"};
                    if (test4.getNextResult())
                    {
                        spdlog::info("LIKE test: TracksSearchData has {} rows with 'dark'", test4.getInt32(0));
                    }
                    
                    SqliteStatement test5{m_db, "SELECT COUNT(*) FROM TracksSearchData WHERE search_content LIKE '%ambient%';"};
                    if (test5.getNextResult())
                    {
                        spdlog::info("LIKE test: TracksSearchData has {} rows with 'ambient'", test5.getInt32(0));
                    }

                    if (auto result = setDBSchemaVersion(12); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 12.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 12 with comprehensive FTS5 search.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 12;
            }

            if (currentVersion < 13)
            {
                spdlog::info("Migrating database from version 12 to 13 - Adding Albums table...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Create Albums table
                    const char* createAlbumsTable = R"SQL(
                        CREATE TABLE Albums (
                            album_id INTEGER PRIMARY KEY AUTOINCREMENT,
                            album_artist TEXT,
                            title TEXT NOT NULL,
                            year INTEGER,
                            folder_id INTEGER NOT NULL,
                            genres TEXT,
                            moods TEXT,
                            tags TEXT,
                            bandcamp_url TEXT,
                            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                            FOREIGN KEY (folder_id) REFERENCES Folders(folder_id) ON DELETE CASCADE
                        );
                    )SQL";

                    // Create unique index for album identification
                    const char* createAlbumsIndex = R"SQL(
                        CREATE UNIQUE INDEX idx_albums_title_folder ON Albums(title, folder_id);
                    )SQL";

                    // Add album_id column to Tracks table (nullable for gradual migration)
                    const char* addAlbumIdToTracks = R"SQL(
                        ALTER TABLE Tracks ADD COLUMN album_id INTEGER REFERENCES Albums(album_id) ON DELETE SET NULL;
                    )SQL";

                    // Create index on album_id for efficient joins
                    const char* createTracksAlbumIndex = R"SQL(
                        CREATE INDEX idx_tracks_album_id ON Tracks(album_id);
                    )SQL";

                    // Execute all migration statements
                    if (!m_db.execute(createAlbumsTable))
                    {
                        spdlog::error("Failed to create Albums table: {}", m_db.getLastError());
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create Albums table.");
                    }

                    if (!m_db.execute(createAlbumsIndex))
                    {
                        spdlog::error("Failed to create Albums index: {}", m_db.getLastError());
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create Albums index.");
                    }

                    if (!m_db.execute(addAlbumIdToTracks))
                    {
                        spdlog::error("Failed to add album_id to Tracks: {}", m_db.getLastError());
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to add album_id to Tracks.");
                    }

                    if (!m_db.execute(createTracksAlbumIndex))
                    {
                        spdlog::error("Failed to create Tracks album_id index: {}", m_db.getLastError());
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create Tracks album_id index.");
                    }

                    // Update schema version
                    if (auto result = setDBSchemaVersion(13); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 13.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated database to version 13 with Albums table.");
                    
                    // Note: Album population will be done by C++ logic after migration,
                    // not as part of the SQL migration, to ensure only high-confidence albums are created.
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 13;
            }

            if (currentVersion < 14)
            {
                spdlog::info("Migrating database from version 13 to 14 - Adding bitrate column to Albums table...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Add bitrate column to Albums table
                    const char* addBitrateColumn = R"SQL(
                        ALTER TABLE Albums ADD COLUMN bitrate INTEGER;
                    )SQL";

                    if (!m_db.execute(addBitrateColumn))
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to add bitrate column to Albums table: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to add bitrate column: " + error);
                    }

                    // Update schema version
                    if (auto result = setDBSchemaVersion(14); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 14.");
                    }

                    if (transaction.commit())
                    {
                        spdlog::info("Migration to version 14 completed successfully.");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 14;
            }

            if (currentVersion < 15)
            {
                spdlog::info("Migrating database from version 14 to 15 - Adding actual_path column to Folders table...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Add actual_path column to Folders table to store case-preserving paths
                    const char* addActualPathColumn = R"SQL(
                        ALTER TABLE Folders ADD COLUMN actual_path TEXT;
                    )SQL";

                    if (!m_db.execute(addActualPathColumn))
                    {
                        const auto error{m_db.getLastError()};
                        // Check if column already exists (some databases might have been partially migrated)
                        if (error.find("duplicate column name") == std::string::npos)
                        {
                            spdlog::error("Failed to add actual_path column to Folders table: {}", error);
                            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to add actual_path column: " + error);
                        }
                        else
                        {
                            spdlog::info("actual_path column already exists, continuing migration.");
                        }
                    }

                    // Update schema version
                    if (auto result = setDBSchemaVersion(15); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 15.");
                    }

                    if (transaction.commit())
                    {
                        spdlog::info("Migration to version 15 completed successfully.");
                        spdlog::info("Note: actual_path will be populated during the next library scan.");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 15;
            }

            if (currentVersion < 16)
            {
                spdlog::info("Migrating database from version 15 to 16 - Adding MixMarkers table...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Create MixMarkers table for mix-wide markers
                    const char* createMixMarkersTable = R"SQL(
                        CREATE TABLE IF NOT EXISTS MixMarkers (
                            marker_id INTEGER PRIMARY KEY AUTOINCREMENT,
                            mix_id INTEGER NOT NULL,
                            position_ms INTEGER NOT NULL,
                            comment TEXT NOT NULL,
                            color TEXT,
                            emoji TEXT,
                            created_at INTEGER NOT NULL,
                            updated_at INTEGER NOT NULL,
                            FOREIGN KEY (mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE
                        );
                    )SQL";
                    
                    if (!m_db.execute(createMixMarkersTable))
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to create MixMarkers table: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create MixMarkers table: " + error);
                    }
                    
                    // Create indices for efficient queries
                    const char* createMixMarkersIndices = R"SQL(
                        CREATE INDEX IF NOT EXISTS idx_mix_markers_mix_id ON MixMarkers(mix_id);
                        CREATE INDEX IF NOT EXISTS idx_mix_markers_position ON MixMarkers(mix_id, position_ms);
                    )SQL";
                    
                    if (!m_db.execute(createMixMarkersIndices))
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to create MixMarkers indices: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create MixMarkers indices: " + error);
                    }
                    
                    // Update schema version
                    if (auto result = setDBSchemaVersion(16); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 16.");
                    }
                    
                    if (transaction.commit())
                    {
                        spdlog::info("Migration to version 16 completed successfully. MixMarkers table created.");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 16;
            }

            // Migration to version 17: Add EQPresets table
            if (currentVersion < 17)
            {
                spdlog::info("Migrating database from version 16 to 17 (EQ Presets)...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Create EQPresets table
                    const char* createEQPresetsTable = R"SQL(
                        CREATE TABLE IF NOT EXISTS EQPresets (
                            preset_id INTEGER PRIMARY KEY,
                            name TEXT NOT NULL UNIQUE,
                            is_deletable INTEGER NOT NULL DEFAULT 1,
                            settings_json TEXT NOT NULL
                        );
                    )SQL";
                    
                    if (!m_db.execute(createEQPresetsTable))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to create EQPresets table: {}", m_db.getLastError());
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create EQPresets table: " + m_db.getLastError());
                    }
                    
                    // Through the shared seeder, so what a database that migrated here holds and what
                    // one created from scratch holds are the same rows by construction.
                    if (auto result = seedDefaultEQPresets(); !result.isOk())
                    {
                        transaction.rollback();
                        return result;
                    }


                    if (auto result = setDBSchemaVersion(17); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 17.");
                    }
                    
                    if (transaction.commit())
                    {
                        spdlog::info("Migration to version 17 completed successfully. EQPresets table created with default presets.");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 17;
            }
            // Migration to version 18: Add ReverbPresets table
            if (currentVersion < 18)
            {
                spdlog::info("Migrating database from version 17 to 18 (Reverb Presets)...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Create ReverbPresets table
                    const char* createReverbPresetsTable = R"SQL(
                        CREATE TABLE IF NOT EXISTS ReverbPresets (
                            preset_id INTEGER PRIMARY KEY,
                            name TEXT NOT NULL UNIQUE,
                            is_deletable INTEGER NOT NULL DEFAULT 1,
                            settings_json TEXT NOT NULL,
                            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                        );
                    )SQL";
                    
                    if (!m_db.execute(createReverbPresetsTable))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to create ReverbPresets table: {}", m_db.getLastError());
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to create ReverbPresets table: " + m_db.getLastError());
                    }
                    
                    // Through the shared seeder, for the same reason as the EQ presets above.
                    if (auto result = seedDefaultReverbPresets(); !result.isOk())
                    {
                        transaction.rollback();
                        return result;
                    }


                    // Update schema version
                     if (auto result = setDBSchemaVersion(18); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 18.");
                    }
                    
                    if (transaction.commit())
                    {
                        spdlog::info("Migration to version 18 completed successfully. ReverbPresets table created with factory presets.");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    spdlog::error("Failed to begin transaction for migration to version 18.");
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 18;
            }

            // Migration to version 19: Export Organization System
            if (currentVersion < 19)
            {
                spdlog::info("Migrating database from version 18 to 19 (Export Organization)...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Add new columns to Mixes table for export tracking
                    if (!m_db.execute("ALTER TABLE Mixes ADD COLUMN exported_at INTEGER;"))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to add exported_at column to Mixes table: {}", m_db.getLastError());
                        return DbResult::failure(DbResultStatus::ErrorDB,
                            "Failed to add exported_at column: " + m_db.getLastError());
                    }

                    if (!m_db.execute("ALTER TABLE Mixes ADD COLUMN export_folder TEXT;"))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to add export_folder column to Mixes table: {}", m_db.getLastError());
                        return DbResult::failure(DbResultStatus::ErrorDB,
                            "Failed to add export_folder column: " + m_db.getLastError());
                    }

                    // Create ExportFolders table
                    const char* createExportFoldersTable = R"SQL(
                        CREATE TABLE IF NOT EXISTS ExportFolders (
                            folder_id INTEGER PRIMARY KEY,
                            name TEXT NOT NULL UNIQUE COLLATE NOCASE,
                            display_order INTEGER,
                            created_at INTEGER NOT NULL,
                            description TEXT
                        );
                    )SQL";

                    if (!m_db.execute(createExportFoldersTable))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to create ExportFolders table: {}", m_db.getLastError());
                        return DbResult::failure(DbResultStatus::ErrorDB,
                            "Failed to create ExportFolders table: " + m_db.getLastError());
                    }

                    // Create index for sorting
                    if (!m_db.execute("CREATE INDEX idx_export_folders_order ON ExportFolders(display_order);"))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to create ExportFolders index: {}", m_db.getLastError());
                        return DbResult::failure(DbResultStatus::ErrorDB,
                            "Failed to create ExportFolders index: " + m_db.getLastError());
                    }

                    // Update schema version
                    if (auto result = setDBSchemaVersion(19); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 19.");
                    }

                    if (transaction.commit())
                    {
                        spdlog::info("Successfully migrated to version 19 (Export Organization).");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 19;
            }

            if (currentVersion < 20)
            {
                spdlog::info("Migrating database from version 19 to 20 (WorkingSet Mix Counter)...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Add next_mix_number column to WorkingSets table
                    if (!m_db.execute("ALTER TABLE WorkingSets ADD COLUMN next_mix_number INTEGER DEFAULT 1;"))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to add next_mix_number column to WorkingSets table: {}", m_db.getLastError());
                        return DbResult::failure(DbResultStatus::ErrorDB,
                            "Failed to add next_mix_number column: " + m_db.getLastError());
                    }

                    // Update schema version
                    if (auto result = setDBSchemaVersion(20); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 20.");
                    }

                    if (transaction.commit())
                    {
                        spdlog::info("Successfully migrated to version 20 (WorkingSet Mix Counter).");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 20;
            }

            if (currentVersion < 21)
            {
                spdlog::info("Migrating database from version 20 to 21 (Master Plugin Chain)...");
                if (SqliteTransaction transaction{m_db})
                {
                    const auto createTableSql = R"SQL(
                        CREATE TABLE IF NOT EXISTS MasterChainPlugins (
                            order_index INTEGER PRIMARY KEY,
                            plugin_format TEXT NOT NULL,
                            identifier TEXT NOT NULL,
                            name TEXT,
                            manufacturer TEXT,
                            version TEXT,
                            is_enabled INTEGER NOT NULL DEFAULT 1,
                            state_blob BLOB
                        );
                    )SQL";

                    if (!m_db.execute(createTableSql))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to create MasterChainPlugins table: {}", m_db.getLastError());
                        return DbResult::failure(
                            DbResultStatus::ErrorDB,
                            "Failed to create MasterChainPlugins table: " + m_db.getLastError());
                    }

                    if (!m_db.execute(
                            "CREATE INDEX IF NOT EXISTS idx_masterchain_identifier "
                            "ON MasterChainPlugins(plugin_format, identifier);"))
                    {
                        transaction.rollback();
                        spdlog::error("Failed to create MasterChainPlugins index: {}", m_db.getLastError());
                        return DbResult::failure(
                            DbResultStatus::ErrorDB,
                            "Failed to create MasterChainPlugins index: " + m_db.getLastError());
                    }

                    if (auto result = setDBSchemaVersion(21); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 21.");
                    }

                    if (transaction.commit())
                    {
                        spdlog::info("Successfully migrated to version 21 (Master Plugin Chain).");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 21;
            }

            // Migration to version 22: Remove unused indexes and abandoned MixUndoHistory table
            if (currentVersion < 22)
            {
                spdlog::info("Migrating database from version 21 to 22 (cleanup unused indexes and tables)...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Drop unused indexes (rating and liked_status columns are never used)
                    m_db.execute("DROP INDEX IF EXISTS idx_tracks_rating;");
                    m_db.execute("DROP INDEX IF EXISTS idx_tracks_liked_status;");

                    // Drop abandoned MixUndoHistory table (undo is handled in-memory by UndoManager)
                    m_db.execute("DROP TABLE IF EXISTS MixUndoHistory;");

                    if (auto result = setDBSchemaVersion(22); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 22.");
                    }

                    if (transaction.commit())
                    {
                        spdlog::info("Successfully migrated to version 22 (removed unused indexes and MixUndoHistory).");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 22;
            }

            if (currentVersion < 23)
            {
                spdlog::info("Migrating database from version 22 to 23 (add pending_export_settings to Mixes)...");
                if (SqliteTransaction transaction{m_db})
                {
                    if (!m_db.execute("ALTER TABLE Mixes ADD COLUMN pending_export_settings TEXT;"))
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to add pending_export_settings column.");
                    }

                    if (auto result = setDBSchemaVersion(23); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 23.");
                    }

                    if (transaction.commit())
                    {
                        spdlog::info("Successfully migrated to version 23 (pending_export_settings).");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 23;
            }

            if (currentVersion < 24)
            {
                spdlog::info("Migrating database from version 23 to 24 (de-duplicate tracks; enforce UNIQUE(folder_id, filename))...");
                if (SqliteTransaction transaction{m_db})
                {
                    // Collapse duplicate Tracks rows sharing (folder_id, filename) onto the lowest
                    // track_id, remapping every reference, so the UNIQUE index can be created. Older
                    // libraries accumulated duplicates from overlapping roots + repeated scans.
                    bool ok = true;
                    for (const char *step : trackDedupeSteps)
                    {
                        if (!m_db.execute(step))
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok)
                    {
                        const auto error{m_db.getLastError()};
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "v24 de-duplication migration failed: " + error);
                    }

                    if (auto result = setDBSchemaVersion(24); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 24.");
                    }

                    if (transaction.commit())
                    {
                        spdlog::info("Successfully migrated to version 24 (de-duplicated tracks; UNIQUE(folder_id, filename)).");
                    }
                    else
                    {
                        const auto error{m_db.getLastError()};
                        spdlog::error("Failed to commit migration transaction: {}", error);
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration: " + error);
                    }
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 24;
            }

            if (currentVersion < 25)
            {
                spdlog::info("Migrating database from version 24 to 25 - Keeping the FTS search index in sync...");
                // The Tracks/TrackTags triggers maintain the TracksSearchData content table, but
                // nothing kept the external-content FTS index (TracksSearchFTS) in sync - it was
                // only ever populated by an explicit 'rebuild'. So tracks added/edited by a scan
                // never became searchable. Add triggers on TracksSearchData itself (where old/new
                // search_content are available, exactly what FTS5's external-content 'delete'
                // needs) and rebuild once to repair the existing backlog.
                if (SqliteTransaction transaction{m_db})
                {
                    const char *ftsSyncSteps[] = {
                        R"SQL(CREATE TRIGGER IF NOT EXISTS tracksdata_fts_ai AFTER INSERT ON TracksSearchData BEGIN
                            INSERT INTO TracksSearchFTS(rowid, search_content) VALUES (new.track_id, new.search_content);
                        END;)SQL",
                        R"SQL(CREATE TRIGGER IF NOT EXISTS tracksdata_fts_ad AFTER DELETE ON TracksSearchData BEGIN
                            INSERT INTO TracksSearchFTS(TracksSearchFTS, rowid, search_content) VALUES ('delete', old.track_id, old.search_content);
                        END;)SQL",
                        R"SQL(CREATE TRIGGER IF NOT EXISTS tracksdata_fts_au AFTER UPDATE ON TracksSearchData BEGIN
                            INSERT INTO TracksSearchFTS(TracksSearchFTS, rowid, search_content) VALUES ('delete', old.track_id, old.search_content);
                            INSERT INTO TracksSearchFTS(rowid, search_content) VALUES (new.track_id, new.search_content);
                        END;)SQL",
                        "INSERT INTO TracksSearchFTS(TracksSearchFTS) VALUES('rebuild');",
                    };
                    for (const char *step : ftsSyncSteps)
                    {
                        if (!m_db.execute(step))
                        {
                            const auto error{m_db.getLastError()};
                            transaction.rollback();
                            return DbResult::failure(DbResultStatus::ErrorDB, "v25 FTS-sync migration failed: " + error);
                        }
                    }

                    if (auto result = setDBSchemaVersion(25); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 25.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated to version 25 - FTS index now stays in sync automatically.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 25;
            }

            if (currentVersion < 26)
            {
                spdlog::info("Migrating database from version 25 to 26 - adding the Genres vocabulary...");
                // Albums.genres has existed (unused, empty) since the table was created. The headline
                // genre now written from the mix editor needs a controlled vocabulary to pick from, so
                // add the table and seed it. No Albums data is touched.
                if (SqliteTransaction transaction{m_db})
                {
                    if (!m_db.execute("CREATE TABLE IF NOT EXISTS Genres (genre_id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE);"))
                    {
                        const auto error{m_db.getLastError()};
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "v26 Genres migration failed: " + error);
                    }

                    if (const auto seedResult{seedDefaultGenres()}; !seedResult.isOk())
                    {
                        transaction.rollback();
                        return seedResult;
                    }

                    if (auto result = setDBSchemaVersion(26); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 26.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated to version 26 - Genres vocabulary available.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 26;
            }

            if (currentVersion < 27)
            {
                spdlog::info("Migrating database from version 26 to 27 - adding MixRecovery...");
                // What a mix contained when it was exported, so it survives the loss of what it
                // describes. Nothing is backfilled here: capturing the existing 1109 mixes is a
                // deliberate one-off, not something to do silently inside a migration against a
                // multi-gigabyte library.
                //
                // Table, foreign key and both indexes go in one transaction with the version stamp. A
                // half-applied migration - the table without its indexes, or without the bump - would be
                // worse than none, because the next run would believe the work was done.
                if (SqliteTransaction transaction{m_db})
                {
                    const char *const statements[] = {
                        R"SQL(
        CREATE TABLE IF NOT EXISTS MixRecovery(
            mix_id          INTEGER NOT NULL,
            order_in_mix    INTEGER NOT NULL,
            captured_at     INTEGER NOT NULL,
            mix_name        TEXT NOT NULL,
            track_id        INTEGER,
            artist_name     TEXT,
            album_title     TEXT,
            title           TEXT,
            filename        TEXT,
            folder_path     TEXT,
            duration        INTEGER,
            filesize_bytes  INTEGER,
            bpm             INTEGER,
            mix_data        TEXT,
            PRIMARY KEY (mix_id, order_in_mix),
            FOREIGN KEY (mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE
        );)SQL",
                        "CREATE INDEX IF NOT EXISTS idx_mixrecovery_track ON MixRecovery(track_id);",
                        "CREATE INDEX IF NOT EXISTS idx_mixrecovery_fileident ON MixRecovery(filename, filesize_bytes);",
                    };

                    for (const auto *const statement : statements)
                    {
                        if (!m_db.execute(statement))
                        {
                            const auto error{m_db.getLastError()};
                            transaction.rollback();
                            return DbResult::failure(DbResultStatus::ErrorDB, "v27 MixRecovery migration failed: " + error);
                        }
                    }

                    if (auto result = setDBSchemaVersion(27); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 27.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated to version 27 - MixRecovery available.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 27;
            }

            if (currentVersion < 28)
            {
                spdlog::info("Migrating database from version 27 to 28 - MixRecovery remembers the mix length...");
                // The recovery record described a mix's tracks but not how long the mix was, so anything
                // rendering it had to ask the live mix - which, for a mix edited since, is a different
                // mix. Nullable, because rows written under v27 genuinely do not know.
                if (SqliteTransaction transaction{m_db})
                {
                    if (!m_db.execute("ALTER TABLE MixRecovery ADD COLUMN total_duration INTEGER;"))
                    {
                        const auto error{m_db.getLastError()};
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "v28 MixRecovery migration failed: " + error);
                    }

                    if (auto result = setDBSchemaVersion(28); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 28.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated to version 28 - MixRecovery remembers the mix length.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 28;
            }

            if (currentVersion < 29)
            {
                spdlog::info("Migrating database from version 28 to 29 - MixRecovery can describe a damaged mix...");
                // Until now a mix that had already lost rows was refused rather than recorded, so
                // the mixes most in need of a record were the ones without one. They can now be
                // recorded as partial, which needs a way to say so.
                //
                // NOT NULL DEFAULT 1 rather than nullable: every existing row came from a capture
                // that refused anything incomplete, so "whole" is not a guess about them, it is
                // what the old rule guaranteed.
                if (SqliteTransaction transaction{m_db})
                {
                    if (!m_db.execute("ALTER TABLE MixRecovery ADD COLUMN is_complete INTEGER NOT NULL DEFAULT 1;"))
                    {
                        const auto error{m_db.getLastError()};
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "v29 MixRecovery migration failed: " + error);
                    }

                    if (auto result = setDBSchemaVersion(29); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 29.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated to version 29 - MixRecovery can describe a damaged mix.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 29;
            }

            if (currentVersion < 30)
            {
                spdlog::info("Migrating database from version 29 to 30 - MixRecovery remembers where a track was in the mix...");
                // order_in_mix is half the primary key, so it has to be the position within the
                // record. For a damaged mix that is not the position the mix stored, and the
                // difference between them is the only surviving evidence of where rows were lost.
                //
                // Filled in from order_in_mix for every existing row, which is correct for all of
                // them: an intact record has the two equal by the rule that let it be written, and
                // the partial records that exist so far were written with the mix's own positions
                // copied straight across.
                if (SqliteTransaction transaction{m_db})
                {
                    // Then the record positions are normalised, so that order_in_mix means the same
                    // thing in every row that exists rather than only in rows written from here on.
                    // The partial records written under v29 kept the mix's own positions, gaps and
                    // all, and those are now preserved in the new column instead.
                    //
                    // Two passes, because order_in_mix is half the primary key and renumbering in
                    // place can collide with a row that has not moved yet. The first pass maps every
                    // value to -1-value: still unique within a mix, and disjoint from the range the
                    // second pass writes. The second sets each row to its rank among the mix's source
                    // positions, which are unique per mix because they were the primary key a moment
                    // ago.
                    if (!m_db.execute("ALTER TABLE MixRecovery ADD COLUMN source_order_in_mix INTEGER;") ||
                        !m_db.execute("UPDATE MixRecovery SET source_order_in_mix = order_in_mix;") ||
                        !m_db.execute("UPDATE MixRecovery SET order_in_mix = -1 - order_in_mix;") ||
                        !m_db.execute("UPDATE MixRecovery SET order_in_mix = (SELECT COUNT(*) FROM MixRecovery r2 "
                                      "WHERE r2.mix_id = MixRecovery.mix_id AND r2.source_order_in_mix < MixRecovery.source_order_in_mix);"))
                    {
                        const auto error{m_db.getLastError()};
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "v30 MixRecovery migration failed: " + error);
                    }

                    if (auto result = setDBSchemaVersion(30); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 30.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated to version 30 - MixRecovery remembers where a track was in the mix.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 30;
            }

            if (currentVersion < 31)
            {
                spdlog::info("Migrating database from version 30 to 31 - one Folders row per path...");
                // Folders had no unique index on its path, so a second row for a path that already had
                // one was a legal insert - and the consequence was permanent: buildCacheIfNeeded
                // refuses to finish a cache holding two rows for one path, so from the first duplicate
                // onwards the cache could never be rebuilt, every lookup missed, and every folder
                // touched after that got another row of its own.
                //
                // Adding the index means merging whatever duplicates a library already carries. The
                // losers are folded onto the lowest folder_id for the path: their children are
                // re-parented, their tracks moved, and only then are they deleted. Deleting them first
                // and letting ON DELETE CASCADE sort it out would take the tracks and the mix rows
                // that reference them (MixTracks.track_id cascades) with it.
                if (SqliteTransaction transaction{m_db})
                {
                    const auto runSteps = [this](const auto &steps)
                    {
                        for (const char *step : steps)
                        {
                            if (!m_db.execute(step))
                            {
                                return false;
                            }
                        }
                        return true;
                    };

                    // The blanks become NULLs first, so the reconstruction below has one thing to look
                    // for rather than two.
                    const char *blankSteps[] = {
                        "UPDATE Folders SET root_path = NULL WHERE root_path = '';",
                    };

                    if (!runSteps(blankSteps))
                    {
                        const auto error{m_db.getLastError()};
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "v31 folder de-duplication failed: " + error);
                    }

                    if (const auto backfill{reconstructMissingFolderPaths()}; !backfill.isOk())
                    {
                        transaction.rollback();
                        return backfill;
                    }

                    const char *mapSteps[] = {
                        "CREATE TEMP TABLE _folder_dup_map AS "
                        "SELECT f.folder_id AS old_id, m.canonical_id FROM Folders f "
                        "JOIN (SELECT root_path, MIN(folder_id) AS canonical_id FROM Folders "
                        "WHERE root_path IS NOT NULL GROUP BY root_path HAVING COUNT(*) > 1) m "
                        "ON f.root_path = m.root_path WHERE f.folder_id <> m.canonical_id;",
                        "CREATE INDEX _folder_dup_map_idx ON _folder_dup_map(old_id);",
                    };

                    if (!runSteps(mapSteps))
                    {
                        const auto error{m_db.getLastError()};
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "v31 folder de-duplication failed: " + error);
                    }

                    int64_t duplicateFolders = 0;
                    {
                        SqliteStatement countStmt{m_db, "SELECT COUNT(*) FROM _folder_dup_map"};
                        if (!countStmt.isValid() || !countStmt.getNextResult())
                        {
                            const auto error{m_db.getLastError()};
                            transaction.rollback();
                            return DbResult::failure(DbResultStatus::ErrorDB, "v31 could not count duplicate folders: " + error);
                        }
                        duplicateFolders = countStmt.getInt64(0);
                    }

                    if (duplicateFolders > 0)
                    {
                        spdlog::warn("v31: {} duplicate folder row(s) to merge.", duplicateFolders);

                        // UNIQUE(folder_id, filename) comes off for the move and is put back by
                        // trackDedupeSteps below: two rows for one path can each hold the same
                        // filename, and both have to survive the move long enough to be collapsed
                        // onto one track_id with their mix and working set rows remapped.
                        //
                        // Albums are merged before they are moved. The index on (title, folder_id) is
                        // unique, so two albums of one title cannot share the keeper folder - and the
                        // one that would have to give way is not dropped: what the survivor has no
                        // value for is taken from it, then its tracks are pointed at the survivor, and
                        // only then is it deleted. Letting it cascade away with its folder instead
                        // would take the user's genres, moods, tags and bandcamp link with it and set
                        // every referencing Tracks.album_id to NULL, which nothing later restores - the
                        // album pass in buildCacheIfNeeded creates a row where there is none, and there
                        // would be one.
                        //
                        // Albums of different titles are left alone. A folder is allowed to hold
                        // several, so co-locating them is not a collision to resolve.
                        //
                        // Where both have a value, the survivor keeps its own. There is no merge rule
                        // for two different free-text genre lists that is better than picking one.
                        const char *mergeSteps[] = {
                            "UPDATE Folders SET parent_id = (SELECT canonical_id FROM _folder_dup_map WHERE old_id = Folders.parent_id) "
                            "WHERE parent_id IN (SELECT old_id FROM _folder_dup_map);",
                            "DROP INDEX IF EXISTS idx_tracks_parent_filename;",
                            "UPDATE Tracks SET folder_id = (SELECT canonical_id FROM _folder_dup_map WHERE old_id = Tracks.folder_id) "
                            "WHERE folder_id IN (SELECT old_id FROM _folder_dup_map);",
                            // Every album that is about to share a folder, with the folder it is headed
                            // for. Built before anything moves, so which album survives a title
                            // collision is decided here and not by the order an UPDATE happens to visit
                            // rows in: it is the lowest album_id among the albums that would share a
                            // (folder, title), the same rule the folder and track merges use. With no
                            // album on the keeper and same-title albums on two losers, that is still
                            // one determinate answer.
                            "CREATE TEMP TABLE _album_group AS "
                            "SELECT a.album_id, a.title, "
                            "COALESCE((SELECT d.canonical_id FROM _folder_dup_map d WHERE d.old_id = a.folder_id), a.folder_id) AS target_folder "
                            "FROM Albums a WHERE a.folder_id IN (SELECT old_id FROM _folder_dup_map) "
                            "OR a.folder_id IN (SELECT canonical_id FROM _folder_dup_map);",
                            "CREATE TEMP TABLE _album_dup_map AS "
                            "SELECT g.album_id AS old_id, m.canonical_id FROM _album_group g "
                            "JOIN (SELECT target_folder, title, MIN(album_id) AS canonical_id FROM _album_group "
                            "GROUP BY target_folder, title) m ON g.target_folder = m.target_folder AND g.title = m.title "
                            "WHERE g.album_id <> m.canonical_id;",
                            "CREATE INDEX _album_dup_map_idx ON _album_dup_map(old_id);",
                        };

                        // One statement per column, all the same shape, generated rather than written
                        // out seven times. "No value" is spelled three ways here and the guard has to
                        // know all of them: NULL, '', and '[]' - genres, moods and tags are JSON
                        // arrays, and vectorToJsonArray writes an empty one as '[]'. It is the same
                        // test SqliteAlbumManager uses when it asks whether an album carries genres.
                        // On the columns that hold no JSON the '[]' arm is simply never true.
                        static constexpr const char *mergeableAlbumColumns[] = {
                            "album_artist", "year", "genres", "moods", "tags", "bandcamp_url", "bitrate"};

                        const auto mergeAlbumFields = [this]()
                        {
                            for (const auto *column : mergeableAlbumColumns)
                            {
                                const auto sql{std::format("UPDATE Albums SET {0} = COALESCE("
                                                           "(SELECT o.{0} FROM Albums o JOIN _album_dup_map m ON o.album_id = m.old_id "
                                                           "WHERE m.canonical_id = Albums.album_id "
                                                           "AND o.{0} IS NOT NULL AND o.{0} <> '' AND o.{0} <> '[]' "
                                                           "ORDER BY o.album_id LIMIT 1), {0}) "
                                                           "WHERE ({0} IS NULL OR {0} = '' OR {0} = '[]') "
                                                           "AND album_id IN (SELECT canonical_id FROM _album_dup_map);",
                                    column)};
                                if (!m_db.execute(sql.c_str()))
                                {
                                    return false;
                                }
                            }
                            return true;
                        };

                        const char *albumTailSteps[] = {
                            "UPDATE Tracks SET album_id = (SELECT canonical_id FROM _album_dup_map WHERE old_id = Tracks.album_id) "
                            "WHERE album_id IN (SELECT old_id FROM _album_dup_map);",
                            "DELETE FROM Albums WHERE album_id IN (SELECT old_id FROM _album_dup_map);",
                            // Only now, with every collision already resolved, do the survivors move.
                            // A plain UPDATE: nothing can be in its way, because anything that would
                            // have been was just merged into it.
                            "UPDATE Albums SET folder_id = (SELECT canonical_id FROM _folder_dup_map WHERE old_id = Albums.folder_id) "
                            "WHERE folder_id IN (SELECT old_id FROM _folder_dup_map);",
                            "DROP TABLE _album_dup_map;",
                            "DROP TABLE _album_group;",
                            "DELETE FROM Folders WHERE folder_id IN (SELECT old_id FROM _folder_dup_map);",
                        };

                        // The dedupe steps were written for a database that migrated its way up, and
                        // they touch two tables that only the v4 and v12 rungs create: TrackMarkers
                        // and the FTS index. initialSqlStatements creates neither, so every database
                        // created from scratch since is missing both - recorded in tasks.md, and not
                        // this rung's to fix. What this rung has to do is not fail on it.
                        const auto runDedupe = [this]()
                        {
                            const bool haveMarkers{m_db.doesTableExist("TrackMarkers")};
                            const bool haveSearchIndex{m_db.doesTableExist("TracksSearchFTS")};
                            for (const char *step : trackDedupeSteps)
                            {
                                const std::string_view sql{step};
                                if ((!haveMarkers && sql.find("TrackMarkers") != std::string_view::npos) ||
                                    (!haveSearchIndex && sql.find("TracksSearchFTS") != std::string_view::npos))
                                {
                                    spdlog::warn("v31: skipping a de-duplication step, this database has no such table: {}", sql);
                                    continue;
                                }

                                if (!m_db.execute(step))
                                {
                                    return false;
                                }
                            }
                            return true;
                        };

                        if (!runSteps(mergeSteps) || !mergeAlbumFields() || !runSteps(albumTailSteps) || !runDedupe())
                        {
                            const auto error{m_db.getLastError()};
                            transaction.rollback();
                            return DbResult::failure(DbResultStatus::ErrorDB, "v31 folder merge failed: " + error);
                        }
                    }

                    // Dropped before it is created, the way v24 does it: a database stamped below 31
                    // is not supposed to carry this index, but if one ever does, recreating it is how
                    // this rung guarantees the index it means rather than accepting whatever is there
                    // under that name.
                    const char *indexSteps[] = {
                        "DROP TABLE _folder_dup_map;",
                        "DROP INDEX IF EXISTS idx_folders_root_path;",
                        "CREATE UNIQUE INDEX idx_folders_root_path ON Folders(root_path);",
                    };

                    if (!runSteps(indexSteps))
                    {
                        const auto error{m_db.getLastError()};
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "v31 could not create the unique folder path index: " + error);
                    }

                    if (auto result = setDBSchemaVersion(31); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 31.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated to version 31 - one Folders row per path.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 31;
            }

            if (currentVersion < 32)
            {
                spdlog::info("Migrating database from version 31 to 32 - a new database gets what the ladder used to add...");
                // The repair half of the divergence convergenceSqlStatements documents. Two kinds of
                // database arrive here and both are handled by the same statements: one that migrated
                // up the ladder and already has all of it, and one that was created from scratch at
                // v22 or later and has none of it. Everything is IF NOT EXISTS, so the first is a
                // no-op and the second is repaired.
                if (SqliteTransaction transaction{m_db})
                {
                    for (const char *sql : convergenceSqlStatements)
                    {
                        if (!m_db.execute(sql))
                        {
                            const auto error{m_db.getLastError()};
                            transaction.rollback();
                            return DbResult::failure(DbResultStatus::ErrorDB, "v32 schema convergence failed: " + error);
                        }
                    }

                    // A library created without the search tables has been scanned without them too, so
                    // the content table has to be filled from what is already in Tracks - the triggers
                    // above only maintain it from here on. OR IGNORE because a database that has had
                    // them all along has these rows already.
                    const char *searchBackfill[] = {
                        R"SQL(
                        INSERT OR IGNORE INTO TracksSearchData (track_id, search_content)
                        SELECT
                            t.track_id,
                            COALESCE(t.title, '') || ' ' ||
                            COALESCE(t.artist_name, '') || ' ' ||
                            COALESCE(t.album_title, '') || ' ' ||
                            COALESCE(t.filename, '') || ' ' ||
                            COALESCE(f.root_path, '') || ' ' ||
                            COALESCE(f.name, '') || ' ' ||
                            COALESCE(
                                (SELECT GROUP_CONCAT(Tags.name, ' ')
                                 FROM TrackTags tt
                                 JOIN Tags ON tt.tag_id = Tags.tag_id
                                 WHERE tt.track_id = t.track_id),
                                ''
                            )
                        FROM Tracks t
                        LEFT JOIN Folders f ON t.folder_id = f.folder_id;)SQL",
                        "INSERT INTO TracksSearchFTS(TracksSearchFTS) VALUES('rebuild');",
                        // Divergence in the other direction, and the reason to walk the whole ladder
                        // rather than only the tables that were missing: the v6 rung named this index
                        // idx_mixtracks_order and the schema a new database is built from names the
                        // same two columns idx_mixtracks_mix_order, so a migrated library carries the
                        // old name and lacks idx_mixtracks_track entirely. Both are created above by
                        // initialSqlStatements on a new database; here the old one goes.
                        "DROP INDEX IF EXISTS idx_mixtracks_order;",
                        "CREATE INDEX IF NOT EXISTS idx_mixtracks_mix_order ON MixTracks(mix_id, order_in_mix);",
                        "CREATE INDEX IF NOT EXISTS idx_mixtracks_track ON MixTracks(track_id);",
                    };

                    // Before the steps above run: the rebuild drops MixTracks, which takes its indexes
                    // with it, and the last three of those steps are what put them back under the
                    // names the current schema uses.
                    if (const auto rebuilt{rebuildMixTracksWithoutPrimaryKey()}; !rebuilt.isOk())
                    {
                        transaction.rollback();
                        return rebuilt;
                    }

                    for (const char *sql : searchBackfill)
                    {
                        if (!m_db.execute(sql))
                        {
                            const auto error{m_db.getLastError()};
                            transaction.rollback();
                            return DbResult::failure(DbResultStatus::ErrorDB, "v32 search backfill failed: " + error);
                        }
                    }

                    if (auto result = seedDefaultEQPresets(); !result.isOk())
                    {
                        transaction.rollback();
                        return result;
                    }

                    if (auto result = seedDefaultReverbPresets(); !result.isOk())
                    {
                        transaction.rollback();
                        return result;
                    }

                    if (auto result = setDBSchemaVersion(32); !result.isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update schema version to 32.");
                    }

                    if (!transaction.commit())
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit migration transaction.");
                    }
                    spdlog::info("Successfully migrated to version 32 - a new database gets what the ladder used to add.");
                }
                else
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
                }
                currentVersion = 32;
            }

            return DbResult::success();
        }

        DbResult SqliteTrackDatabase::saveTrackInfo(TrackInfo &trackInfo)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for saveTrackInfo.");
            }
            m_lastErrorMessage.clear();

            if (trackInfo.filename.empty())
            {
                return DbResult::failure(DbResultStatus::ErrorGeneric, "Filename cannot be empty.");
            }

            // For simplicity, we'll use a single transaction for INSERT or
            // UPDATE You might want finer-grained transaction control in a real
            // app
            if (!m_db.execute("BEGIN TRANSACTION;"))
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin transaction: " + m_db.getLastError());
            }

            bool success = false;
            if (trackInfo.trackId == -1)
            { // INSERT
                const std::string sql = R"SQL(
    INSERT INTO Tracks (folder_id, filename, last_modified_fs, filesize_bytes, date_added, last_scanned,
                        title, artist_name, album_title, album_artist_name, track_number, disc_number, year, 
                        duration, samplerate, channels, bitrate, codec_name,
                        bpm, intro_end, outro_start, key_string, beat_locations_json,
                        rating, liked_status, play_count, last_played,
                        internal_content_hash, user_notes, is_missing, status) 
    VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);
)SQL";

                SqliteStatement stmt{m_db, sql};
                if (stmt.isValid() && bindTrackInfoToStatement(stmt, trackInfo, false) && stmt.execute())
                {
                    trackInfo.trackId = m_db.getLastInsertRowId();
                    spdlog::debug("Inserted track ID: {}, Path: {}", trackInfo.trackId, pathToString(trackInfo.reconstructFullPath()));
                    success = true;
                }
                m_cachedTotalTrackCount = 0;
            }
            else
            { // UPDATE
                const std::string sql = R"SQL(
    UPDATE Tracks SET folder_id=?, filename=?, last_modified_fs=?, filesize_bytes=?, date_added=?, last_scanned=?,
                      title=?, artist_name=?, album_title=?, album_artist_name=?, track_number=?, disc_number=?, year=?, 
                      duration=?, samplerate=?, channels=?, bitrate=?, codec_name=?,
                      bpm=?, intro_end=?, outro_start=?, key_string=?, beat_locations_json=?,
                      rating=?, liked_status=?, play_count=?, last_played=?,
                      internal_content_hash=?, user_notes=?, is_missing=?, status=?
    WHERE track_id = ?;
)SQL";

                SqliteStatement stmt{m_db, sql};
                if (stmt.isValid() && bindTrackInfoToStatement(stmt, trackInfo, true) && stmt.execute())
                {
                    spdlog::debug("Updated track ID: {}", trackInfo.trackId);
                    success = true;
                }
            }

            if (success)
            {
                updateTrackTagsFromInsideTransaction(trackInfo.trackId,
                    trackInfo.tag_ids); // Update tags after insert
                if (!m_db.execute("COMMIT;"))
                {
                    m_lastErrorMessage = "Failed to commit transaction: " + m_db.getLastError();
                    // Attempt to rollback, though the main operation might have
                    // already written
                    m_db.execute("ROLLBACK;");
                    return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                }
                return DbResult::success();
            }
            else
            {
                m_lastErrorMessage = "SaveTrackInfo failed: " + m_db.getLastError(); // Get last error from SqliteDatabase
                m_db.execute("ROLLBACK;");                                           // Rollback on any failure
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }
        }

        DbResult SqliteTrackDatabase::updateScannedTrackData(const TrackInfo &trackInfo, ScannedFields fields)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for updateScannedTrackData.");
            }
            m_lastErrorMessage.clear();

            if (trackInfo.trackId <= 0)
            {
                return DbResult::failure(DbResultStatus::ErrorGeneric, "updateScannedTrackData needs an existing track id.");
            }

            if (trackInfo.filename.empty() || trackInfo.folderId <= 0)
            {
                // Both are half of the unique index, so writing a default over either would move the row
                // to a name no file has.
                return DbResult::failure(DbResultStatus::ErrorGeneric, "updateScannedTrackData needs a folder and a filename.");
            }

            // Immediate, because the row is read back through getChangesCount below and the tag insert
            // depends on the update having landed.
            SqliteTransaction transaction{m_db, TransactionMode::Immediate};
            if (!transaction)
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin transaction: " + m_db.getLastError());
            }

            // The columns a scan owns, and no others.
            //
            // Absent by intent, not by oversight: date_added (the library's history, not the file's),
            // bpm, intro_end, outro_start, key_string, beat_locations_json (analysis, which costs minutes
            // per track and no scanner produces), album_id, status, internal_content_hash, and
            // album_artist_name, disc_number and codec_name, which no scanner currently fills in - so
            // writing them would mean writing an empty string over whatever is there.
            //
            // Built from the mask rather than picked from a set of ready-made statements, because the
            // two halves of the metadata fail independently: tags without audio properties is a real
            // outcome, and so is the reverse. The assignments and the binds below are two halves of one
            // list and have to stay in step.
            const bool withTags{includes(fields, ScannedFields::Tags)};
            const bool withAudioProperties{includes(fields, ScannedFields::AudioProperties)};

            // Always written. The scanner reads these from the directory entry, not from the file, so
            // they are known whenever there is a file at all - including one nothing could open.
            std::string assignments{"folder_id=?, filename=?, last_modified_fs=?, filesize_bytes=?, last_scanned=?, is_missing=?"};
            if (withTags)
            {
                assignments += ", title=?, artist_name=?, album_title=?, track_number=?, year=?";
            }
            if (withAudioProperties)
            {
                assignments += ", duration=?, samplerate=?, channels=?, bitrate=?";
            }

            SqliteStatement stmt{m_db, std::format("UPDATE Tracks SET {} WHERE track_id = ?;", assignments)};
            bool ok = stmt.isValid();
            ok = ok && stmt.addParam(trackInfo.folderId);
            ok = ok && stmt.addParam(trackInfo.filename);
            ok = ok && stmt.addParam(timestampToInt64(trackInfo.last_modified_fs));
            ok = ok && stmt.addParam(static_cast<int64_t>(trackInfo.filesize_bytes));
            ok = ok && stmt.addParam(timestampToInt64(trackInfo.last_scanned));
            ok = ok && stmt.addParam(trackInfo.is_missing ? 1 : 0);
            if (withTags)
            {
                ok = ok && stmt.addParam(trackInfo.title);
                ok = ok && stmt.addParam(trackInfo.artist_name);
                ok = ok && stmt.addParam(trackInfo.album_title);
                ok = ok && stmt.addParam(trackInfo.track_number);
                ok = ok && stmt.addParam(trackInfo.year);
            }
            if (withAudioProperties)
            {
                ok = ok && stmt.addParam(durationToInt64(trackInfo.duration));
                ok = ok && stmt.addParam(trackInfo.samplerate);
                ok = ok && stmt.addParam(trackInfo.channels);
                ok = ok && stmt.addParam(trackInfo.bitrate);
            }
            ok = ok && stmt.addParam(trackInfo.trackId);
            if (!ok || !stmt.execute())
            {
                m_lastErrorMessage = "updateScannedTrackData failed: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }

            if (m_db.getChangesCount() != 1)
            {
                m_lastErrorMessage = std::format("no track {} to update", trackInfo.trackId);
                return DbResult::failure(DbResultStatus::ErrorNotFound, m_lastErrorMessage);
            }

            // Added, not replaced. See the interface for why: a rescan may learn a genre, and may not
            // unlearn one the user assigned. INSERT OR IGNORE against the (track_id, tag_id) primary key
            // is the union, without reading the existing set first.
            //
            // Skipped when the tags were not read: the genre list is then empty because the read failed,
            // not because the file has no genres.
            const std::vector<TagId> genresToAdd{withTags ? trackInfo.tag_ids : std::vector<TagId>{}};
            for (const auto tagId : genresToAdd)
            {
                SqliteStatement tagStmt{m_db, "INSERT OR IGNORE INTO TrackTags (track_id, tag_id) VALUES (?,?);"};
                if (!tagStmt.isValid() || !tagStmt.addParam(trackInfo.trackId) || !tagStmt.addParam(tagId) || !tagStmt.execute())
                {
                    m_lastErrorMessage = "updateScannedTrackData could not add a genre: " + m_db.getLastError();
                    return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                }
            }

            if (!transaction.commit())
            {
                m_lastErrorMessage = "Failed to commit transaction: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }

            return DbResult::success();
        }

        std::optional<TrackInfo> SqliteTrackDatabase::getTrackById(TrackId trackId) const
        {
            if (!isOpen())
            {
                return std::nullopt;
            }
            m_lastErrorMessage.clear(); // mutable m_lastErrorMessage

            SqliteStatement stmt{m_db, "SELECT * FROM Tracks WHERE track_id = ?;"};
            if (!stmt.isValid())
            {
                m_lastErrorMessage = m_db.getLastError();
                return std::nullopt;
            }
            stmt.addParam(trackId);
            if (stmt.getNextResult())
            {
                auto result{trackInfoFromStatement(stmt)};
                result.tag_ids = getTrackTags(result.trackId);
                return result;
            }
            // If getNextResult returns false, it might be an error or just no
            // rows. Your SqliteStatement::getNextResult() should distinguish
            // this or m_db.getLastError() should be checked.
            if (!m_db.getLastError().empty() && m_db.getLastError().find("SQLITE_DONE") == std::string::npos)
            { // Crude check
                m_lastErrorMessage = m_db.getLastError();
            }
            return std::nullopt;
        }

        std::optional<TrackInfo> SqliteTrackDatabase::getNextTrackForBpmAnalysis() const
        {
            if (!isOpen())
            {
                return std::nullopt;
            }
            m_lastErrorMessage.clear(); // mutable m_lastErrorMessage

            // --- PRIORITY 1: Un-analyzed tracks that are part of ANY mix project ---
            std::string sql_priority1 = R"SQL(
                SELECT T.* FROM Tracks T
                JOIN MixTracks MT ON T.track_id = MT.track_id
                WHERE (T.bpm IS NULL OR T.bpm <= 0)
                AND T.status != 'bad_format'
                LIMIT 1;
            )SQL";

            SqliteStatement stmt1{m_db, sql_priority1};
            if (stmt1.getNextResult())
            {
                return trackInfoFromStatement(stmt1); // Found a high-priority track
            }

            // --- PRIORITY 2: Any other un-analyzed track ---
            std::string sql_priority2 = "SELECT * FROM Tracks WHERE (bpm IS NULL OR bpm <= 0) AND status != 'bad_format' LIMIT 1;";
            SqliteStatement stmt2{m_db, sql_priority2};
            if (stmt2.getNextResult())
            {
                return trackInfoFromStatement(stmt2); // Found a regular-priority track
            }
            return std::nullopt;
        }

        // --- Generic single field update helper ---
        template <typename T>
        DbResult SqliteTrackDatabase::updateSingleTrackField(
            TrackId trackId, const std::string &columnName, T value, std::function<bool(SqliteStatement &, T)> binder)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for update.");
            }
            m_lastErrorMessage.clear();
            std::string sql = "UPDATE Tracks SET " + columnName + " = ? WHERE track_id = ?;";
            SqliteStatement stmt{m_db, sql};

            if (!stmt.isValid())
            {
                m_lastErrorMessage = "Prepare failed for " + columnName + " update: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }
            if (!binder(stmt, value))
            { // Call custom binder
                m_lastErrorMessage = "Bind failed for " + columnName + " update: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }
            stmt.addParam(trackId);

            if (stmt.execute())
            {
                // Check sqlite3_changes(m_db.getInternalHandle()) if you need
                // to confirm rows affected
                spdlog::debug("Updated {} for track_id: {}", columnName, trackId);
                return DbResult::success();
            }
            m_lastErrorMessage = "Execute failed for " + columnName + " update: " + m_db.getLastError();
            return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
        }

        DbResult SqliteTrackDatabase::updateTrackBpm(TrackId trackId, const AudioMetadata &am)
        {
            // This implementation now just calls the batch update for a single item.
            std::vector<std::pair<TrackId, AudioMetadata>> results;
            results.emplace_back(trackId, am);
            return updateTrackBpm(results);
        }

        DbResult SqliteTrackDatabase::updateTrackBpm(const std::vector<std::pair<TrackId, AudioMetadata>> &results)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for update.");
            }
            if (results.empty())
            {
                return DbResult::success();
            }
            m_lastErrorMessage.clear();

            if (SqliteTransaction transaction{m_db})
            {
                // Clear intro_end/outro_start: legacy data from broken analysis should not persist
                SqliteStatement stmt{m_db, "UPDATE Tracks SET bpm=?, intro_end=NULL, outro_start=NULL WHERE track_id = ?;"};
                if (!stmt.isValid())
                {
                    m_lastErrorMessage = "Prepare failed for updateTrackBpm(): " + m_db.getLastError();
                    transaction.rollback();
                    return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                }

                for (const auto &[trackId, am] : results)
                {
                    stmt.reset();
                    stmt.addParam(static_cast<int64_t>(am.bpm * 100)); // Store as integer (centiBPM)
                    stmt.addParam(trackId);

                    if (!stmt.execute())
                    {
                        m_lastErrorMessage = "Execute failed for updateTrackBpm() on track " + std::to_string(trackId) + ": " + m_db.getLastError();
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                    }
                }

                if (!transaction.commit())
                {
                    m_lastErrorMessage = "Failed to commit transaction for batch BPM update: " + m_db.getLastError();
                    return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                }

                spdlog::debug("Batch updated BPM for {} tracks.", results.size());
                return DbResult::success();
            }
            else
            {
                m_lastErrorMessage = "Failed to begin transaction for batch BPM update: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }
        }

        DbResult SqliteTrackDatabase::updateTrackEnergyData(TrackId trackId, Duration_t introEnd,
                                                            Duration_t outroStart, const std::string& json)
        {
            std::vector<std::tuple<TrackId, Duration_t, Duration_t, std::string>> results;
            results.emplace_back(trackId, introEnd, outroStart, json);
            return updateTrackEnergyData(results);
        }

        DbResult SqliteTrackDatabase::updateTrackEnergyData(
            const std::vector<std::tuple<TrackId, Duration_t, Duration_t, std::string>>& results)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for update.");
            }
            if (results.empty())
            {
                return DbResult::success();
            }
            m_lastErrorMessage.clear();

            if (SqliteTransaction transaction{m_db})
            {
                SqliteStatement stmt{m_db,
                    "UPDATE Tracks SET intro_end=?, outro_start=?, beat_locations_json=? WHERE track_id=?;"};
                if (!stmt.isValid())
                {
                    m_lastErrorMessage = "Prepare failed for updateTrackEnergyData(): " + m_db.getLastError();
                    transaction.rollback();
                    return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                }

                for (const auto& [trackId, introEnd, outroStart, json] : results)
                {
                    stmt.reset();
                    stmt.addParam(durationToInt64(introEnd));
                    stmt.addParam(durationToInt64(outroStart));
                    stmt.addParam(json);
                    stmt.addParam(trackId);

                    if (!stmt.execute())
                    {
                        m_lastErrorMessage = "Execute failed for updateTrackEnergyData() on track " +
                                             std::to_string(trackId) + ": " + m_db.getLastError();
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                    }
                }

                if (!transaction.commit())
                {
                    m_lastErrorMessage = "Failed to commit transaction for energy data update: " + m_db.getLastError();
                    return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                }

                spdlog::debug("Updated energy data for {} tracks.", results.size());
                return DbResult::success();
            }
            else
            {
                m_lastErrorMessage = "Failed to begin transaction for energy data update: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }
        }

        DbResult SqliteTrackDatabase::updateTrackRating(TrackId trackId, int rating)
        {
            return updateSingleTrackField<int>(trackId,
                "rating",
                rating,
                [](SqliteStatement &s, int val)
                {
                    return s.addParam(val);
                });
        }

        DbResult SqliteTrackDatabase::updateTrackLikedStatus(TrackId trackId, int likedStatus)
        {
            return updateSingleTrackField<int>(trackId,
                "liked_status",
                likedStatus,
                [](SqliteStatement &s, int val)
                {
                    return s.addParam(val);
                });
        }

        DbResult SqliteTrackDatabase::updateTrackUserNotes(TrackId trackId, const std::string &notes)
        {
            return updateSingleTrackField<const std::string &>(trackId,
                "user_notes",
                notes,
                [](SqliteStatement &s, const std::string &val)
                {
                    return s.addParam(val);
                });
        }

        DbResult SqliteTrackDatabase::updateTrackStatus(TrackId trackId, TrackStatus status)
        {
            std::string statusStr;
            switch (status)
            {
            case TrackStatus::Unknown:
                statusStr = "unknown";
                break;
            case TrackStatus::Ok:
                statusStr = "ok";
                break;
            case TrackStatus::BadFormat:
                statusStr = "bad_format";
                break;
            }

            return updateSingleTrackField<const std::string &>(trackId,
                "status",
                statusStr,
                [](SqliteStatement &s, const std::string &val)
                {
                    return s.addParam(val);
                });
        }

        DbResult SqliteTrackDatabase::incrementTrackPlayCount(TrackId trackId)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open");
            }
            m_lastErrorMessage.clear();
            std::string sql = "UPDATE Tracks SET play_count = play_count + 1, "
                              "last_played = ? WHERE track_id = ?;";
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                m_lastErrorMessage = "Prepare failed for incrementPlayCount: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }

            stmt.addParam(timestampToInt64(std::chrono::system_clock::now()));
            stmt.addParam(trackId);

            if (stmt.execute())
            {
                return DbResult::success();
            }
            m_lastErrorMessage = "Execute failed for incrementPlayCount: " + m_db.getLastError();
            return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
        }

        DbResult SqliteTrackDatabase::updateTrackFilesystemInfo(TrackId trackId, Timestamp_t lastModified, std::uintmax_t filesize)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open");
            }
            m_lastErrorMessage.clear();
            std::string sql = "UPDATE Tracks SET last_modified_fs = ?, "
                              "filesize_bytes = ? WHERE track_id = ?;";
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                m_lastErrorMessage = "Prepare failed for updateFSInfo: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }

            stmt.addParam(timestampToInt64(lastModified));
            stmt.addParam(static_cast<int64_t>(filesize));
            stmt.addParam(trackId);

            if (stmt.execute())
            {
                return DbResult::success();
            }
            m_lastErrorMessage = "Execute failed for updateFSInfo: " + m_db.getLastError();
            return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
        }

        DbResult SqliteTrackDatabase::setTrackPathMissing(TrackId trackId, bool isMissing)
        {
            return updateSingleTrackField<int>(trackId,
                "is_missing",
                isMissing ? 1 : 0,
                [](SqliteStatement &s, int val)
                {
                    return s.addParam(val);
                });
        }

        DbResult SqliteTrackDatabase::removeTracks(const std::vector<TrackId> &trackIds)
        {
            if (trackIds.empty())
            {
                return DbResult::success();
            }

            // Immediate: this reads which mixes are affected, then writes based on that answer. A
            // deferred transaction takes its write lock at the first write, so another writer can get
            // in between the two and turn the upgrade into a failure - or, on this connection, join the
            // transaction outright.
            if (SqliteTransaction transaction{m_db, TransactionMode::Immediate})
            {
                // Which mixes are about to change, captured before the rows go. MixTracks.track_id
                // cascades, so once the tracks are deleted there is nothing left to say which mixes
                // were affected - and their stored length would go on describing tracks they no longer
                // contain. Deleting a track is a mix mutation, even though nothing here mentions mixes.
                std::vector<MixId> affectedMixes;
                if (!findMixesContainingTracks(m_db, trackIds, affectedMixes).isOk())
                {
                    transaction.rollback();
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to find the mixes affected by the deletion.");
                }

                SqliteStatement stmt{m_db, "DELETE FROM Tracks WHERE track_id = ?"};

                for (const auto &trackId : trackIds)
                {
                    stmt.addParam(trackId);
                    if (!stmt.execute())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, std::format("Failed to delete track with ID: {}", trackId));
                    }
                    stmt.reset();
                }

                for (const auto mixId : affectedMixes)
                {
                    if (!refreshMixSummary(m_db, transaction, mixId).isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, std::format("Failed to refresh the summary of mix {} after deletion.", mixId));
                    }
                }

                if (!transaction.commit())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit transaction.");
                }
                return DbResult::success();
            }
            else
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin migration transaction.");
            }
        }

        DbResult SqliteTrackDatabase::deleteTracksFromLibrary(const std::vector<TrackId> &trackIds)
        {
            if (trackIds.empty())
            {
                return DbResult::success();
            }

            // Immediate, for the same reason as removeTracks above: a read that decides what to write.
            if (SqliteTransaction transaction{m_db, TransactionMode::Immediate})
            {
                // Which mixes are about to change, captured before the rows go. MixTracks.track_id
                // cascades, so once the tracks are deleted there is nothing left to say which mixes
                // were affected - and their stored length would go on describing tracks they no longer
                // contain. Deleting a track is a mix mutation, even though nothing here mentions mixes.
                std::vector<MixId> affectedMixes;
                if (!findMixesContainingTracks(m_db, trackIds, affectedMixes).isOk())
                {
                    transaction.rollback();
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to find the mixes affected by the deletion.");
                }

                // Step 1: Delete from MixTracks table
                SqliteStatement stmtMixTracks{m_db, "DELETE FROM MixTracks WHERE track_id = ?"};
                for (const auto &trackId : trackIds)
                {
                    stmtMixTracks.addParam(trackId);
                    if (!stmtMixTracks.execute())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, std::format("Failed to delete track {} from MixTracks", trackId));
                    }
                    stmtMixTracks.reset();
                }

                // Step 2: Delete from WorkingSetTracks table
                SqliteStatement stmtWorkingSetTracks{m_db, "DELETE FROM WorkingSetTracks WHERE track_id = ?"};
                for (const auto &trackId : trackIds)
                {
                    stmtWorkingSetTracks.addParam(trackId);
                    if (!stmtWorkingSetTracks.execute())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, std::format("Failed to delete track {} from WorkingSetTracks", trackId));
                    }
                    stmtWorkingSetTracks.reset();
                }

                // Step 3: Delete from Tracks table
                SqliteStatement stmtTracks{m_db, "DELETE FROM Tracks WHERE track_id = ?"};
                for (const auto &trackId : trackIds)
                {
                    stmtTracks.addParam(trackId);
                    if (!stmtTracks.execute())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, std::format("Failed to delete track {} from Tracks", trackId));
                    }
                    stmtTracks.reset();
                }

                for (const auto mixId : affectedMixes)
                {
                    if (!refreshMixSummary(m_db, transaction, mixId).isOk())
                    {
                        transaction.rollback();
                        return DbResult::failure(DbResultStatus::ErrorDB, std::format("Failed to refresh the summary of mix {} after deletion.", mixId));
                    }
                }

                if (!transaction.commit())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit deleteTracksFromLibrary transaction.");
                }

                // Invalidate cached track count
                m_cachedTotalTrackCountValid = false;

                spdlog::info("[SqliteTrackDatabase] Deleted {} track(s) from library (Tracks, MixTracks, WorkingSetTracks)", trackIds.size());
                return DbResult::success();
            }
            else
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "Failed to begin deleteTracksFromLibrary transaction.");
            }
        }

        // GetTracks and GetTotalTrackCount need more complex SQL building based
        // on TrackQueryArgs
        std::vector<TrackInfo> SqliteTrackDatabase::getTracks(const TrackQueryArgs &args) const
        {
            if (!isOpen())
                return {};
            m_lastErrorMessage.clear();
            m_cachedTotalTrackCountValid = false;

            std::vector<TrackInfo> results;
            SqliteStatement stmt{m_db};
            SqliteStatementConstruction stmtConstruction{stmt};
            if (!stmtConstruction.createSelectStatement(args))
            {
                m_lastErrorMessage = "Failed to create select statement: " + m_db.getLastError();
                return results; // Return empty vector on failure
            }

            if (stmt.isValid())
            {
                while (stmt.getNextResult())
                {
                    results.emplace_back(trackInfoFromStatement(stmt));
                }
                readAllTagTracks(results);
            }
            else
            {
                m_lastErrorMessage = m_db.getLastError();
            }
            if (!m_db.getLastError().empty() && m_db.getLastError().find("SQLITE_DONE") == std::string::npos)
            {
                m_lastErrorMessage = m_db.getLastError();
            }
            return results;
        }

        int64_t nextUniqueId()
        {
            static std::atomic<int64_t> counter{0};
            return counter.fetch_add(1, std::memory_order_relaxed);
        }

        std::string generateTempTableName(const std::string &base = "temp_table_")
        {
            return base + "_" + std::to_string(nextUniqueId());
        }

        void SqliteTrackDatabase::readAllTagTracks(std::vector<TrackInfo> &tracks) const
        {
            StringWriter sqlStatement;
            sqlStatement.append("SELECT track_id, tag_id FROM TrackTags WHERE track_id IN (");
            bool first = true;
            std::unordered_map<TrackId, TrackInfo *> trackMap;
            for (auto &track : tracks)
            {
                if (track.trackId != -1) // Only include valid track IDs
                {
                    if (first)
                    {
                        first = false; // First track, no comma
                    }
                    else
                    {
                        sqlStatement.append(", ");
                    }
                    sqlStatement.append(std::to_string(track.trackId));
                    trackMap[track.trackId] = &track; 
                }
            }
            sqlStatement.append(");");

            SqliteStatement stmt{m_db, sqlStatement.asString()};
            while (stmt.getNextResult())
            {
                if (!stmt.isNull(0))
                {
                    TrackId trackId = stmt.getInt64(0);
                    const auto it = trackMap.find(trackId);
                    if (it != trackMap.end())
                    {
                        TagId tagId = stmt.getInt64(1);
                        it->second->tag_ids.emplace_back(tagId); // Add tag_id to the corresponding TrackInfo
                    }
                }
            }
        }

        std::vector<TrackId> SqliteTrackDatabase::getTrackIds(const TrackQueryArgs &args) const
        {
            if (!isOpen())
                return {};
            m_lastErrorMessage.clear();

            std::vector<TrackId> results;
            SqliteStatement stmt{m_db};
            SqliteStatementConstruction stmtConstruction{stmt};

            // Create a modified query args to only select the track_id
            TrackQueryArgs id_args = args;
            id_args.columns = {"track_id"};
            id_args.usePaging = false; // Ensure we get all IDs

            if (!stmtConstruction.createSelectStatement(id_args))
            {
                m_lastErrorMessage = "Failed to create select statement for getTrackIds: " + m_db.getLastError();
                return results; // Return empty vector on failure
            }

            if (stmt.isValid())
            {
                while (stmt.getNextResult())
                {
                    results.push_back(stmt.getInt64(0));
                }
            }
            else
            {
                m_lastErrorMessage = m_db.getLastError();
            }
            if (!m_db.getLastError().empty() && m_db.getLastError().find("SQLITE_DONE") == std::string::npos)
            {
                m_lastErrorMessage = m_db.getLastError();
            }
            return results;
        }

        // In SqliteTrackDatabase.cpp

        int SqliteTrackDatabase::getTotalTrackCount(const TrackQueryArgs &args) const
        {
            if (!isOpen())
            {
                return -1; // Indicate error
            }
            m_lastErrorMessage.clear();

            // The old optimization for virtualFolderId is now completely obsolete and has been removed.
            // Our new SqliteStatementConstruction handles all filtering logic correctly,
            // including the new folderIds and recursive filtering.

            // We now have one single, robust path for all count queries.
            SqliteStatement stmt{m_db};
            SqliteStatementConstruction stmtConstruction{stmt};

            if (!stmtConstruction.createCountStatement(args))
            {
                // If the statement construction itself fails (e.g., bad SQL string),
                // we should capture that error. The constructor should have logged it.
                // We can get a more specific error from our local object if needed.
                m_lastErrorMessage = "Failed to create count statement.";
                return -1;
            }

            // After construction, the statement should be valid and all parameters bound.
            if (stmt.isValid())
            {
                if (stmt.getNextResult())
                {
                    // Successfully executed the query and got a row. Return the count.
                    return stmt.getInt32(0);
                }
            }

            // If we reach here, either the statement was invalid after construction,
            // or getNextResult() failed. In either case, it's an error.
            m_lastErrorMessage = m_db.getLastError();
            return -1; // Indicate error
        }

        bool SqliteTrackDatabase::getAggregateStats(const TrackQueryArgs &args, AggregateStats &outStats) const
        {
            outStats.reset();

            if (!isOpen())
            {
                return false;
            }

            try
            {
                SqliteStatement stmt{m_db};
                SqliteStatementConstruction stmtConstruction{stmt};

                if (!stmtConstruction.createAggregateStatement(args))
                {
                    spdlog::error("Failed to create aggregate statement");
                    return false;
                }

                if (stmt.isValid() && stmt.getNextResult())
                {
                    outStats.totalTracks = stmt.getInt64(0);
                    outStats.totalBytes = static_cast<uint64_t>(stmt.getInt64(1));
                    outStats.totalDurationMs = stmt.getInt64(2);
                    return true;
                }
            }
            catch (const std::exception &e)
            {
                spdlog::error("Failed to get aggregate stats: {}", e.what());
            }

            return false;
        }

        // --- Tag Management Implementations ---
        bool SqliteTrackDatabase::updateTrackTagsFromInsideTransaction(TrackId trackId, const std::vector<TagId> &tagIds)
        {
            SqliteStatement stmt_delete{m_db, "DELETE FROM TrackTags WHERE track_id = ?;"};
            if (!stmt_delete.isValid())
            {
                m_lastErrorMessage = m_db.getLastError();
                return false;
            }

            stmt_delete.addParam(trackId);
            if (!stmt_delete.execute())
            {
                return false;
            }

            for (const auto tagId : tagIds)
            {
                // Insert new tag associations
                SqliteStatement stmt_insert{m_db, "INSERT INTO TrackTags (track_id, tag_id) VALUES (?, ?);"};
                if (!stmt_insert.isValid())
                {
                    return false;
                }
                stmt_insert.addParam(trackId);
                stmt_insert.addParam(tagId);
                if (!stmt_insert.execute())
                {
                    return false;
                }
            }
            return true;
        }

        DbResult SqliteTrackDatabase::updateTrackTags(TrackId trackId, const std::vector<TagId> &tagIds)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open");
            }
            m_lastErrorMessage.clear();

            // Transaction for atomicity
            if (!m_db.execute("BEGIN TRANSACTION;"))
            {
                m_lastErrorMessage = m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, "Unable to begin transaction: " + m_db.getLastError());
            }

            if (!updateTrackTagsFromInsideTransaction(trackId, tagIds))
            {
                m_db.execute("ROLLBACK;");
                return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update track tags: " + m_db.getLastError());
            }
            if (!m_db.execute("COMMIT;"))
            {
                m_db.execute("ROLLBACK;");
                m_lastErrorMessage = m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit transaction: " + m_db.getLastError());
            }
            return DbResult::success();
        }

        std::vector<TagId> SqliteTrackDatabase::getTrackTags(TrackId trackId) const
        {
            if (!isOpen())
                return {};
            m_lastErrorMessage.clear();
            std::vector<TagId> tags;
            SqliteStatement stmt{m_db, "SELECT tag_id FROM TrackTags WHERE track_id = ?;"};
            if (!stmt.isValid())
            {
                m_lastErrorMessage = m_db.getLastError();
                return {};
            }

            stmt.addParam(trackId);
            while (stmt.getNextResult())
            {
                if (!stmt.isNull(0))
                {
                    tags.emplace_back(stmt.getInt64(0));
                }
            }
            if (!m_db.getLastError().empty() && m_db.getLastError().find("SQLITE_DONE") == std::string::npos)
            {
                m_lastErrorMessage = m_db.getLastError();
            }
            return tags;
        }

        bool SqliteTrackDatabase::getTotalTrackCountForFolders(const std::unordered_set<FolderId>& folderIds, int64_t& outCount) const
        {
            StringWriter sql;
            sql.append("SELECT COUNT(*) FROM Tracks WHERE folder_id IN (");
            for (size_t idx = 0; idx < folderIds.size(); ++idx)
            {
                if(idx > 0)
                {
                    sql.append(',');
                }
                sql.append('?');
            }
            sql.append(");");
            const auto sqlString = sql.asString();
            SqliteStatement stmt{m_db, sql.asString()};
            for (const auto &folderId : folderIds)
            {
                stmt.addParam(folderId);
            }
            if (!stmt.getNextResult())
            {
                spdlog::error("Failed to execute {}: {}", sqlString, m_db.getLastError());
                return false;
            }
            outCount = stmt.getInt64(0);
            return true;
        }

        std::unordered_set<FolderId> SqliteTrackDatabase::getFoldersContainingMatchingTracks(const std::vector<std::string>& searchTerms) const
        {
            std::unordered_set<FolderId> result;

            // If no search terms, return empty set (all folders should be visible)
            if (searchTerms.empty())
            {
                return result;
            }

            // Build FTS query to get folders containing matching tracks
            StringWriter sql;
            sql.append("SELECT DISTINCT t.folder_id FROM Tracks t "
                      "INNER JOIN TracksSearchFTS ON t.track_id = TracksSearchFTS.rowid "
                      "WHERE TracksSearchFTS MATCH ?");

            SqliteStatement stmt{m_db, sql.asString()};

            // For FTS5, we pass the entire search string as a single term
            stmt.addParam(searchTerms[0]);

            // Collect all folder IDs directly containing matching tracks
            std::unordered_set<FolderId> directFolders;
            while (stmt.getNextResult())
            {
                const auto folderId = stmt.getInt64(0);
                directFolders.insert(folderId);
            }

            if (!m_db.getLastError().empty() && m_db.getLastError().find("SQLITE_DONE") == std::string::npos)
            {
                spdlog::error("getFoldersContainingMatchingTracks query failed: {}", m_db.getLastError());
                return result;
            }

            // Include all ancestors of matching folders
            for (const auto folderId : directFolders)
            {
                result.insert(folderId);

                // Get all parent folders recursively
                const auto parents = m_folderDatabase.getParentSet(folderId);
                for (const auto parentId : parents)
                {
                    result.insert(parentId);
                }
            }

            return result;
        }

        std::vector<TagId> SqliteTrackDatabase::getAllTags() const
        {
            if (!isOpen())
                return {};
            m_lastErrorMessage.clear();
            std::vector<TagId> tags;
            SqliteStatement stmt{m_db, "SELECT T.tag_id FROM TrackTags;"};
            if (!stmt.isValid())
            {
                m_lastErrorMessage = m_db.getLastError();
                return {};
            }
            while (stmt.getNextResult())
            {
                if (!stmt.isNull(0))
                {
                    tags.emplace_back(stmt.getInt64(0));
                }
            }
            if (!m_db.getLastError().empty() && m_db.getLastError().find("SQLITE_DONE") == std::string::npos)
            {
                m_lastErrorMessage = m_db.getLastError();
            }
            return tags;
        }

        std::filesystem::path TrackInfo::reconstructFullPath(const ITrackDatabase &db) const
        {
            if (folderId == -1 || filename.empty())
            {
                return {}; // Not enough info to reconstruct
            }

            // Delegate the heavy lifting to the database's path reconstruction method
            const auto parentPath = db.reconstructFullPath(folderId);
            if (parentPath.empty())
            {
                return {}; // The parent folder couldn't be found
            }

            return parentPath / filename;
        }

        DbResult SqliteTrackDatabase::saveWaveform(TrackId trackId, const std::vector<unsigned char>& blob)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for saveWaveform.");
            }
            m_lastErrorMessage.clear();

            SqliteStatement stmt{m_db, "INSERT OR REPLACE INTO WaveformCache (track_id, waveform_blob) VALUES (?, ?);"};
            if (!stmt.isValid())
            {
                m_lastErrorMessage = "Prepare failed for saveWaveform: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }

            stmt.addParam(trackId);
            stmt.addParam(blob);

            if (stmt.execute())
            {
                return DbResult::success();
            }
            
            m_lastErrorMessage = "Execute failed for saveWaveform: " + m_db.getLastError();
            return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
        }

        DbResult SqliteTrackDatabase::loadWaveform(TrackId trackId, std::vector<unsigned char>& blob)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for loadWaveform.");
            }
            m_lastErrorMessage.clear();

            SqliteStatement stmt{m_db, "SELECT waveform_blob FROM WaveformCache WHERE track_id = ?;"};
            if (!stmt.isValid())
            {
                m_lastErrorMessage = "Prepare failed for loadWaveform: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }

            stmt.addParam(trackId);

            if (stmt.getNextResult())
            {
                blob = stmt.getBlob(0);

                // Reported as a miss, not handed back. A stored thumbnail with no samples in it is
                // a record of a failure, not a waveform, and every caller reads this through here -
                // the mix editor's per-track component, its preloader, and anything later. Failing
                // once, here, is what turns those rows back into something that regenerates.
                if (isEmptyWaveformBlob(blob))
                {
                    spdlog::info("[Waveform] Cached waveform for track {} has no samples; treating it as absent.", trackId);
                    blob.clear();
                    return DbResult::failure(DbResultStatus::ErrorGeneric, "Cached waveform is empty.");
                }

                return DbResult::success();
            }
            
            return DbResult::failure(DbResultStatus::ErrorGeneric, "No waveform found in cache.");
        }
        
        bool SqliteTrackDatabase::enrichWavMetadata(std::atomic<bool> &shouldCancel)
        {
            if (!isOpen())
            {
                spdlog::error("Database not open for WAV metadata enrichment.");
                return false;
            }
            
            // Query all WAV files with missing metadata
            const auto sql = R"SQL(
                SELECT track_id, filename, title, artist_name, album_title, folder_id
                FROM Tracks 
                WHERE (filename LIKE '%.wav' OR filename LIKE '%.WAV')
                AND (title IS NULL OR title = '' 
                     OR artist_name IS NULL OR artist_name = ''
                     OR album_title IS NULL OR album_title = '')
            )SQL";
            
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare WAV enrichment query: {}", m_db.getLastError());
                return false;
            }
            
            std::vector<std::tuple<TrackId, std::string, std::string, std::string>> updates;
            int processedCount = 0;
            int updatedCount = 0;
            
            while (stmt.getNextResult() && !shouldCancel)
            {
                const auto trackId = stmt.getInt32(0);
                const auto filename = stmt.getText(1);
                auto title = stmt.isNull(2) ? "" : stmt.getText(2);
                auto artist = stmt.isNull(3) ? "" : stmt.getText(3);
                auto album = stmt.isNull(4) ? "" : stmt.getText(4);
                const auto folderId = stmt.getInt32(5);
                
                processedCount++;
                bool needsUpdate = false;
                
                // Get folder info for context
                const auto folderPath = reconstructFullPath(folderId);
                const auto folderName = folderPath.filename().string();
                
                // Extract title from filename if missing
                if (title.empty())
                {
                    auto stem = std::filesystem::path(filename).stem().string();
                    
                    // Clean up common WAV naming patterns
                    // Remove track number prefix like "01-" or "A1-"
                    if (stem.length() > 2 && (stem[2] == '-' || stem[2] == '_'))
                    {
                        stem = stem.substr(3);
                    }
                    
                    // Remove common suffixes
                    const std::vector<std::string> suffixes = {
                        "dither", "_master", "_final", "_mix", "_mastered", 
                        "-master", "-final", "-mix", "-mastered"
                    };
                    for (const auto& suffix : suffixes)
                    {
                        const auto pos = stem.rfind(suffix);
                        if (pos != std::string::npos)
                        {
                            stem = stem.substr(0, pos);
                            // Trim trailing underscore or dash
                            if (!stem.empty() && (stem.back() == '_' || stem.back() == '-'))
                            {
                                stem.pop_back();
                            }
                        }
                    }
                    
                    title = stem;
                    needsUpdate = true;
                }
                
                // Extract album from folder if missing
                if (album.empty() && !folderName.empty())
                {
                    album = folderName;
                    needsUpdate = true;
                }
                
                // Try to extract artist from folder structure
                if (artist.empty() && !folderName.empty())
                {
                    // Common patterns: "Artist - Album", "Artist_-_Album", "Artist - Album - Year"
                    size_t separatorPos = folderName.find(" - ");
                    if (separatorPos == std::string::npos)
                    {
                        separatorPos = folderName.find("_-_");
                    }
                    
                    if (separatorPos != std::string::npos)
                    {
                        artist = folderName.substr(0, separatorPos);
                        
                        // Update album to just the album part if it was the full folder name
                        if (album == folderName && separatorPos + 3 < folderName.length())
                        {
                            auto albumPart = folderName.substr(separatorPos + 3);
                            
                            // Remove year suffix if present (e.g., " - 2020")
                            const auto lastDash = albumPart.rfind(" - ");
                            if (lastDash != std::string::npos && lastDash + 3 < albumPart.length())
                            {
                                // Check if what follows is a year (4 digits)
                                const auto possibleYear = albumPart.substr(lastDash + 3);
                                if (possibleYear.length() == 4 && 
                                    std::all_of(possibleYear.begin(), possibleYear.end(), ::isdigit))
                                {
                                    albumPart = albumPart.substr(0, lastDash);
                                }
                            }
                            
                            album = albumPart;
                        }
                        needsUpdate = true;
                    }
                }
                
                if (needsUpdate)
                {
                    updates.emplace_back(trackId, title, artist, album);
                    updatedCount++;
                    
                    spdlog::debug("WAV enrichment: {} -> Title: '{}', Artist: '{}', Album: '{}'",
                        filename, title, artist, album);
                }
                
                // Log progress every 100 files
                if (processedCount % 100 == 0)
                {
                    spdlog::info("WAV enrichment progress: {} files processed, {} updated",
                        processedCount, updatedCount);
                }
            }
            
            // Apply updates in a transaction
            if (!updates.empty() && !shouldCancel)
            {
                if (SqliteTransaction transaction{m_db})
                {
                    const auto updateSql = R"SQL(
                        UPDATE Tracks 
                        SET title = ?, artist_name = ?, album_title = ?
                        WHERE track_id = ?
                    )SQL";
                    
                    SqliteStatement updateStmt{m_db, updateSql};
                    if (!updateStmt.isValid())
                    {
                        spdlog::error("Failed to prepare update statement: {}", m_db.getLastError());
                        transaction.rollback();
                        return false;
                    }
                    
                    for (const auto& [trackId, title, artist, album] : updates)
                    {
                        if (shouldCancel)
                        {
                            transaction.rollback();
                            return false;
                        }
                        
                        updateStmt.reset();
                        updateStmt.addParam(title);
                        updateStmt.addParam(artist);
                        updateStmt.addParam(album);
                        updateStmt.addParam(trackId);
                        
                        if (!updateStmt.execute())
                        {
                            spdlog::error("Failed to update track {}: {}", trackId, m_db.getLastError());
                            transaction.rollback();
                            return false;
                        }
                    }
                    
                    if (!transaction.commit())
                    {
                        spdlog::error("Failed to commit WAV enrichment transaction: {}", m_db.getLastError());
                        return false;
                    }
                    
                    spdlog::info("WAV metadata enrichment completed: {} files processed, {} updated",
                        processedCount, updatedCount);
                }
            }
            else if (shouldCancel)
            {
                spdlog::info("WAV metadata enrichment cancelled: {} files processed, {} pending updates discarded",
                    processedCount, updatedCount);
                return false;
            }
            else
            {
                spdlog::info("WAV metadata enrichment: {} files checked, none needed updates", processedCount);
            }
            
            return true;
        }

    } // namespace database
} // namespace jucyaudio
