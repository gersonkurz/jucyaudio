#include <Database/Includes/ITagManager.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteStatementConstruction.h>
#include <Database/Sqlite/SqliteTrackDatabase.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Utils/AssortedUtils.h>
#include <Utils/StringWriter.h>
#include <cassert> // For assert
#include <ranges>
#include <spdlog/spdlog.h>

namespace
{
    using namespace jucyaudio;
    using namespace database;

    // Array of initial SQL statements for schema creation
    const char *maintenanceSqlStatements[] = {"PRAGMA optimize;", "VACUUM;"};

    // Array of initial SQL statements for schema creation
    const char *initialSqlStatements[] = {
        "PRAGMA foreign_keys = ON;",
        R"SQL(
    CREATE TABLE IF NOT EXISTS Folders (
        folder_id INTEGER PRIMARY KEY AUTOINCREMENT,
        fs_path TEXT NOT NULL UNIQUE,
        num_files INTEGER DEFAULT -1,
        total_bytes INTEGER DEFAULT 0,
        last_scanned INTEGER DEFAULT 0
    );)SQL",
        R"SQL(
CREATE TABLE IF NOT EXISTS Tracks (
    track_id INTEGER PRIMARY KEY AUTOINCREMENT,
    folder_id INTEGER,
    filepath TEXT NOT NULL UNIQUE,
    last_modified_fs INTEGER,
    filesize_bytes INTEGER,
    date_added INTEGER,
    last_scanned INTEGER,
    title TEXT,
    artist_name TEXT,
    album_title TEXT,
    album_artist_name TEXT,
    track_number INTEGER,
    disc_number INTEGER,
    year INTEGER, 
    duration INTEGER,
    samplerate INTEGER,
    channels INTEGER,
    bitrate INTEGER,
    codec_name TEXT,
    bpm INTEGER,
    intro_end INTEGER,
    outro_start INTEGER,
    key_string TEXT,
    beat_locations_json TEXT,
    rating INTEGER DEFAULT 0,
    liked_status INTEGER DEFAULT 0,
    play_count INTEGER DEFAULT 0,
    last_played INTEGER,
    internal_content_hash TEXT,
    user_notes TEXT,
    is_missing INTEGER DEFAULT 0,
    FOREIGN KEY (folder_id) REFERENCES Folders(folder_id) ON DELETE CASCADE
);)SQL",
        "CREATE INDEX IF NOT EXISTS idx_tracks_filepath ON Tracks (filepath);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_folder_id ON Tracks (folder_id);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_artist ON Tracks (artist_name "
        "COLLATE NOCASE);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_album ON Tracks (album_title "
        "COLLATE NOCASE);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_title ON Tracks (title COLLATE "
        "NOCASE);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_bpm ON Tracks (bpm);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_rating ON Tracks (rating);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_liked_status ON Tracks "
        "(liked_status);",
        R"SQL(
CREATE TABLE IF NOT EXISTS Tags (
    tag_id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE COLLATE NOCASE);
)SQL",
        R"SQL(
CREATE TABLE IF NOT EXISTS TrackTags (
    track_id INTEGER NOT NULL,
    tag_id INTEGER NOT NULL,
    PRIMARY KEY (track_id, tag_id),
    FOREIGN KEY (track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE,
    FOREIGN KEY (tag_id) REFERENCES Tags(tag_id) ON DELETE CASCADE
);)SQL",
        "CREATE INDEX IF NOT EXISTS idx_tracktags_tag_id ON TrackTags "
        "(tag_id);",
        R"SQL(
CREATE TABLE IF NOT EXISTS SchemaInfo (
    key TEXT PRIMARY KEY,
    value TEXT
);)SQL",

        R"SQL(
CREATE TABLE IF NOT EXISTS WorkingSets(
    ws_id INTEGER PRIMARY KEY AUTOINCREMENT,
    name  TEXT NOT NULL UNIQUE COLLATE NOCASE,
    timestamp INTEGER);
)SQL",
        R"SQL(
CREATE TABLE IF NOT EXISTS WorkingSetTracks(
    ws_id INTEGER NOT NULL,
    track_id INTEGER NOT NULL,
    PRIMARY KEY(ws_id, track_id),
    FOREIGN KEY(ws_id) REFERENCES WorkingSets(ws_id) ON DELETE CASCADE,
    FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE);
)SQL",
        R"SQL(
CREATE TABLE IF NOT EXISTS Mixes(
    mix_id INTEGER PRIMARY KEY AUTOINCREMENT,
    name  TEXT NOT NULL UNIQUE COLLATE NOCASE,
    timestamp INTEGER,
    track_count INTEGER,
    total_length INTEGER
);)SQL",

        R"SQL(
