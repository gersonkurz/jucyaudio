#include <Config/toml_backend.h>
#include <UI/MainComponent.h>
#include <UI/Settings.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/spdlog.h>
#include <Database/Sqlite/SqliteTrackDatabase.h>

#include "Windows.h"

/*
- Database: Disgintuish Node-level actions from track-level actions
- UI: support multi-track selection
- UI have an action that says "create mix out of these tracks".
- Preliminary: create "auto-mix" (e.g. hardcoded 5 seconds transition old/new)
- Future TODO: create proper dialog to fine-tune this
- UI/Database: have / show nodes for mixes (that might mean, playback in this node is different from regular playback)!
- Improve generic status-bar
- support actions on multiple selected tracks (e.g. delete, move, copy, etc.)

*/

#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Utils/AssortedUtils.h> // For normalizeForCache
#include <filesystem>
#include <spdlog/spdlog.h>
#include <tuple>
#include <unordered_map>
#include <functional>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Utils/AssortedUtils.h> // Or wherever you will call this from
#include <spdlog/spdlog.h>

#include <filesystem>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// ICU headers for normalization
#include <unicode/unorm2.h>
#include <unicode/ustring.h>

// ==========================================================================
// ==                  START OF MIGRATION IMPLEMENTATION                   ==
// ==========================================================================

// This is the correct, simple schema for the "Pure Cache" architecture.
// The database has no knowledge of case-insensitivity.
const std::vector<std::string> MIGRATION_TARGET_SCHEMA_SQL = {"PRAGMA foreign_keys = ON;",
    "CREATE TABLE Folders (folder_id INTEGER PRIMARY KEY, parent_id INTEGER, name TEXT NOT NULL, root_path TEXT, FOREIGN KEY (parent_id) REFERENCES "
    "Folders(folder_id) ON DELETE CASCADE);",
    "CREATE INDEX idx_folders_parent_name ON Folders(parent_id, name);",
    "CREATE TABLE Tracks (track_id INTEGER PRIMARY KEY, folder_id INTEGER NOT NULL, filename TEXT NOT NULL, last_modified_fs INTEGER, filesize_bytes INTEGER, "
    "date_added INTEGER, last_scanned INTEGER, title TEXT, artist_name TEXT, album_title TEXT, album_artist_name TEXT, track_number INTEGER, disc_number "
    "INTEGER, year INTEGER, duration INTEGER, samplerate INTEGER, channels INTEGER, bitrate INTEGER, codec_name TEXT, bpm INTEGER, intro_end INTEGER, "
    "outro_start INTEGER, key_string TEXT, beat_locations_json TEXT, rating INTEGER DEFAULT 0, liked_status INTEGER DEFAULT 0, play_count INTEGER DEFAULT 0, "
    "last_played INTEGER, internal_content_hash TEXT, user_notes TEXT, is_missing INTEGER DEFAULT 0, status TEXT NOT NULL DEFAULT 'unknown', FOREIGN KEY "
    "(folder_id) REFERENCES Folders(folder_id) ON DELETE CASCADE);",
    "CREATE INDEX idx_tracks_parent_filename ON Tracks(folder_id, filename);",
    "CREATE TABLE Tags (tag_id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE);",
    "CREATE TABLE TrackTags (track_id INTEGER NOT NULL, tag_id INTEGER NOT NULL, PRIMARY KEY (track_id, tag_id), FOREIGN KEY (track_id) REFERENCES "
    "Tracks(track_id) ON DELETE CASCADE, FOREIGN KEY (tag_id) REFERENCES Tags(tag_id) ON DELETE CASCADE);",
    "CREATE INDEX idx_tracktags_tag_id ON TrackTags (tag_id);",
    "CREATE TABLE WorkingSets(ws_id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE, timestamp INTEGER, sort_order TEXT);",
    "CREATE TABLE WorkingSetTracks(ws_id INTEGER NOT NULL, track_id INTEGER NOT NULL, PRIMARY KEY(ws_id, track_id), FOREIGN KEY(ws_id) REFERENCES "
    "WorkingSets(ws_id) ON DELETE CASCADE, FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE);",
    "CREATE TABLE Mixes(mix_id INTEGER PRIMARY KEY, name  TEXT NOT NULL UNIQUE COLLATE NOCASE, timestamp INTEGER, track_count INTEGER, total_length INTEGER, "
    "source_ws_id INTEGER, status TEXT DEFAULT 'New', undo_stack_position INTEGER DEFAULT 0, FOREIGN KEY(source_ws_id) REFERENCES WorkingSets(ws_id));",
    "CREATE TABLE MixTracks(mix_id INTEGER NOT NULL, track_id INTEGER NOT NULL, order_in_mix INTEGER NOT NULL, mix_data TEXT NOT NULL, PRIMARY KEY(mix_id, "
    "track_id), FOREIGN KEY(mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE, FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE);",
    "CREATE INDEX idx_mixtracks_order ON MixTracks(mix_id, order_in_mix);",
    "CREATE TABLE TrackMarkers (marker_id INTEGER PRIMARY KEY, track_id INTEGER NOT NULL, position_ms INTEGER NOT NULL, comment TEXT NOT NULL, created_at "
    "INTEGER NOT NULL, updated_at INTEGER NOT NULL, color TEXT, emoji TEXT, FOREIGN KEY (track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE);",
    "CREATE INDEX idx_trackmarkers_track_id ON TrackMarkers (track_id);"};

