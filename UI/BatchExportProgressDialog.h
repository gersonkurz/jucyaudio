#pragma once

#include <Audio/Includes/IMixExporter.h>
#include <Database/Includes/IMixManager.h>
#include <atomic>
#include <functional>
#include <thread>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        class BatchExportProgressDialog : public juce::Component,
                                          public juce::Button::Listener
        {
        public:
            BatchExportProgressDialog(std::vector<database::IMixManager::ScheduledExport> mixes,
                                      const audio::IMixExporter& exporter,
                                      std::function<void(bool success)> onCompletion = nullptr);
            ~BatchExportProgressDialog() override;

            void paint(juce::Graphics& g) override;
            void resized() override;
            void buttonClicked(juce::Button* button) override;

            static void launch(std::vector<database::IMixManager::ScheduledExport> mixes,
                               const audio::IMixExporter& exporter,
                               juce::Component* parentToCenterOn = nullptr,
                               std::function<void(bool success)> onCompletion = nullptr);

        private:
            void startTask();
            void handleProgressUpdate(int overallPercent,
                                      std::string overallMessage,
                                      int currentPercent,
                                      std::string currentMessage);
            void handleTaskCompleted(bool success, std::string resultMessage);
            void closeDialog(int modalReturnValue);

            std::vector<database::IMixManager::ScheduledExport> m_mixes;
            const audio::IMixExporter& m_exporter;
            std::function<void(bool success)> m_onCompletion;

            juce::LookAndFeel_V4 m_lookAndFeel;

            juce::Label m_titleLabel;
            juce::Label m_overallStatusLabel;
            juce::Label m_currentStatusLabel;
            juce::Label m_resultLabel;
            double m_overallProgressValue{0.0};
            double m_currentProgressValue{0.0};
            juce::ProgressBar m_overallProgressBar;
            juce::ProgressBar m_currentProgressBar;
            juce::TextButton m_actionButton;

            std::atomic<bool> m_shouldCancel{false};
            std::atomic<bool> m_taskIsRunning{false};
            std::atomic<bool> m_taskHasCompleted{false};
            std::atomic<bool> m_finalTaskSuccessState{false};
            std::thread m_taskThread;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BatchExportProgressDialog)
        };
    } // namespace ui
} // namespace jucyaudio
