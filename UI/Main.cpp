#include <Config/toml_backend.h>
#include <UI/MainComponent.h>
#include <UI/Settings.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/spdlog.h>
#include <Database/Sqlite/SqliteTrackDatabase.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Utils/AssortedUtils.h>
#include <UI/SplashScreenComponent.h>
#include <Database/DatabaseBackupManager.h>
#include <Audio/Plugins/PluginManagerService.h>
#include <Audio/Plugins/PluginChain.h>
#include <Audio/Plugins/MasterPluginChainPersistence.h>
#include <UI/Plugins/PluginWindow.h>
#include <UI/SingletonDialog.h>
#include <Tests/SelfTests.h>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <unordered_map>
#include <functional>
#include <optional>
#include <stdexcept>
#include <cstdlib>
#include <string>
#include <vector>
#include "Utils/LoggingUtils.h"

namespace jucyaudio
{
    namespace ui
    {
        std::string g_strConfigFilename;

        //
        // ==============================================================================
        class jucyaudioApplication : public juce::JUCEApplication, private juce::Timer
        {
            class MainComponent;

        public:
            //==============================================================================
            jucyaudioApplication()
            {
            }

            const juce::String getApplicationName() override //-V839
            {
                return PROJECT_NAME;
            }

            const juce::String getApplicationVersion() override //-V839
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
                SingletonComponentDialog::closeDialog("MasterEffects");
                PluginWindow::closeAllWindows();
                audio::theMasterPluginChain.clear();
                mainWindow = nullptr;
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
                    // Start with window hidden and small
                    setVisible(false); //-V1053
                    setSize(1, 1);
                    setTopLeftPosition(-100, -100); // Position off-screen
                    
                    setUsingNativeTitleBar(true);
                    theThemeManager.applyCurrentTheme(lookAndFeel, this);
                    m_pMainComponent = new jucyaudio::ui::MainComponent{commandManager}; // Create MainComponent
                    setContentOwned(m_pMainComponent, true);                             // Set as content
                    setMenuBar(m_pMainComponent);
                    theThemeManager.applyCurrentTheme(lookAndFeel, getMenuBarComponent());
                    // Apply theme to MainComponent after creation so all child components get the accent color
                    theThemeManager.applyCurrentTheme(lookAndFeel, m_pMainComponent);
                    setResizable(true, true);
                    
                    // Don't center or show yet - wait for explicit show
                    // centreWithSize(getWidth(), getHeight());
                    // setVisible(true);
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
                initializeConfigRoot();
                setupLogging();
                generateSampleConfigIfNeeded();

                // Headless self test: no splash, no main window, no plugin scan. Deferred to the timer
                // like the normal startup path, because quit() needs a message loop that is running.
                // Matched as a whole argument: this mode writes to the database and moves files about,
                // so it must not switch on because some other argument happens to contain the text.
                if (juce::StringArray::fromTokens(commandLine, true).contains(kSelfTestScanFlag))
                {
                    spdlog::info("Command line requested {}; starting headless.", kSelfTestScanFlag);
                    m_selfTestRequested = true;
                    m_initPhase = 1;
                    startTimer(50);
                    return;
                }

                spdlog::info("Creating splash screen...");

                // Create splash screen
                splashScreen = std::make_unique<JucyAudioSplashScreen>();
                splashScreen->setAlwaysOnTop(true);
                splashScreen->addToDesktop(juce::ComponentPeer::windowHasDropShadow);
                splashScreen->setVisible(true);
                splashScreen->toFront(true);

                spdlog::info("Splash screen created, visible: {}, bounds: {},{},{},{}",
                    splashScreen->isVisible(),
                    splashScreen->getX(), splashScreen->getY(),
                    splashScreen->getWidth(), splashScreen->getHeight());

                // NOTE: Removed runDispatchLoopUntil - it causes beachball on macOS
                // The splash will be drawn when the event loop runs after initialise() returns