// Helper struct and hash specialization for the folder cache key
struct FolderCacheKey
{
    int64_t parentId;
    std::string normalizedName;

    bool operator==(const FolderCacheKey &other) const
    {
        return parentId == other.parentId && normalizedName == other.normalizedName;
    }
};

namespace std
{
    template <> struct hash<FolderCacheKey>
    {
        size_t operator()(const FolderCacheKey &k) const
        {
            return hash<int64_t>()(k.parentId) ^ (hash<string>()(k.normalizedName) << 1);
        }
    };
} // namespace std

// Finds or creates a hierarchical folder structure in the database.
// This is the final, correct, and fast implementation.
// It relies SOLELY on the in-memory cache for uniqueness checks.
std::optional<int64_t> getOrCreateFolderId(
    jucyaudio::database::SqliteDatabase &db, const std::filesystem::path &path, std::unordered_map<FolderCacheKey, int64_t> &cache)
{
    using namespace jucyaudio;
    int64_t currentParentId = -1;

    // (Path splitting logic remains the same)
    std::vector<std::string> parts;
    std::filesystem::path current = path;
    while (current.has_relative_path())
    {
        parts.insert(parts.begin(), pathToString(current.filename()));
        current = current.parent_path();
    }
    parts.insert(parts.begin(), pathToString(current.root_path()));

    for (const auto &part : parts)
    {
        if (part.empty() || part == "\\" || part == "/")
            continue;

        auto normalizedResult = normalizeForCache(part);
        if (!normalizedResult)
        {
            spdlog::error("FATAL: Could not normalize path component '{}' in path '{}'", part, pathToString(path));
            return std::nullopt;
        }

        const std::string &normalizedPart = *normalizedResult;
        FolderCacheKey key = {currentParentId, normalizedPart};

        // --- THE CORRECT LOGIC ---
        // 1. Check the cache.
        auto it = cache.find(key);
        if (it != cache.end())
        {
            // 2. If it's in the cache, it's already in the DB. Use the ID.
            currentParentId = it->second;
        }
        else
        {
            // 3. If it's NOT in the cache, it's NOT in the DB. Insert it.
            jucyaudio::database::SqliteStatement insertStmt(db, "INSERT INTO Folders (parent_id, name, root_path) VALUES (?, ?, ?);");
            (currentParentId != -1) ? insertStmt.addParam(currentParentId) : insertStmt.addNullParam();
            insertStmt.addParam(part);
            (currentParentId == -1) ? insertStmt.addParam(pathToString(path.root_path())) : insertStmt.addNullParam();

            if (!insertStmt.execute())
            {
                // An insert should never fail here unless there's a disk error.
                throw std::runtime_error("Failed to insert new folder: " + part + " with error: " + db.getLastError());
            }
            currentParentId = db.getLastInsertRowId();

            // 4. Add the newly created folder to the cache.
            cache[key] = currentParentId;
        }
    }
    return currentParentId;
}

