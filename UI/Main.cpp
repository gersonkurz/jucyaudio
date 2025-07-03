#include <Config/toml_backend.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <UI/MainComponent.h>
#include <UI/Settings.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/spdlog.h>

#if _WIN32 // Or specific MSVC checks
#pragma comment(lib, "aubio.lib")
#pragma comment(lib, "pthreadVC2.lib")
#pragma comment(lib, "libfftw3f-3.lib")
#pragma comment(lib, "libsndfile-1.lib") // Or sndfile.lib
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swresample.lib")
#pragma comment(lib, "libmp3lame.lib")
// Add any other direct dependencies of these if needed
#endif

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

namespace jucyaudio
{
    namespace ui
    {
        std::string g_strConfigFilename;

        //==============================================================================
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

            //==============================================================================
            /*
                This class implements the desktop window that contains an instance of
                our MainComponent class.
            */
            class MainWindow : public juce::DocumentWindow
            { // Or whatever your main window class is
            public:
                MainWindow(const juce::String &name, juce::ApplicationCommandManager &commandManager)
                    : DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(ResizableWindow::backgroundColourId),
                                     DocumentWindow::allButtons)
                {
                    setUsingNativeTitleBar(true);
                    m_pMainComponent = new jucyaudio::ui::MainComponent{commandManager}; // Create MainComponent
                    setContentOwned(m_pMainComponent, true);                             // Set as content

// If MainComponent directly handles menus for native Mac bar:
#if JUCE_MAC
                                                             // MainComponent::setMacMainMenu was already called from MainComponent constructor
#else
                                                             // If menu is in-window, MainComponent already added its m_menuBar
#endif
                    setMenuBar(m_pMainComponent);
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
                JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
            };

            //==============================================================================
            void initialise([[maybe_unused]] const juce::String &commandLine) override
            {
                setupLogging();
                setupPropertiesFile();

                mainWindow = std::make_unique<MainWindow>(getApplicationName(), commandManager);

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

                juce::Logger::writeToLog("Properties file location: " + g_strConfigFilename);
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
