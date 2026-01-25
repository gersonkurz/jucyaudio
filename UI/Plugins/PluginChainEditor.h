#pragma once

#include <Audio/Plugins/PluginChain.h>
#include <Audio/Plugins/PluginManagerService.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        class PluginWindow;

        class PluginChainEditor final : public juce::Component,
                                        public juce::Button::Listener,
                                        public juce::ListBoxModel
        {
        public:
            PluginChainEditor();
            ~PluginChainEditor() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            int getNumRows() override;
            void paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected) override;
            void selectedRowsChanged(int lastRowSelected) override;

            void buttonClicked(juce::Button *button) override;

        private:
            void refreshAvailablePlugins();
            void addSelectedPlugin();
            void removeSelectedPlugin();
            void moveSelectedPlugin(int direction);
            void openSelectedPluginEditor();
            void updateChain();
            void updateButtons();
            void closeEditorsForPlugin(const juce::AudioPluginInstance *plugin);

            juce::Label m_titleLabel;
            juce::ComboBox m_availablePluginsCombo;
            juce::TextButton m_refreshButton{"Refresh"};
            juce::TextButton m_scanButton{"Scan..."};
            juce::TextButton m_addButton{"Add"};

            juce::ListBox m_chainList{"pluginChainList", this};
            juce::TextButton m_removeButton{"Remove"};
            juce::TextButton m_moveUpButton{"Up"};
            juce::TextButton m_moveDownButton{"Down"};
            juce::TextButton m_openEditorButton{"Open UI"};

            std::vector<juce::PluginDescription> m_availablePlugins;
            std::vector<std::shared_ptr<juce::AudioPluginInstance>> m_chain;
            std::unordered_map<const juce::AudioPluginInstance *, juce::Component::SafePointer<PluginWindow>> m_openEditors;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChainEditor)
        };
    } // namespace ui
} // namespace jucyaudio