// Main migration orchestrator function
void run_full_migration(const std::filesystem::path &sourcePath, const std::filesystem::path &destPath)
{
    using namespace jucyaudio::database;
    using namespace jucyaudio;

    SqliteDatabase sourceDb, destDb;

    spdlog::info("Connecting to source: {}", pathToString(sourcePath));
    if (!sourceDb.open(pathToString(sourcePath)))
        throw std::runtime_error("Failed to open source DB.");

    spdlog::info("Connecting to destination: {}", pathToString(destPath));
    if (!destDb.open(pathToString(destPath)))
        throw std::runtime_error("Failed to open destination DB.");

    SqliteTransaction transaction(destDb);

    spdlog::info("Step 1: Creating new schema...");
    for (const auto &sql : MIGRATION_TARGET_SCHEMA_SQL)
    {
        if (!destDb.execute(sql))
        {
            throw std::runtime_error("Failed to create new schema: " + destDb.getLastError());
        }
    }

    spdlog::info("Step 2: Migrating Tracks and Folders...");
    std::unordered_map<FolderCacheKey, int64_t> folderCache;
    const std::string selectSql =
        "SELECT track_id, folder_id, filepath, last_modified_fs, filesize_bytes, date_added, last_scanned, title, artist_name, album_title, album_artist_name, "
        "track_number, disc_number, year, duration, samplerate, channels, bitrate, codec_name, bpm, intro_end, outro_start, key_string, beat_locations_json, "
        "rating, liked_status, play_count, last_played, internal_content_hash, user_notes, is_missing, IFNULL(status, 'unknown') FROM Tracks;";
    SqliteStatement selectTracks(sourceDb, selectSql);

    int count = 0;
    while (selectTracks.getNextResult())
    {
        std::filesystem::path filepath(pathFromString(selectTracks.getText(2)));
        auto directory = filepath.parent_path();
        auto filename = filepath.filename();

        auto parentFolderIdResult = getOrCreateFolderId(destDb, directory, folderCache);
        if (!parentFolderIdResult)
        {
            throw std::runtime_error("Could not process folder for path: " + pathToString(directory));
        }
        int64_t parentFolderId = *parentFolderIdResult;

        SqliteStatement insertTrack(destDb,
            "INSERT INTO Tracks (track_id, folder_id, filename, last_modified_fs, filesize_bytes, date_added, last_scanned, title, artist_name, album_title, "
            "album_artist_name, track_number, disc_number, year, duration, samplerate, channels, bitrate, codec_name, bpm, intro_end, outro_start, key_string, "
            "beat_locations_json, rating, liked_status, play_count, last_played, internal_content_hash, user_notes, is_missing, status) VALUES "
            "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");

        insertTrack.addParam(selectTracks.getInt64(0));
        insertTrack.addParam(parentFolderId);
        insertTrack.addParam(pathToString(filename));
        for (int i = 3; i < 32; ++i)
        {
            if (!insertTrack.bindColumnFrom(selectTracks, i))
            {
                throw std::runtime_error("Failed to bind column " + std::to_string(i));
            }
        }

        if (!insertTrack.execute())
        {
            throw std::runtime_error("Failed to insert track: " + destDb.getLastError());
        }
        if (++count % 1000 == 0)
        {
            spdlog::info("Migrated {:L} tracks...", count);
        }
    }
    spdlog::info("Finished migrating {:L} total tracks.", count);

    spdlog::info("Step 3: Copying remaining tables...");
    const std::vector<std::string> tablesToCopy = {"Tags", "TrackTags", "WorkingSets", "WorkingSetTracks", "Mixes", "MixTracks", "TrackMarkers"};
    for (const auto &tableName : tablesToCopy)
    {
        SqliteStatement selectAll(sourceDb, "SELECT * FROM " + tableName + ";");
        while (selectAll.getNextResult())
        {
            std::string insertSql = "INSERT INTO " + tableName + " VALUES (";
            for (size_t i = 0; i < selectAll.getNumberOfColumns(); ++i)
            {
                insertSql += (i == 0 ? "?" : ",?");
            }
            insertSql += ");";

            SqliteStatement insertRow(destDb, insertSql);
            for (size_t i = 0; i < selectAll.getNumberOfColumns(); ++i)
            {
                if (!insertRow.bindColumnFrom(selectAll, i))
                {
                    throw std::runtime_error("Failed to bind column for table " + tableName);
                }
            }
            if (!insertRow.execute())
            {
                throw std::runtime_error("Failed to copy row to table " + tableName + ": " + destDb.getLastError());
            }
        }
        spdlog::info("Copied table {}.", tableName);
    }

    if (!transaction.commit())
    {
        throw std::runtime_error("Failed to commit migration transaction.");
    }
    spdlog::info("Migration committed successfully!");
}

// ==========================================================================
// ==                   END OF MIGRATION IMPLEMENTATION                    ==
// ==========================================================================
namespace jucyaudio
{

    

    namespace ui
    {
        std::string g_strConfigFilename;

        //
        // ==============================================================================
        class jucyaudioApplication : public juce::JUCEApplication
        {
            class MainComponent;

