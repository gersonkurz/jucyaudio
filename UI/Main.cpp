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
#include <filesystem>
#include <tuple>
#include <unordered_map>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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
                    setVisible(false);
                    setSize(1, 1);
                    setTopLeftPosition(-100, -100); // Position off-screen
                    
                    setUsingNativeTitleBar(true);
                    theThemeManager.applyCurrentTheme(lookAndFeel, this);
                    m_pMainComponent = new jucyaudio::ui::MainComponent{commandManager}; // Create MainComponent
                    setContentOwned(m_pMainComponent, true);                             // Set as content
                    setMenuBar(m_pMainComponent);
                    theThemeManager.applyCurrentTheme(lookAndFeel, getMenuBarComponent());
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
                setupPropertiesFile();
                setupLogging();
                
                spdlog::info("Creating splash screen...");
               
                // Create splash screen and take ownership with a ScopedPointer.
                splashScreen = std::make_unique<JucyAudioSplashScreen>();

                // Make the splash screen always on top
                splashScreen->setAlwaysOnTop(true);
                splashScreen->addToDesktop(juce::ComponentPeer::windowHasDropShadow);
                splashScreen->setVisible(true);
                splashScreen->toFront(true);
                
                spdlog::info("Splash screen created, visible: {}, bounds: {},{},{},{}", 
                    splashScreen->isVisible(),
                    splashScreen->getX(), splashScreen->getY(),
                    splashScreen->getWidth(), splashScreen->getHeight());
                
                // Process any pending messages to ensure the splash screen is drawn
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

                // Start a one-shot timer. The callback will do the heavy work.
                // Small delay to ensure splash is rendered before heavy work begins
                m_initPhase = 1;
                startTimer(10); 
            }

            void timerCallback() override
            {
                spdlog::info("Begin timerCallback");
                stopTimer();

                // Do ALL initialization first
                config::TomlBackend backend{g_strConfigFilename};
                config::theSettings.load(backend);
                theThemeManager.initialize(getThemesDirectoryPath(), config::theSettings.uiSettings.theme.get());
                
                // Initialize database
                juce::File appDataDir{juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("jucyaudioApp_Dev")};
                if (!appDataDir.exists())
                {
                    appDataDir.createDirectory();
                }
                juce::File dbJuceFile{appDataDir.getChildFile("jucyaudio_library_dev.sqlite")};
                std::filesystem::path dbPath{dbJuceFile.getFullPathName().toStdString()};

                if (theTrackLibrary.initialise(dbPath))
                {
                    spdlog::info("TrackLibrary initialised successfully");
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
                
                // Force the main window to paint itself while hidden
                mainWindow->repaint();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
                
                // Now switch windows
                spdlog::info("Switching from splash to main...");
                splashScreen->setVisible(false);
                mainWindow->setVisible(true);
                mainWindow->toFront(true);
                splashScreen = nullptr;
                
                spdlog::info("Initialization complete");
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
                            std::cerr << "FATAL: Could not create app data directory: " << appDataDir.getFullPathName().toStdString() << std::endl;
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
                    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath_std, true); // true = truncate if exists
                    sinks.push_back(fileSink);

#if JUCE_WINDOWS
                    auto debugSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
                    sinks.push_back(debugSink);
#endif

                    // 3. Create loggers
                    auto combined_logger = std::make_shared<spdlog::logger>("jucyaudio_logger", begin(sinks), end(sinks));
                    auto conf_logger = std::make_shared<spdlog::logger>("conf", sinks.begin(), sinks.end());
                    config::logger = conf_logger;
                    spdlog::register_logger(conf_logger);
                    spdlog::set_default_logger(combined_logger);

                    // 4. Set Log Level from config file
                    config::TomlBackend backend{g_strConfigFilename};
                    config::theSettings.load(backend);
                    auto level_str = config::theSettings.loggingSettings.logLevel.get();
                    auto level = spdlog::level::from_str(level_str);

                    if (level == spdlog::level::off && level_str != "off")
                    {
                        level = spdlog::level::critical;
                    }
                    combined_logger->set_level(level);
                    combined_logger->flush_on(level);
                    conf_logger->set_level(spdlog::level::warn);
                    conf_logger->flush_on(spdlog::level::warn);

                    // 5. Test log message
                    spdlog::info("---------------------------------------------------------");
                    spdlog::info("jucyaudio Application Started. Logging initialised.");
                    spdlog::info("Log file: {}", logFilePath_std);
                    spdlog::info("Log level set to \"{}\"", spdlog::level::to_string_view(level));

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
            return dynamic_cast<jucyaudioApplication *>(juce::JUCEApplication::getInstance())->getGlobalCommandManager();
        }
    } // namespace ui
} // namespace jucyaudio

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION(jucyaudio::ui::jucyaudioApplication)