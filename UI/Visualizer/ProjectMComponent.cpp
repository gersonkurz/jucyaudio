#include "ProjectMComponent.h"
#include "GlewInitializer.h"
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {

        ProjectMComponent::ProjectMComponent()
            : m_audioBuffer(kAudioBufferSize)
        {
            // Request OpenGL 4.1 Core Profile (projectM v4 requires GLSL 330, which needs OpenGL 3.3+)
            m_openGLContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL4_1);
            m_openGLContext.setRenderer(this);
            m_openGLContext.setContinuousRepainting(false);  // We use Timer instead
            // Note: attachTo() is deferred to start() when component is visible

            // Enable keyboard focus for preset controls
            setWantsKeyboardFocus(true);

            spdlog::info("ProjectMComponent created");
        }

        ProjectMComponent::~ProjectMComponent()
        {
            stop();
            if (m_openGLContext.isAttached())
            {
                m_openGLContext.detach();
            }
            spdlog::info("ProjectMComponent destroyed");
        }

        void ProjectMComponent::setPresetPath(const juce::File& presetDirectory)
        {
            m_presetDirectory = presetDirectory;
            loadPresetsFromDirectory(presetDirectory);
        }

        void ProjectMComponent::start()
        {
            if (!m_isRunning)
            {
                spdlog::info("ProjectMComponent::start() - attaching OpenGL context");

                // Attach OpenGL context now that we're visible
                if (!m_openGLContext.isAttached())
                {
                    m_openGLContext.attachTo(*this);
                }

                m_isRunning = true;
                startTimer(1000 / m_targetFps);
                spdlog::info("Visualizer started at {} fps", m_targetFps);
            }
        }

        void ProjectMComponent::stop()
        {
            if (m_isRunning)
            {
                m_isRunning = false;
                stopTimer();
                spdlog::info("Visualizer stopped");
            }
        }

        void ProjectMComponent::nextPreset()
        {
            m_pendingNextPreset = true;
        }

        void ProjectMComponent::previousPreset()
        {
            m_pendingPrevPreset = true;
        }

        void ProjectMComponent::randomPreset()
        {
            m_pendingRandomPreset = true;
        }

        void ProjectMComponent::lockPreset(bool locked)
        {
            m_presetLocked = locked;
            if (m_projectM)
            {
                projectm_set_preset_locked(m_projectM, locked);
            }
        }

        bool ProjectMComponent::isPresetLocked() const
        {
            return m_presetLocked;
        }

        juce::String ProjectMComponent::getCurrentPresetName() const
        {
            if (m_currentPresetIndex >= 0 && m_currentPresetIndex < m_presetFiles.size())
            {
                return juce::File(m_presetFiles[m_currentPresetIndex]).getFileNameWithoutExtension();
            }
            return "No preset";
        }

        void ProjectMComponent::setTargetFrameRate(int fps)
        {
            m_targetFps = juce::jlimit(1, 120, fps);
            if (m_isRunning)
            {
                stopTimer();
                startTimer(1000 / m_targetFps);
            }
        }

        void ProjectMComponent::paint(juce::Graphics& g)
        {
            // OpenGL renders directly, but paint black if context not ready
            if (!m_contextReady)
            {
                g.fillAll(juce::Colours::black);
            }
        }

        void ProjectMComponent::resized()
        {
            // Store pending resize for GL thread
            m_pendingWidth = getWidth();
            m_pendingHeight = getHeight();
        }

        bool ProjectMComponent::keyPressed(const juce::KeyPress& key)
        {
            if (key == juce::KeyPress('n') || key == juce::KeyPress('N'))
            {
                nextPreset();
                spdlog::info("Next preset requested via keyboard");
                return true;
            }
            if (key == juce::KeyPress('p') || key == juce::KeyPress('P'))
            {
                previousPreset();
                spdlog::info("Previous preset requested via keyboard");
                return true;
            }
            if (key == juce::KeyPress('r') || key == juce::KeyPress('R'))
            {
                randomPreset();
                spdlog::info("Random preset requested via keyboard");
                return true;
            }
            if (key == juce::KeyPress('l') || key == juce::KeyPress('L'))
            {
                lockPreset(!isPresetLocked());
                spdlog::info("Preset lock toggled via keyboard: {}", isPresetLocked());
                return true;
            }
            return false;
        }

        void ProjectMComponent::newOpenGLContextCreated()
        {
            spdlog::info("newOpenGLContextCreated called");

            // Log GL version using our wrapper
            logOpenGLInfo();

            if (initializeProjectM())
            {
                m_contextReady = true;

                // Set initial size with DPI awareness
                const double scale = m_openGLContext.getRenderingScale();
                const int width = juce::roundToInt(getWidth() * scale);
                const int height = juce::roundToInt(getHeight() * scale);
                
                if (width > 0 && height > 0)
                {
                    projectm_set_window_size(m_projectM, static_cast<size_t>(width), static_cast<size_t>(height));
                }

                // Load first non-transition preset if available
                if (!m_presetFiles.isEmpty())
                {
                    // Find first preset that isn't a transition preset
                    m_currentPresetIndex = 0;
                    for (int i = 0; i < m_presetFiles.size(); ++i)
                    {
                        if (!m_presetFiles[i].containsIgnoreCase("Transition"))
                        {
                            m_currentPresetIndex = i;
                            break;
                        }
                    }
                    const auto presetPath = m_presetFiles[m_currentPresetIndex].toStdString();
                    projectm_load_preset_file(m_projectM, presetPath.c_str(), true);
                    spdlog::info("Loaded initial preset: {}", presetPath);
                }
            }
        }

        void ProjectMComponent::renderOpenGL()
        {
            static int renderCount = 0;
            if (renderCount++ < 5)
            {
                spdlog::info("renderOpenGL called, projectM={}, contextReady={}, size={}x{}",
                    (m_projectM != nullptr), m_contextReady.load(), getWidth(), getHeight());
            }

            if (!m_projectM || !m_contextReady)
                return;

            const double scale = m_openGLContext.getRenderingScale();
            
            // Handle pending resize
            const int pendingW = m_pendingWidth.exchange(0);
            const int pendingH = m_pendingHeight.exchange(0);
            if (pendingW > 0 && pendingH > 0)
            {
                const int physicalW = juce::roundToInt(pendingW * scale);
                const int physicalH = juce::roundToInt(pendingH * scale);
                projectm_set_window_size(m_projectM, static_cast<size_t>(physicalW), static_cast<size_t>(physicalH));
            }

            // Handle pending preset changes
            if (m_pendingNextPreset.exchange(false))
            {
                if (!m_presetFiles.isEmpty())
                {
                    m_currentPresetIndex = (m_currentPresetIndex + 1) % m_presetFiles.size();
                    const auto presetPath = m_presetFiles[m_currentPresetIndex].toStdString();
                    projectm_load_preset_file(m_projectM, presetPath.c_str(), true);
                }
            }
            if (m_pendingPrevPreset.exchange(false))
            {
                if (!m_presetFiles.isEmpty())
                {
                    m_currentPresetIndex = (m_currentPresetIndex - 1 + m_presetFiles.size()) % m_presetFiles.size();
                    const auto presetPath = m_presetFiles[m_currentPresetIndex].toStdString();
                    projectm_load_preset_file(m_projectM, presetPath.c_str(), true);
                }
            }
            if (m_pendingRandomPreset.exchange(false))
            {
                if (!m_presetFiles.isEmpty())
                {
                    juce::Random random;
                    m_currentPresetIndex = random.nextInt(m_presetFiles.size());
                    const auto presetPath = m_presetFiles[m_currentPresetIndex].toStdString();
                    projectm_load_preset_file(m_projectM, presetPath.c_str(), true);
                }
            }

            // Set viewport to match physical size
            const int width = juce::roundToInt(getWidth() * scale);
            const int height = juce::roundToInt(getHeight() * scale);
            setGLViewport(0, 0, width, height);

            // Feed audio data from FIFO before rendering
            static int audioLogCount = 0;
            if (m_visualizerFIFO != nullptr)
            {
                const int samplesRead = m_visualizerFIFO->read(m_audioBuffer.data(), kAudioBufferSize);
                if (samplesRead > 0)
                {
                    if (audioLogCount++ < 10)
                    {
                        // Calculate peak level for debugging
                        float peak = 0.0f;
                        for (int i = 0; i < samplesRead; ++i)
                        {
                            peak = std::max(peak, std::abs(m_audioBuffer[static_cast<size_t>(i)]));
                        }
                        spdlog::info("Fed {} audio samples to projectM, peak level: {:.4f}", samplesRead, peak);
                    }
                    projectm_pcm_add_float(m_projectM, m_audioBuffer.data(),
                                           static_cast<unsigned int>(samplesRead),
                                           PROJECTM_MONO);
                }
            }

            // Render the frame
            projectm_opengl_render_frame(m_projectM);
            
            // Check for errors
            static int errorLogCount = 0;
            if (errorLogCount < 10 && checkGLError("renderOpenGL"))
            {
                errorLogCount++;
            }
        }

        void ProjectMComponent::openGLContextClosing()
        {
            spdlog::info("OpenGL context closing");
            m_contextReady = false;
            shutdownProjectM();
        }

        void ProjectMComponent::timerCallback()
        {
            if (m_isRunning && m_contextReady)
            {
                static int triggerCount = 0;
                if (triggerCount++ < 5)
                {
                    spdlog::info("timerCallback: triggering repaint, bounds={}x{}, visible={}, attached={}",
                        getWidth(), getHeight(), isVisible(), m_openGLContext.isAttached());
                }
                m_openGLContext.triggerRepaint();
            }
            else
            {
                static int logCount = 0;
                if (logCount++ < 5)
                {
                    spdlog::info("timerCallback: isRunning={}, contextReady={}",
                        m_isRunning.load(), m_contextReady.load());
                }
            }
        }

        bool ProjectMComponent::initializeProjectM()
        {
            // Initialize GLEW for OpenGL function loading (required by projectM on Windows)
            if (!initializeGlew())
            {
                spdlog::error("Failed to initialize GLEW");
                return false;
            }
            spdlog::info("GLEW initialized successfully, version: {}", getGlewVersionString());

            m_projectM = projectm_create();
            if (!m_projectM)
            {
                spdlog::error("Failed to create projectM instance");
                return false;
            }

            // Configure projectM
            projectm_set_fps(m_projectM, static_cast<unsigned int>(m_targetFps));
            projectm_set_preset_duration(m_projectM, 30.0);  // 30 seconds per preset
            projectm_set_soft_cut_duration(m_projectM, 3.0); // 3 second transitions
            projectm_set_hard_cut_duration(m_projectM, 20.0);
            projectm_set_beat_sensitivity(m_projectM, 2.0f);  // Higher = more responsive to audio
            projectm_set_aspect_correction(m_projectM, true);

            spdlog::info("projectM initialized successfully");
            return true;
        }

        void ProjectMComponent::shutdownProjectM()
        {
            if (m_projectM)
            {
                projectm_destroy(m_projectM);
                m_projectM = nullptr;
                spdlog::info("projectM destroyed");
            }
        }

        void ProjectMComponent::loadPresetsFromDirectory(const juce::File& directory)
        {
            m_presetFiles.clear();

            if (!directory.isDirectory())
            {
                spdlog::warn("Preset directory does not exist: {}", directory.getFullPathName().toStdString());
                return;
            }

            // Recursively find all .milk and .prjm preset files
            for (const auto& entry : juce::RangedDirectoryIterator(directory, true, "*.milk;*.prjm"))
            {
                m_presetFiles.add(entry.getFile().getFullPathName());
            }

            // Sort alphabetically
            m_presetFiles.sort(true);

            spdlog::info("Loaded {} presets from {}", m_presetFiles.size(), directory.getFullPathName().toStdString());
        }

        juce::File ProjectMComponent::getDefaultPresetsDirectory()
        {
            auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

#if JUCE_MAC
            // On macOS, presets are in Contents/Resources inside the bundle
            return appFile.getChildFile("Contents").getChildFile("Resources").getChildFile("presets");
#else
            // On Windows and Linux, presets are next to the executable
            return appFile.getParentDirectory().getChildFile("presets");
#endif
        }

    } // namespace ui
} // namespace jucyaudio