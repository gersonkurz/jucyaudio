#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace jucyaudio
{
    namespace ui
    {
        class PluginWindow final : public juce::DocumentWindow
        {
        public:
            static PluginWindow *showForPlugin(const std::shared_ptr<juce::AudioPluginInstance> &plugin);
            static void closeAllWindows();

            explicit PluginWindow(std::shared_ptr<juce::AudioPluginInstance> plugin);
            ~PluginWindow() override;

            const juce::AudioPluginInstance *getPlugin() const;

            void closeButtonPressed() override;

        private:
            static void registerWindow(PluginWindow *window);
            static void unregisterWindow(PluginWindow *window);

            std::shared_ptr<juce::AudioPluginInstance> m_plugin;
        };
    } // namespace ui
} // namespace jucyaudio