                // Start a one-shot timer for deferred initialization
                m_initPhase = 1;
                startTimer(50);  // Slightly longer delay to ensure splash is rendered
            }

            void timerCallback() override
            {
                spdlog::info("Begin timerCallback");
                stopTimer();

                if (m_selfTestRequested)
                {
                    runSelfTestAndQuit();
                    return;
                }

                // Load settings first to get backup configuration
                config::TomlBackend backend{g_strConfigFilename};
                config::theSettings.load(backend);

                audio::thePluginManagerService.initialize(m_configRoot);

                // Determine database path using expandPath for ${VAR} expansion
                const auto& configuredDbFilename = config::theSettings.database.filename.get();
                std::filesystem::path dbPath;
                if (!configuredDbFilename.empty())
                {
                    // Expand environment variables in configured path
                    dbPath = expandPath(configuredDbFilename);
                    spdlog::info("Using configured database path: {}", pathToString(dbPath));
                }
                else
                {
                    // Default database filename in config root
                    dbPath = m_configRoot / "jucyaudio.db";
                    spdlog::info("Using default database path: {}", pathToString(dbPath));
                }

                // Perform database backup check before the database is opened
                {
                    database::DatabaseBackupManager backupManager;
                    // dryRunPruning is the inverse of enablePruning setting
                    const bool dryRunPruning = !config::theSettings.backupSettings.enablePruning.get();
                    backupManager.performBackupCheck(config::theSettings, dbPath, false, dryRunPruning);
                }

                theThemeManager.initialize(getThemesDirectoryPath(), config::theSettings.uiSettings.theme.get());
                
                if (theTrackLibrary.initialise(dbPath))
                {
                    spdlog::info("TrackLibrary initialised successfully");
                    audio::MasterPluginChainPersistence::loadFromDatabase();
                }
                else
                {
                    spdlog::error("TrackLibrary FAILED to initialise: {}", theTrackLibrary.getLastError());
                }
                
                // Create main window but DON'T add to desktop yet
                spdlog::info("Creating main window...");
                juce::String windowTitle = getApplicationName() + " " + getApplicationVersion();
                mainWindow = std::make_unique<MainWindow>(windowTitle, commandManager, m_lookAndFeel);
                
                if (auto *mainComp = mainWindow->getMainComponent())
                {
                    commandManager.registerAllCommandsForTarget(mainComp);
                }
                
                // Position window
                mainWindow->centreWithSize(1200, 800);

                // Now switch windows
                spdlog::info("Switching from splash to main...");
                splashScreen->setVisible(false);
                mainWindow->setVisible(true);
                mainWindow->toFront(true);

#if JUCE_WINDOWS
                if (auto* peer = mainWindow->getPeer())
                {
                    const auto engines = peer->getAvailableRenderingEngines();
                    spdlog::info("Available rendering engines: {}", engines.joinIntoString(", ").toStdString());

                    const int32_t rendererIndex = 1;
                    spdlog::info("Applying renderer override JUCYAUDIO_RENDERER={} ({})",
                                    rendererIndex,
                                    engines[rendererIndex].toStdString());
                    peer->setCurrentRenderingEngine(rendererIndex);
                }
#endif

                splashScreen = nullptr;
                spdlog::info("Initialization complete");
            }
        private:
            std::filesystem::path m_configRoot;  // Cached config root path
            bool m_selfTestRequested = false;

            static constexpr const char *kSelfTestScanFlag = "--selftest-scan";

