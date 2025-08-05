#include <Database/Includes/ITagManager.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteStatementConstruction.h>
#include <Database/Sqlite/SqliteTrackDatabase.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Utils/AssortedUtils.h>
#include <Utils/StringWriter.h>
#include <algorithm> // For std::reverse
#include <cassert>   // For assert
#include <ranges>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace
{
    using namespace jucyaudio;
    using namespace database;

    // Array of initial SQL statements for schema creation
    const char *maintenanceSqlStatements[] = {"PRAGMA optimize;", "VACUUM;"};

    // --- THIS IS THE FINAL, CORRECTED SCHEMA FOR A NEW DATABASE ---
    const char *initialSqlStatements[] = {
        "PRAGMA foreign_keys = ON;",
        R"SQL(
        CREATE TABLE IF NOT EXISTS Folders (
            folder_id   INTEGER PRIMARY KEY,
            parent_id   INTEGER,
            name        TEXT NOT NULL,
            root_path   TEXT,
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
            mix_id INTEGER NOT NULL, track_id INTEGER NOT NULL, order_in_mix INTEGER NOT NULL,
            mix_data TEXT NOT NULL, PRIMARY KEY(mix_id, track_id),
            FOREIGN KEY(mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE,
            FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_mixtracks_order ON MixTracks(mix_id, order_in_mix);",
        R"SQL(
        CREATE TABLE IF NOT EXISTS MixUndoHistory (
            undo_id INTEGER PRIMARY KEY, mix_id INTEGER NOT NULL, operation_id INTEGER NOT NULL,
            operation_type INTEGER NOT NULL, table_name INTEGER NOT NULL, record_id INTEGER,
            old_state TEXT, new_state TEXT,
            FOREIGN KEY (mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_mixundohistory_mix_id ON MixUndoHistory (mix_id);",
        "CREATE INDEX IF NOT EXISTS idx_mixundohistory_operation_id ON MixUndoHistory (operation_id);",
        "CREATE TABLE IF NOT EXISTS LibraryRoots (root_id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL);",
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
        info.rating = stmt.getInt32(col++);
        info.liked_status = stmt.getInt32(col++);
        info.play_count = stmt.getInt32(col++);
        info.last_played = timestampFromInt64(stmt.getInt64(col++));
        if (!stmt.isNull(col))
            info.internal_content_hash = stmt.getText(col);
        col++;
        if (!stmt.isNull(col))
            info.user_notes = stmt.getText(col);
        col++;
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
                info.status = TrackStatus::Unknown;
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
        ok &= stmt.addParam(info.rating);
        ok &= stmt.addParam(info.liked_status);
        ok &= stmt.addParam(info.play_count);
        ok &= stmt.addParam(timestampToInt64(info.last_played));
        ok &= stmt.addParam(info.internal_content_hash);
        ok &= stmt.addParam(info.user_notes);
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
              m_workingSetManager{m_db},
              m_folderDatabase{m_db},
              m_markerManager{m_db},
              m_libraryRoootManager{m_db},
              m_undoManager{m_db},
              m_mixManagerWithUndo{m_mixManager, m_undoManager},
              m_databaseFilePath{},
              m_lastErrorMessage{},
              m_cachedTotalTrackCount{0},
              m_cachedTotalTrackCountValid{false}

        {
            spdlog::debug("SqliteTrackDatabase created.");
        }

        SqliteTrackDatabase::~SqliteTrackDatabase()
        {
            close(); // m_db.close() will be called by SqliteDatabase destructor
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
            return m_mixManagerWithUndo;
        }

        const IMixManager &SqliteTrackDatabase::getMixManager() const
        {
            assert(isOpen() && "Cannot get mix manager when database is not open");
            return m_mixManagerWithUndo;
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

        IUndoManager &SqliteTrackDatabase::getUndoManager()
        {
            assert(isOpen() && "Cannot get undo manager when database is not open");
            return m_undoManager;
        }

        const IUndoManager &SqliteTrackDatabase::getUndoManager() const
        {
            assert(isOpen() && "Cannot get undo manager when database is not open");
            return m_undoManager;
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

            DbResult schemaResult = createTablesIfNeeded();
            if (!schemaResult.isOk())
            {
                m_db.close();
                return schemaResult;
            }
            m_undoManager.initialize();
            return DbResult::success();
        }

        bool SqliteTrackDatabase::runMaintenanceTasks([[maybe_unused]] std::atomic<bool> &shouldCancel)
        {
            if (!isOpen())
            {
                spdlog::error("DB not open for schema creation.");
                return false;
            }
            for (const auto *sql : maintenanceSqlStatements)
            {
                if (!m_db.execute(sql))
                {
                    m_lastErrorMessage = "Maintenance statement failed [" + std::string(sql) + "] Error: " + m_db.getLastError();
                    spdlog::error("Maintenance task failed: {}", m_lastErrorMessage);
                    return false;
                }
            }
            spdlog::info("Database schema verified/created successfully.");
            return true;
        }

        DbResult SqliteTrackDatabase::createTablesIfNeeded()
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for schema creation.");
            }

            spdlog::info("Verifying/Creating database schema...");
            for (const auto *sql : initialSqlStatements)
            {
                if (!m_db.execute(sql))
                {
                    m_lastErrorMessage = "Schema creation failed on SQL: [" + std::string(sql) + "] Error: " + m_db.getLastError();
                    return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                }
            }

            SqliteStatement stmt{m_db, "INSERT OR IGNORE INTO SchemaInfo (key, value) VALUES (?, ?);"};
            if (!stmt.isValid())
            {
                m_lastErrorMessage = "Failed to prepare schema version insert: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }
            stmt.addParam("schema_version");
            stmt.addParam("9");
            if (!stmt.execute())
            {
                m_lastErrorMessage = "Failed to insert initial schema version: " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }

            // return runMigrations(); // Call migrations after basic schema is
            // confirmed For now, assume runMigrations is simple
            DbResult migrationResult = runMigrations();
            if (!migrationResult.isOk())
            {
                return migrationResult;
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

            spdlog::debug("Reconstructing full path for folderId: {}", folderId);
            std::vector<std::string> parts;
            FolderId currentId = folderId;

            while (currentId != -1)
            {
                auto folderOpt = m_folderDatabase.getFolderById(currentId);
                if (!folderOpt)
                {
                    spdlog::error("reconstructFullPath FAIL: Could not find folder with ID {}.", currentId);
                    return {};
                }

                spdlog::debug(" -> Found part: '{}', parentId: {}", folderOpt->name, folderOpt->parentId);

                if (!folderOpt->rootPath.empty())
                {
                    parts.insert(parts.begin(), folderOpt->rootPath);
                    spdlog::debug(" -> Hit root path: '{}'", folderOpt->rootPath);
                    break;
                }

                parts.insert(parts.begin(), folderOpt->name);
                currentId = folderOpt->parentId;
            }

            std::filesystem::path result;
            for (const auto &part : parts)
            {
                result /= part;
            }
            m_lastKnownFolderId = folderId;
            m_lastKnownFolderPath = result;
            spdlog::debug("Reconstructed path for folderId {}: {}", folderId, pathToString(result));
            return result;
        }

        std::filesystem::path SqliteTrackDatabase::reconstructFullPath(const TrackInfo &trackInfo) const
        {
            if (trackInfo.folderId == -1)
                return {};
            return reconstructFullPath(trackInfo.folderId) / trackInfo.filename;
        }

        int SqliteTrackDatabase::getDBSchemaVersion()
        {
            if (!isOpen())
                return 0; // Or -1 to indicate error
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
                m_cachedTotalTrackCount = false;
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
                SqliteStatement stmt{m_db, "UPDATE Tracks SET bpm=?, intro_end=?, outro_start=? WHERE track_id = ?;"};
                if (!stmt.isValid())
                {
                    m_lastErrorMessage = "Prepare failed for updateTrackBpm(): " + m_db.getLastError();
                    transaction.rollback();
                    return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
                }

                for (const auto &[trackId, am] : results)
                {
                    stmt.reset();
                    stmt.addParam(static_cast<int64_t>(am.bpm * 100)); // Store as integer
                    if (am.hasIntro)
                    {
                        stmt.addParam(static_cast<int64_t>(am.introEnd * 1000));
                    }
                    else
                    {
                        stmt.addNullParam();
                    }
                    if (am.hasOutro)
                    {
                        stmt.addParam(static_cast<int64_t>(am.outroStart * 1000));
                    }
                    else
                    {
                        stmt.addNullParam();
                    }
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
            const auto tempTableName{generateTempTableName()};
            m_db.execute("BEGIN TRANSACTION;");
            m_db.execute("DROP TABLE IF EXISTS " + tempTableName + ";");
            m_db.execute("CREATE TEMP TABLE " + tempTableName + " (track_id INTEGER PRIMARY KEY);");
            std::unordered_map<TrackId, TrackInfo *> trackMap;
            for (auto &track : tracks)
            {
                if (track.trackId != -1) // Only insert valid track IDs
                {
                    m_db.execute("INSERT INTO " + tempTableName + " (track_id) VALUES (" + std::to_string(track.trackId) + ");");
                    trackMap[track.trackId] = &track; // Store pointer to TrackInfo
                }
            }
            SqliteStatement stmt{m_db,
                "SELECT track_id, tag_id FROM TrackTags WHERE "
                "track_id IN (SELECT track_id FROM " +
                    tempTableName + ");"};
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
            m_db.execute("DROP TABLE " + tempTableName + ";");
            m_db.execute("COMMIT;");
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
            std::filesystem::path parentPath = db.reconstructFullPath(folderId);
            if (parentPath.empty())
            {
                return {}; // The parent folder couldn't be found
            }

            return parentPath / filename;
        }
    } // namespace database
} // namespace jucyaudio
