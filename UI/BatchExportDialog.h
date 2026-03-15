#pragma once

#include <Audio/Includes/ActiveExportSettings.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/MixInfo.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        class BatchExportDialog : public juce::Component,
                                  public juce::Button::Listener
        {
        public:
            using OnBatchExportCallback = std::function<void(std::vector<database::IMixManager::ScheduledExport> mixesToExport)>;

            BatchExportDialog(OnBatchExportCallback callback);
            ~BatchExportDialog() override;

            void paint(juce::Graphics& g) override;
            void resized() override;
            void buttonClicked(juce::Button* button) override;

        private:
            void refreshList();
            void handleRemoveSelected();
            void handleExportAll();
            void handleCancel();
            void closeDialog();

            OnBatchExportCallback m_callback;
            std::vector<database::IMixManager::ScheduledExport> m_scheduled;

            // UI
            juce::Label m_titleLabel;
            juce::ListBox m_listBox;

            class ListModel : public juce::ListBoxModel
            {
            public:
                ListModel(BatchExportDialog& owner) : m_owner{owner} {}
                int getNumRows() override;
                void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

            private:
                BatchExportDialog& m_owner;
            };

            ListModel m_listModel{*this};

            juce::TextButton m_removeButton{"Remove"};
            juce::TextButton m_exportAllButton{"Export All"};
            juce::TextButton m_cancelButton{"Close"};

            juce::LookAndFeel_V4 m_lookAndFeel;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BatchExportDialog)
        };

    } // namespace ui
} // namespace jucyaudio
