#include <UI/Plugins/PluginWindow.h>
#include <algorithm>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        namespace
        {
            juce::CriticalSection s_windowLock;
            std::vector<juce::Component::SafePointer<PluginWindow>> s_openWindows;
        }

        PluginWindow *PluginWindow::showForPlugin(const std::shared_ptr<juce::AudioPluginInstance> &plugin)
        {
            if (!plugin)
            {
                return nullptr;
            }

            auto *window = new PluginWindow{plugin};
            window->setVisible(true);
            window->toFront(true);
            return window;
        }

        void PluginWindow::closeAllWindows()
        {
            std::vector<juce::Component::SafePointer<PluginWindow>> windowsCopy;
            {
                const juce::ScopedLock lock{s_windowLock};
                windowsCopy = s_openWindows;
            }

            for (auto &safeWindow : windowsCopy)
            {
                if (auto *window = safeWindow.getComponent())
                {
                    delete window;
                }
            }
        }

        PluginWindow::PluginWindow(std::shared_ptr<juce::AudioPluginInstance> plugin)
            : juce::DocumentWindow{plugin ? plugin->getName() : "Plugin",
                  juce::Colours::darkgrey,
                  juce::DocumentWindow::closeButton},
              m_plugin{std::move(plugin)}
        {
            registerWindow(this);
            setUsingNativeTitleBar(true);

            juce::AudioProcessorEditor *editor = nullptr;
            if (m_plugin)
            {
                if (m_plugin->hasEditor())
                {
                    editor = m_plugin->createEditorIfNeeded();
                }
                else
                {
                    editor = new juce::GenericAudioProcessorEditor(*m_plugin);
                }
            }

            if (editor != nullptr)
            {
                setContentOwned(editor, true);
                centreWithSize(editor->getWidth(), editor->getHeight());
            }
            else
            {
                setSize(400, 200);
            }
        }

        PluginWindow::~PluginWindow()
        {
            unregisterWindow(this);
        }

        const juce::AudioPluginInstance *PluginWindow::getPlugin() const
        {
            return m_plugin.get();
        }

        void PluginWindow::closeButtonPressed()
        {
            delete this;
        }

        void PluginWindow::registerWindow(PluginWindow *window)
        {
            const juce::ScopedLock lock{s_windowLock};
            s_openWindows.emplace_back(window);
        }

        void PluginWindow::unregisterWindow(PluginWindow *window)
        {
            const juce::ScopedLock lock{s_windowLock};
            s_openWindows.erase(
                std::remove_if(s_openWindows.begin(), s_openWindows.end(),
                    [window](const auto &entry)
                    {
                        return entry.getComponent() == window;
                    }),
                s_openWindows.end());
        }
    } // namespace ui
} // namespace jucyaudio
