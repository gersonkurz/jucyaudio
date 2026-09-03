/*
 * This file is part of jucyaudio.
 * Copyright (C) 2025 Gerson Kurz <not@p-nand-q.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

namespace jucyaudio
{
    namespace tests
    {
        /**
         * @brief The schema a brand-new database really had at version 12.
         *
         * Not reconstructed and not hand-written: this is `initialSqlStatements` copied out of commit
         * d34e3bc, the commit that introduced the FTS5 search and set `latestSchemaVersion` back to 12
         * to carry it. Verify it with
         *
         *     git show d34e3bc:Database/Sqlite/SqliteTrackDatabase.cpp
         *
         * and compare the array. Two deliberate differences from the original: everything is indented one
         * level further to sit in this namespace, and the LibraryRoots statement is split into two
         * adjacent string literals because at this indentation the original line is 164 characters and
         * .clang-format pins the limit at 160. The compiler joins them back into the identical text.
         *
         * That verifiability is the whole point. A test that pins the ladder against a shape somebody
         * invented for the test proves nothing, and a reviewer has to be able to check that the shape
         * came from the project's own history rather than from whatever made the comparison pass.
         *
         * Why d34e3bc and why 12: at that commit the search tables, the FTS index and the five search
         * triggers are created by the v12 migration rung and are absent from the array below, so a
         * library created from scratch then never got them while one migrated up did. That is the
         * divergence the v32 rung exists to repair, frozen at the version where it starts. (The
         * preceding commit 258454b also declares 12 and carries a byte-identical array, but it predates
         * the search rung entirely, so it cannot show the divergence.)
         *
         * Note what is therefore NOT in here: TracksSearchData, TracksSearchFTS and the five triggers.
         * That is not an omission in the fixture; it is the defect.
         */
        constexpr const char *schemaV12Statements[] = {
            "PRAGMA foreign_keys = ON;",
            R"SQL(
            CREATE TABLE IF NOT EXISTS Folders (
                folder_id   INTEGER PRIMARY KEY,
                parent_id   INTEGER,
                name        TEXT NOT NULL,
                root_path   TEXT,
                track_count INTEGER,
                FOREIGN KEY (parent_id) REFERENCES Folders(folder_id) ON DELETE CASCADE
            );)SQL",
            "CREATE INDEX IF NOT EXISTS idx_folders_parent_name ON Folders(parent_id, name);",
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
                FOREIGN KEY (folder_id) REFERENCES Folders(folder_id) ON DELETE CASCADE
            );)SQL",
            "CREATE INDEX IF NOT EXISTS idx_tracks_parent_filename ON Tracks(folder_id, filename);",
            "CREATE INDEX IF NOT EXISTS idx_tracks_artist ON Tracks (artist_name COLLATE NOCASE);",
            "CREATE INDEX IF NOT EXISTS idx_tracks_album ON Tracks (album_title COLLATE NOCASE);",
            "CREATE INDEX IF NOT EXISTS idx_tracks_title ON Tracks (title COLLATE NOCASE);",
            "CREATE INDEX IF NOT EXISTS idx_tracks_bpm ON Tracks (bpm);",
            "CREATE INDEX IF NOT EXISTS idx_tracks_rating ON Tracks (rating);",
            "CREATE INDEX IF NOT EXISTS idx_tracks_liked_status ON Tracks (liked_status);",
            R"SQL(CREATE TABLE IF NOT EXISTS Tags (tag_id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE);)SQL",
            R"SQL(
            CREATE TABLE IF NOT EXISTS TrackTags (
                track_id INTEGER NOT NULL, tag_id INTEGER NOT NULL, PRIMARY KEY (track_id, tag_id),
                FOREIGN KEY (track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE,
                FOREIGN KEY (tag_id) REFERENCES Tags(tag_id) ON DELETE CASCADE
            );)SQL",
            "CREATE INDEX IF NOT EXISTS idx_tracktags_tag_id ON TrackTags(tag_id);",
            R"SQL(CREATE TABLE IF NOT EXISTS SchemaInfo (key TEXT PRIMARY KEY, value TEXT);)SQL",
            R"SQL(
            CREATE TABLE IF NOT EXISTS WorkingSets(
                ws_id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE,
                timestamp INTEGER, sort_order TEXT
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
            R"SQL(
            CREATE TABLE IF NOT EXISTS MixUndoHistory (
                undo_id INTEGER PRIMARY KEY, mix_id INTEGER NOT NULL, operation_id INTEGER NOT NULL,
                operation_type INTEGER NOT NULL, table_name INTEGER NOT NULL, record_id INTEGER,
                old_state TEXT, new_state TEXT,
                FOREIGN KEY (mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE
            );)SQL",
            "CREATE INDEX IF NOT EXISTS idx_mixundohistory_mix_id ON MixUndoHistory (mix_id);",
            "CREATE INDEX IF NOT EXISTS idx_mixundohistory_operation_id ON MixUndoHistory (operation_id);",
            "CREATE TABLE IF NOT EXISTS LibraryRoots (root_id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, "
            "file_count INTEGER DEFAULT 0, last_scanned INTEGER);",
        };

        /**
         * @brief The search objects the v12 rung created, from the same commit.
         *
         * Two kinds of real v12 database existed. One was created from scratch and has none of this -
         * `schemaV12Statements` on its own. The other migrated up from an earlier version and ran the
         * v12 rung, which created exactly these objects. Both shapes are worth climbing: the first is
         * missing more, and the second is the only one that reaches the v25 rung with search tables
         * present, which is the branch of that rung the first one skips.
         *
         * Verbatim from the v12 rung of the same commit, in the order it ran them: the content table, the
         * FTS table, the rebuild that populates it, then the five triggers. Extracted the same way and
         * verifiable the same way. One step of the rung is left out - the INSERT that filled the content
         * table from Tracks - because the fixture is built before any rows exist, so there would be
         * nothing for it to copy; the rows it gets later arrive through the triggers installed here.
         */
        constexpr const char *schemaV12SearchStatements[] = {
            R"SQL(
                            CREATE TABLE IF NOT EXISTS TracksSearchData (
                                track_id INTEGER PRIMARY KEY,
                                search_content TEXT NOT NULL,
                                FOREIGN KEY (track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
                            );)SQL",
            R"SQL(
                            CREATE VIRTUAL TABLE TracksSearchFTS USING fts5(
                                search_content,
                                content='TracksSearchData',
                                content_rowid='track_id',
                                tokenize='unicode61'
                            );)SQL",
            "INSERT INTO TracksSearchFTS(TracksSearchFTS) VALUES('rebuild');",
            R"SQL(
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
                            END;)SQL",
            R"SQL(
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
                            END;)SQL",
            R"SQL(
                            CREATE TRIGGER tracks_search_delete
                            AFTER DELETE ON Tracks
                            BEGIN
                                DELETE FROM TracksSearchData WHERE track_id = OLD.track_id;
                            END;)SQL",
            R"SQL(
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
                            END;)SQL",
            R"SQL(
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
                            END;)SQL",
        };

    } // namespace tests
} // namespace jucyaudio