CREATE TABLE IF NOT EXISTS MixTracks(
    mix_id INTEGER NOT NULL,
    track_id INTEGER NOT NULL,
    order_in_mix INTEGER,
    envelopePoints TEXT, -- JSON encoded envelope points
    mix_start_time INTEGER,
    mix_end_time INTEGER,
    PRIMARY KEY(mix_id, track_id),
    FOREIGN KEY(mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE,
    FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
);)SQL",
    };

    TrackInfo trackInfoFromStatement(const SqliteStatement &stmt)
    {
        TrackInfo info{};
        int col = 0;
        info.trackId = stmt.getInt64(col++);
        info.folderId = stmt.getInt64(col++);
        if (!stmt.isNull(col))
            info.filepath = pathFromString(stmt.getText(col));
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
        return info;
    }

    bool bindTrackInfoToStatement(SqliteStatement &stmt, const TrackInfo &info, bool forUpdate = false)
    {
        bool ok = true;
        ok &= stmt.addParam(info.folderId);
        ok &= stmt.addParam(pathToString(info.filepath));
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

        if (forUpdate)
        {
            ok &= stmt.addParam(info.trackId); // For the WHERE track_id = ?
        }
        if (!ok)
        {
            spdlog::error("Failed to bind one or more parameters for TrackInfo: {}", pathToString(info.filepath));
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
            stmt.addParam("1"); // Initial schema version
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
            SqliteStatement stmt{m_db, "UPDATE SchemaInfo SET value = ? WHERE "
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
            // ... (Full implementation from before if needed, or keep as stub)
            // ...
            spdlog::debug("Running DB migrations (currently a stub). Current "
                          "schema version: {}",
                          getDBSchemaVersion());
            // Example:
            // int currentVersion = getDBSchemaVersion();
            // if (currentVersion < REQUIRED_SCHEMA_VERSION_2) { /* apply v2
            // changes */ setDBSchemaVersion(2); } if (currentVersion <
            // REQUIRED_SCHEMA_VERSION_3) { /* apply v3 changes */
            // setDBSchemaVersion(3); }
            return DbResult::success();
        }

        DbResult SqliteTrackDatabase::saveTrackInfo(TrackInfo &trackInfo)
        {
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for saveTrackInfo.");
            }
            m_lastErrorMessage.clear();

            if (trackInfo.filepath.empty())
            {
                return DbResult::failure(DbResultStatus::ErrorGeneric, "Filepath cannot be empty for saveTrackInfo.");
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
            {  // INSERT
                const std::string sql = R"SQL(
            INSERT INTO Tracks (folder_id, filepath, last_modified_fs, filesize_bytes, date_added, last_scanned,
                                title, artist_name, album_title, album_artist_name, track_number, disc_number, year, 
                                duration, samplerate, channels, bitrate, codec_name,
                                bpm, intro_end, outro_start, key_string, beat_locations_json,
                                rating, liked_status, play_count, last_played,
                                internal_content_hash, user_notes, is_missing) 
            VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);
        )SQL"; // 28 placeholders, is_missing defaults to 0 on insert

                SqliteStatement stmt{m_db, sql};
                if (stmt.isValid() && bindTrackInfoToStatement(stmt, trackInfo, false) && stmt.execute())
                {
                    trackInfo.trackId = m_db.getLastInsertRowId();
                    spdlog::debug("Inserted track ID: {}, Path: {}", trackInfo.trackId, pathToString(trackInfo.filepath));
                    success = true;
                }
                m_cachedTotalTrackCount = false;
            }
            else
            {  // UPDATE
                const std::string sql = R"SQL(
            UPDATE Tracks SET folder_id=?, filepath=?, last_modified_fs=?, filesize_bytes=?, date_added=?, last_scanned=?,
                              title=?, artist_name=?, album_title=?, album_artist_name=?, track_number=?, disc_number=?, year=?, 
                              duration=?, samplerate=?, channels=?, bitrate=?, codec_name=?,
                              bpm=?, intro_end=?, outro_start=?, key_string=?, beat_locations_json=?,
                              rating=?, liked_status=?, play_count=?, last_played=?,
                              internal_content_hash=?, user_notes=?, is_missing=?
            WHERE track_id = ?;
        )SQL"; // 28 fields + 1 for track_id in WHERE

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
                WHERE T.bpm IS NULL OR T.bpm <= 0
                LIMIT 1;
            )SQL";

            SqliteStatement stmt1{m_db, sql_priority1};
            if (stmt1.getNextResult())
            {
                return trackInfoFromStatement(stmt1); // Found a high-priority track
            }

            // --- PRIORITY 2: Any other un-analyzed track ---
            std::string sql_priority2 = "SELECT * FROM Tracks WHERE bpm IS NULL OR bpm <= 0 LIMIT 1;";
            SqliteStatement stmt2{m_db, sql_priority2};
            if (stmt2.getNextResult())
            {
                return trackInfoFromStatement(stmt2); // Found a regular-priority track
            }
            return std::nullopt;
        }

        std::optional<TrackInfo> SqliteTrackDatabase::getTrackByFilepath(const std::filesystem::path &filepath) const
        {
            if (!isOpen())
            {
                return std::nullopt;
            }
            m_lastErrorMessage.clear();

            SqliteStatement stmt{m_db, "SELECT * FROM Tracks WHERE filepath = ?;"};
            if (!stmt.isValid())
            {
                m_lastErrorMessage = m_db.getLastError();
                return std::nullopt;
            }
            stmt.addParam(pathToString(filepath)); // Use u8string for the path
            if (stmt.getNextResult())
            {
                auto result{trackInfoFromStatement(stmt)};
                result.tag_ids = getTrackTags(result.trackId);
                return result;
            }
            if (!m_db.getLastError().empty() && m_db.getLastError().find("SQLITE_DONE") == std::string::npos)
            {
                m_lastErrorMessage = m_db.getLastError();
            }
            return std::nullopt;
        }

        // --- Generic single field update helper ---
        template <typename T>
        DbResult SqliteTrackDatabase::updateSingleTrackField(TrackId trackId, const std::string &columnName, T value,
                                                             std::function<bool(SqliteStatement &, T)> binder)
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
            if (!isOpen())
            {
                return DbResult::failure(DbResultStatus::ErrorConnection, "DB not open for update.");
            }
            m_lastErrorMessage.clear();
            std::string sql = "UPDATE Tracks SET bpm=?, intro_end=?, outro_start=? WHERE track_id = ?;";
            SqliteStatement stmt{m_db, sql};

            if (!stmt.isValid())
            {
                m_lastErrorMessage = "Prepare failed for updateTrackBpm(): " + m_db.getLastError();
                return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
            }
            stmt.addParam(static_cast<int64_t>(am.bpm * 100)); // Store as integer
            if (am.hasIntro)
            {
                stmt.addParam(static_cast<int64_t>(am.introEnd * 1000));
            }
            else
            {
                stmt.addNullParam(); // Use null if no intro end
            }
            if (am.hasOutro)
            {
                stmt.addParam(static_cast<int64_t>(am.outroStart * 1000));
            }
            else
            {
                stmt.addNullParam(); // Use null if no intro end
            }
            stmt.addParam(trackId);

            if (stmt.execute())
            {
                // Check sqlite3_changes(m_db.getInternalHandle()) if you need
                // to confirm rows affected
                spdlog::debug("updateTrackBpm for track_id: {}", trackId);
                return DbResult::success();
            }
            m_lastErrorMessage = "Execute failed for updateTrackBpm(): " + m_db.getLastError();
            return DbResult::failure(DbResultStatus::ErrorDB, m_lastErrorMessage);
        }

        DbResult SqliteTrackDatabase::updateTrackRating(TrackId trackId, int rating)
        {
            return updateSingleTrackField<int>(trackId, "rating", rating,
                                               [](SqliteStatement &s, int val)
                                               {
                                                   return s.addParam(val);
                                               });
        }

        DbResult SqliteTrackDatabase::updateTrackLikedStatus(TrackId trackId, int likedStatus)
        {
            return updateSingleTrackField<int>(trackId, "liked_status", likedStatus,
                                               [](SqliteStatement &s, int val)
                                               {
                                                   return s.addParam(val);
                                               });
        }

        DbResult SqliteTrackDatabase::updateTrackUserNotes(TrackId trackId, const std::string &notes)
        {
            return updateSingleTrackField<const std::string &>(trackId, "user_notes", notes,
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
            return updateSingleTrackField<int>(trackId, "is_missing", isMissing ? 1 : 0,
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
            SqliteStatement stmt{m_db, "SELECT track_id, tag_id FROM TrackTags WHERE "
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

        int SqliteTrackDatabase::getTotalTrackCount(const TrackQueryArgs &args) const
        {
            if (!isOpen())
                return -1; // Indicate error
            m_lastErrorMessage.clear();
            SqliteStatement stmt{m_db};
            SqliteStatementConstruction stmtConstruction{stmt};
            if (!stmtConstruction.createCountStatement(args))
            {
                m_lastErrorMessage = "Failed to create select statement: " + m_db.getLastError();
                return -1;
            }
            if (stmt.isValid())
            {
                if (stmt.getNextResult())
                {
                    return stmt.getInt32(0);
                }
            }
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
    } // namespace database
} // namespace jucyaudio