        public:
            //==============================================================================
            jucyaudioApplication()
            {
            }

            const juce::String getApplicationName() override
            {
                return PROJECT_NAME;
            }

            const juce::String getApplicationVersion() override
            {
                return PROJECT_VERSION;
            }

            bool moreThanOneInstanceAllowed() override
            {
                return true;
            }

            juce::ApplicationCommandManager &getGlobalCommandManager()
            {
                return commandManager;
            }

            void shutdown() override
            {
                // Add your application's shutdown code here..

                mainWindow = nullptr; // (deletes our window)
            }

            //==============================================================================
            void systemRequestedQuit() override
            {
                // This is called when the app is being asked to quit: you can ignore this
                // request and let the app carry on running, or call quit() to allow the app to close.
                quit();
            }

            void anotherInstanceStarted([[maybe_unused]] const juce::String &commandLine) override
            {
                // When another instance of the app is launched while this one is running,
                // this method is invoked, and the commandLine parameter tells you what
                // the other instance's command-line arguments were.
            }

            juce::LookAndFeel_V4 m_lookAndFeel; // Custom LookAndFeel for the app

            //==============================================================================
            /*
                This class implements the desktop window that contains an instance of
                our MainComponent class.
            */
            class MainWindow : public juce::DocumentWindow
            { // Or whatever your main window class is
            public:
                MainWindow(const juce::String &name, juce::ApplicationCommandManager &commandManager, juce::LookAndFeel_V4 &lookAndFeel)
                    : DocumentWindow(name, lookAndFeel.findColour(ResizableWindow::backgroundColourId), DocumentWindow::allButtons)
                {
                    setUsingNativeTitleBar(true);
                    theThemeManager.applyCurrentTheme(lookAndFeel, this);
                    m_pMainComponent = new jucyaudio::ui::MainComponent{commandManager}; // Create MainComponent
                    setContentOwned(m_pMainComponent, true);                             // Set as content
                    setMenuBar(m_pMainComponent);
                    theThemeManager.applyCurrentTheme(lookAndFeel, getMenuBarComponent());
                    setResizable(true, true);
                    centreWithSize(getWidth(), getHeight());
                    setVisible(true);
                }

                auto getMainComponent() const
                {
                    return m_pMainComponent;
                }

                void closeButtonPressed() override
                {
                    JUCEApplication::getInstance()->systemRequestedQuit();
                }

            private:
                jucyaudio::ui::MainComponent *m_pMainComponent;
                juce::TooltipWindow m_tooltipWindow{this, 700}; // 700ms delay before showing
                JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
            };

            std::filesystem::path getThemesDirectoryPath() const
            {
                // This gets the directory containing the executable or the .app bundle
                auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

                juce::File themesDir;
#if JUCE_MAC
                // On macOS, it's in Contents/Resources inside the bundle
                themesDir = appFile.getChildFile("Contents").getChildFile("Resources").getChildFile("themes");
#elif JUCE_WINDOWS
                // On Windows, we placed it next to the executable's directory
                themesDir = appFile.getSiblingFile("themes");
#else
                // Linux fallback
                themesDir = appFile.getSiblingFile("themes");
#endif

                return themesDir.getFullPathName().toStdString();
            }

            //==============================================================================
            void initialise([[maybe_unused]] const juce::String &commandLine) override
            {
                setupLogging();
                setupPropertiesFile();



                //run_full_migration("C:\\Projects\\jucyaudio\\input.sqlite", "C:\\Projects\\jucyaudio\\output.sqlite");
                //TerminateProcess(GetCurrentProcess(), 0);

                config::TomlBackend backend{g_strConfigFilename};
                config::theSettings.load(backend);

                theThemeManager.initialize(getThemesDirectoryPath(), config::theSettings.uiSettings.theme.get());

                mainWindow = std::make_unique<MainWindow>(getApplicationName(), commandManager, m_lookAndFeel);

                // Tell the command manager about your main content component (MainComponent)
                // Assuming MainWindow creates and holds MainComponent
                if (auto *mainComp = mainWindow->getMainComponent())
                { // Hypothetical getter
                    commandManager.registerAllCommandsForTarget(mainComp);
                    // commandManager.setFirstCurrentTarget(mainComp); // Make it a primary target
                }
            }

        private:
            void setupPropertiesFile()
            {
                juce::File appDataDir{
                    juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("jucyaudioApp_Dev")}; // Same base as your DB

                assert(appDataDir.exists()); // should have been created by the spdlog setup

                // Create the settings file in the same directory
                g_strConfigFilename = appDataDir.getChildFile("settings.toml").getFullPathName().toStdString();

                spdlog::info("Properties file location: {}", g_strConfigFilename);
            }

