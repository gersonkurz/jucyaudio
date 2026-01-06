#pragma once

#include <Audio/AudioVisualizerFIFO.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include <projectM-4/projectM.h>
#include <atomic>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief OpenGL-based music visualizer component using projectM.
         *
         * This component renders projectM visualizations using audio data from
         * an AudioVisualizerFIFO. It uses JUCE's OpenGL support to manage the
         * GL context and renders at a configurable frame rate.
         *
         * Usage:
         * 1. Create the component and add to parent
         * 2. Call setVisualizerFIFO() to connect audio source
         * 3. Call setPresetPath() to load presets
         * 4. Call start() to begin rendering
         */
        class ProjectMComponent : public juce::Component,
                                   public juce::OpenGLRenderer,
                                   private juce::Timer
        {
        public:
            ProjectMComponent();
            ~ProjectMComponent() override;

            // Connect to audio source
            void setVisualizerFIFO(audio::AudioVisualizerFIFO* fifo) { m_visualizerFIFO = fifo; }

            // Set the path to preset files
            void setPresetPath(const juce::File& presetDirectory);

            // Start/stop visualization
            void start();
            void stop();
            bool isRunning() const { return m_isRunning; }

            // Preset control
            void nextPreset();
            void previousPreset();
            void randomPreset();
            void lockPreset(bool locked);
            bool isPresetLocked() const;

            // Get current preset name
            juce::String getCurrentPresetName() const;

            // Get default presets directory (platform-specific)
            static juce::File getDefaultPresetsDirectory();

            // Frame rate control (default 30 fps)
            void setTargetFrameRate(int fps);
            int getTargetFrameRate() const { return m_targetFps; }

            // Track change notification (triggers preset switch if enabled)
            void onTrackChanged();

            // Component overrides
            void paint(juce::Graphics& g) override;
            void resized() override;
            bool keyPressed(const juce::KeyPress& key) override;

            // OpenGLRenderer overrides
            void newOpenGLContextCreated() override;
            void renderOpenGL() override;
            void openGLContextClosing() override;

        private:
            // Timer callback for rendering
            void timerCallback() override;

            // Initialize projectM instance
            bool initializeProjectM();
            void shutdownProjectM();

            // Load presets from directory
            void loadPresetsFromDirectory(const juce::File& directory);

            // OpenGL context
            juce::OpenGLContext m_openGLContext;

            // projectM handle
            projectm_handle m_projectM{nullptr};

            // Audio source
            audio::AudioVisualizerFIFO* m_visualizerFIFO{nullptr};

            // Audio buffer for reading from FIFO
            std::vector<float> m_audioBuffer;
            static constexpr int kAudioBufferSize = 512;

            // Preset management
            juce::File m_presetDirectory;
            juce::StringArray m_presetFiles;
            int m_currentPresetIndex{0};
            std::atomic<bool> m_presetLocked{false};

            // State
            std::atomic<bool> m_isRunning{false};
            std::atomic<bool> m_contextReady{false};
            int m_targetFps{30};

            // Auto-switch timer state
            int m_autoSwitchIntervalSeconds{5};  // Loaded from config
            bool m_switchOnTrackChange{true};    // Loaded from config
            int m_framesSinceLastSwitch{0};      // Counter for auto-switch timing
            int m_framesPerAutoSwitch{150};      // Calculated as fps * seconds

            // Pending actions (set from UI thread, processed in GL thread)
            std::atomic<bool> m_pendingNextPreset{false};
            std::atomic<bool> m_pendingPrevPreset{false};
            std::atomic<bool> m_pendingRandomPreset{false};
            std::atomic<int> m_pendingWidth{0};
            std::atomic<int> m_pendingHeight{0};

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectMComponent)
        };

    } // namespace ui
} // namespace jucyaudio