            /// @brief Opens the database, runs the scan self test, and ends the process with its result.
            /// Deliberately skips the plugin manager, the backup check and the theme manager: none of
            /// them is under test, and the backup check in particular has no business running against a
            /// scratch database that is about to be scribbled on.
            void runSelfTestAndQuit()
            {
                // Settings are already loaded by setupLogging(); nothing here needs them again. Note what
                // is deliberately absent: the configured database path is never consulted, because it may
                // name the real library and opening a database is not a read-only act - it can run schema
                // migrations. The self test decides its own location, and refuses to run without one.
                std::filesystem::path selfTestRoot;
                std::filesystem::path databasePath;
                const auto refusal = tests::prepareSelfTestEnvironment(selfTestRoot, databasePath);

                int result = 1;
                if (!refusal.empty())
                {
                    spdlog::error("[SelfTest] Refusing to run: {}", refusal);
                }
                else if (theTrackLibrary.initialise(databasePath))
                {
                    // Both suites always run, and the exit code is the worse of the two. Stopping after
                    // the first failure would hide whatever the second had to say, and on a run nobody is
                    // watching that is the report you wanted.
                    const auto scanResult = tests::runScanSelfTest(selfTestRoot);
                    const auto recoveryResult = tests::runMixRecoverySelfTest(selfTestRoot, databasePath);
                    const auto backupResult = tests::runBackupSelfTest(selfTestRoot);
                    result = (scanResult != 0 || recoveryResult != 0 || backupResult != 0) ? 1 : 0;
                }
                else
                {
                    spdlog::error("[SelfTest] TrackLibrary FAILED to initialise: {}", theTrackLibrary.getLastError());
                }

                spdlog::info("[SelfTest] Exiting with code {}.", result);
                setApplicationReturnValue(result);
                quit();
            }

            void initializeConfigRoot()
            {
                // Get the config root directory (from JUCYAUDIO_CONFIG env var or platform default)
                m_configRoot = getConfigRoot();

                // Create the directory if it doesn't exist
                if (!std::filesystem::exists(m_configRoot))
                {
                    std::error_code ec;
                    std::filesystem::create_directories(m_configRoot, ec);
                    if (ec)
                    {
                        std::cerr << "FATAL: Could not create config directory: " << m_configRoot.string()
                                  << " Error: " << ec.message() << std::endl;
                    }
                }

                // Set JUCYAUDIO_CONFIG environment variable so ${JUCYAUDIO_CONFIG} expands in config files
                // This ensures expandPath() can resolve ${JUCYAUDIO_CONFIG} references
#if defined(_WIN32) || defined(_WIN64)
                _putenv_s("JUCYAUDIO_CONFIG", m_configRoot.string().c_str());
#else
                setenv("JUCYAUDIO_CONFIG", m_configRoot.string().c_str(), 1);
#endif

                // Set the config filename
                g_strConfigFilename = (m_configRoot / "jucyaudio.toml").string();
            }

            void generateSampleConfigIfNeeded()
            {
                std::filesystem::path configPath{g_strConfigFilename};
                if (std::filesystem::exists(configPath))
                {
                    return;  // Config already exists
                }

                spdlog::info("Generating sample configuration file: {}", g_strConfigFilename);

                // Generate sample config with documented defaults
                std::ofstream configFile(configPath);
                if (!configFile)
                {
                    spdlog::error("Failed to create sample config file: {}", g_strConfigFilename);
                    return;
                }

                configFile << "# JucyAudio Configuration File\n";
                configFile << "# Generated automatically on first run\n";
                configFile << "# Paths use ${VAR} syntax for environment variables\n";
                configFile << "# Forward slashes (/) work on all platforms\n";
                configFile << "#\n";
                configFile << "# Config root: " << getDefaultConfigRootTemplate() << "\n";
                configFile << "# Override with JUCYAUDIO_CONFIG environment variable\n";
                configFile << "\n";
                configFile << "[Database]\n";
                configFile << "# Database file path (relative to config root or absolute)\n";
                configFile << "Filename = \"${JUCYAUDIO_CONFIG}/jucyaudio.db\"\n";
                configFile << "\n";
                configFile << "[UI]\n";
                configFile << "Theme = \"light\"\n";
                configFile << "ShowOfflineTracks = false\n";
                configFile << "\n";
                configFile << "[Export]\n";
                configFile << "DefaultArtist = \"Unknown Artist\"\n";
                configFile << "DefaultAlbum = \"Unknown Album\"\n";
                configFile << "DefaultYear = \"2025\"\n";
                configFile << "DefaultGenre = \"Electronic\"\n";
                configFile << "\n";
                configFile << "[MixEditing]\n";
                configFile << "RemoveFromWorkingSetOnDelete = true\n";
                configFile << "AskBeforeRemovingFromWorkingSet = true\n";
                configFile << "ClearWorkingSetAfterExport = true\n";
                configFile << "PreloadWaveformsOnMixOpen = true\n";
                configFile << "DrawStereoWaveforms = false\n";
                configFile << "LinkEnvelopePointsToAttachPoints = true\n";
                configFile << "\n";
                configFile << "[Logging]\n";
                configFile << "# Log levels: trace, debug, info, warn, error, critical, off\n";
                configFile << "log_level = \"info\"\n";
                configFile << "\n";
                configFile << "[Backup]\n";
                configFile << "NumberOfBackups = 5\n";
                configFile << "\n";
                configFile << "[Audio]\n";
                configFile << "EqualizerBypassed = true\n";
                configFile << "ReverbBypassed = true\n";
                configFile << "\n";
                configFile << "[TileRendering]\n";
                configFile << "TileCacheSizeMB = 512\n";
                configFile << "WaveformVerticalZoomPercent = 90\n";
                configFile << "EnableTileCache = true\n";
                configFile << "DebugTileRendering = false\n";

                configFile.close();
                spdlog::info("Sample configuration file created successfully");
            }