            void setupLogging()
            {

                // --- Setup Logging ---
                try
                {
                    // 1. Determine Log File Path (platform-aware, next to DB)
                    juce::File appDataDir{
                        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("jucyaudioApp_Dev")}; // Same base as your DB

                    if (!appDataDir.exists())
                    {
                        auto result = appDataDir.createDirectory();
                        if (!result.wasOk())
                        {
                            // Critical error, cannot create app data dir for logs/DB
                            // Fallback or assert/error
                            std::cerr << "FATAL: Could not create app data directory: " << appDataDir.getFullPathName().toStdString() << std::endl;
                            // You might want to throw or gracefully exit here
                        }
                    }

                    juce::File logDir{appDataDir.getChildFile("Logs")};
                    if (!logDir.exists())
                    {
                        logDir.createDirectory();
                    }

                    juce::File logFile{logDir.getChildFile("jucyaudio.log")};
                    const std::string logFilePath_std = logFile.getFullPathName().toStdString(); // For spdlog

                    // 2. Create Sinks
                    std::vector<spdlog::sink_ptr> sinks;

                    // Sink 1: File Sink (rotating recommended for long-term use)
                    // For simplicity, basic file sink first. Can change to rotating later.
                    // Max size 10MB, 3 rotated files
                    // auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath_std, 1024 * 1024 * 10, 3);
                    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath_std, true); // true = truncate if exists
                    sinks.push_back(fileSink);

// Sink 2: Console Sink (platform-specific for best output)
#if JUCE_WINDOWS
                    // Use msvc_sink for OutputDebugString on Windows
                    auto debugSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
                    sinks.push_back(debugSink);
                    // You could also add a color console sink if running from a terminal
                    // auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                    // sinks.push_back(consoleSink);
#else // macOS, Linux
      // Standard color console sink
      // auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
      // sinks.push_back(consoleSink);
#endif

                    // 3. Create a Logger with Multiple Sinks
                    // Use a descriptive name, "multi_sink_logger" or your app name
                    auto combined_logger = std::make_shared<spdlog::logger>("jucyaudio_logger", begin(sinks), end(sinks));

                    auto conf_logger = std::make_shared<spdlog::logger>("conf", sinks.begin(), sinks.end());

                    // Set levels per module
                    conf_logger->set_level(spdlog::level::warn);

                    // Optional: Flush level per logger
                    conf_logger->flush_on(spdlog::level::warn);
                    config::logger = conf_logger; // Store the config logger globally

                    // Register them globally
                    spdlog::register_logger(conf_logger);

                    // 4. Set Log Level (can be configured from a file later)
                    combined_logger->set_level(spdlog::level::info); // Set level on the specific logger
                    combined_logger->flush_on(spdlog::level::info);  // Flush frequently during debugging

                    // 5. Register it as the default logger (or use it explicitly)
                    spdlog::set_default_logger(combined_logger);

                    // Test log message
                    spdlog::info("---------------------------------------------------------");
                    spdlog::info("jucyaudio Application Started. Logging initialised.");
                    spdlog::info("Log file: {}", logFilePath_std);
                    spdlog::debug("Debug logging is enabled.");
                    try
                    {
                        std::locale loc("en_US.UTF-8");
                        std::locale::global(loc); // Set the global locale
                        spdlog::info("Locale set to: {}", loc.name());
                        spdlog::info("info: {:L}", 1234567); // Test formatting with thousands separators
                    }
                    catch (const std::runtime_error &e)
                    {
                        spdlog::error("Locale error: {}", e.what());
                    }
                }
                catch (const spdlog::spdlog_ex &ex)
                {
                    // Fallback to std::cerr or std::cout if spdlog init fails
                    std::cerr << "Log initialisation failed: " << ex.what() << std::endl;
                    // You could also try a Juce AlertWindow here, but it might be too early in app init
                }
                // ... rest of your initialise method ...
            }

            std::unique_ptr<MainWindow> mainWindow;
            juce::ApplicationCommandManager commandManager;
        };

        juce::ApplicationCommandManager &getGlobalCommandManager()
        {
            return dynamic_cast<jucyaudioApplication *>(juce::JUCEApplication::getInstance())->getGlobalCommandManager();
        }
    } // namespace ui
} // namespace jucyaudio

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION(jucyaudio::ui::jucyaudioApplication)