            void setupLogging()
            {
                // --- Setup Logging ---
                try
                {
                    // 1. Determine Log File Path (uses config root from initializeConfigRoot)
                    auto logDir = m_configRoot / "Logs";
                    if (!std::filesystem::exists(logDir))
                    {
                        std::error_code ec;
                        std::filesystem::create_directories(logDir, ec);
                        if (ec)
                        {
                            std::cerr << "FATAL: Could not create log directory: " << logDir.string()
                                      << " Error: " << ec.message() << std::endl;
                        }
                    }

                    auto logFile = logDir / "jucyaudio.log";
                    const std::string logFilePath_std = logFile.string();

                    // 2. Create Sinks
                    std::vector<spdlog::sink_ptr> sinks;
                    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath_std, true); // true = truncate if exists
                    sinks.push_back(fileSink);

#if JUCE_WINDOWS
                    auto debugSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
                    sinks.push_back(debugSink);
#endif

                    // 3. Create loggers
                    auto combined_logger = std::make_shared<spdlog::logger>("jucyaudio_logger", begin(sinks), end(sinks));
                    auto conf_logger = std::make_shared<spdlog::logger>("conf", sinks.begin(), sinks.end());
                    conf_logger->set_level(spdlog::level::warn); // Disable config logger by default
                    config::logger = conf_logger;
                    spdlog::register_logger(conf_logger);
                    spdlog::set_default_logger(combined_logger);

                    // 4. Set Log Level from config file
                    config::TomlBackend backend{g_strConfigFilename};
                    config::theSettings.load(backend);
                    setLogLevelFromString(config::theSettings.loggingSettings.logLevel);

                    // 5. Test log message
                    spdlog::info("---------------------------------------------------------");
                    spdlog::info("jucyaudio Application Started. Logging initialised.");

                    try
                    {
                        std::locale loc("en_US.UTF-8");
                        std::locale::global(loc);
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
                    std::cerr << "Log initialisation failed: " << ex.what() << std::endl;
                }
            }

            std::unique_ptr<MainWindow> mainWindow;
            std::unique_ptr<JucyAudioSplashScreen> splashScreen;
            juce::ApplicationCommandManager commandManager;
            int m_initPhase = 0;
        };

        juce::ApplicationCommandManager &getGlobalCommandManager()
        {
            // this is safe because jucyaudioApplication is a singleton
            return dynamic_cast<jucyaudioApplication *>(juce::JUCEApplication::getInstance())->getGlobalCommandManager(); // -V522
        }
    } // namespace ui
} // namespace jucyaudio

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION(jucyaudio::ui::jucyaudioApplication)
