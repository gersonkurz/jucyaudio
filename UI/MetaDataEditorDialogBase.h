#pragma once

// Standard library includes first
#include <string>
#include <functional>

// JUCE includes
#include <juce_gui_basics/juce_gui_basics.h>

// Project includes
#include <Database/Includes/Constants.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @class MetaDataEditorDialogBase
         * @brief Base class for metadata editor dialogs
         * 
         * This class provides common functionality for editing metadata
         * of various container types (WorkingSet, Mix, etc.) reducing code duplication.
         */
        class MetaDataEditorDialogBase : public juce::Component, private juce::Timer
        {
        public:
            using OnDialogFinished = std::function<void(bool nameChanged, std::string_view newName)>;

            ~MetaDataEditorDialogBase() override = default;

            void paint(juce::Graphics &g) override
            {
                g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(10);
                
                // Title label at the top
                m_titleLabel.setBounds(bounds.removeFromTop(30));
                bounds.removeFromTop(10);  // Add spacing after title
                
                auto topArea = bounds.removeFromTop(120);  // Increased for 3 stats fields
                auto buttonArea = bounds.removeFromBottom(30);

                auto nameArea = topArea.removeFromTop(30);
                m_nameLabel.setBounds(nameArea.removeFromLeft(80));
                m_nameEditor.setBounds(nameArea);

                // Three separate stats fields
                auto tracksArea = topArea.removeFromTop(25);
                m_tracksLabel.setBounds(tracksArea.removeFromLeft(80));
                m_tracksValueLabel.setBounds(tracksArea);
                
                auto durationArea = topArea.removeFromTop(25);
                m_durationLabel.setBounds(durationArea.removeFromLeft(80));
                m_durationValueLabel.setBounds(durationArea);
                
                auto timestampArea = topArea.removeFromTop(25);
                m_timestampLabel.setBounds(timestampArea.removeFromLeft(80));
                m_timestampValueLabel.setBounds(timestampArea);

                m_cancelButton.setBounds(buttonArea.removeFromRight(80));
                buttonArea.removeFromRight(10);
                m_saveButton.setBounds(buttonArea.removeFromRight(80));
            }

            void parentHierarchyChanged() override
            {
                if (isShowing() && !isTimerRunning())
                {
                    // Start a short timer to grab focus after the dialog window is active
                    startTimer(100);
                }
            }

            void timerCallback() override
            {
                stopTimer();
                if (auto* dialogWindow = findParentComponentOfClass<juce::DialogWindow>())
                {
                    if (dialogWindow->isActiveWindow())
                    {
                        m_nameEditor.grabKeyboardFocus();
                    }
                    else
                    {
                        // If window isn't active yet, try again
                        startTimer(50);
                    }
                }
            }

        protected:
            MetaDataEditorDialogBase(std::string_view title,
                                   const std::string& initialName,
                                   int64_t trackCount,
                                   Duration_t duration,
                                   Timestamp_t timestamp,
                                   OnDialogFinished onFinishedCallback)
                : m_initialName{initialName},
                  m_onFinishedCallback{std::move(onFinishedCallback)},
                  m_titleLabel{"titleLabel", title.data()},
                  m_saveButton{"Save"},
                  m_cancelButton{"Cancel"}
            {
                // Title Label
                m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(20.0f)}.boldened());
                m_titleLabel.setJustificationType(juce::Justification::left);
                
                // Name Editor
                m_nameLabel.setText("Name:", juce::dontSendNotification);
                m_nameEditor.setText(initialName);
                m_nameEditor.setSelectAllWhenFocused(true);

                // Statistics - format the values here
                m_tracksLabel.setText("Tracks:", juce::dontSendNotification);
                m_tracksValueLabel.setText(formatStandardStringNumber(trackCount), juce::dontSendNotification);
                
                m_durationLabel.setText("Duration:", juce::dontSendNotification);
                m_durationValueLabel.setText(durationToString(duration), juce::dontSendNotification);
                
                m_timestampLabel.setText("Created:", juce::dontSendNotification);
                m_timestampValueLabel.setText(timestampToString(timestamp), juce::dontSendNotification);

                addAndMakeVisible(m_titleLabel);
                addAndMakeVisible(m_nameLabel);
                addAndMakeVisible(m_nameEditor);
                addAndMakeVisible(m_tracksLabel);
                addAndMakeVisible(m_tracksValueLabel);
                addAndMakeVisible(m_durationLabel);
                addAndMakeVisible(m_durationValueLabel);
                addAndMakeVisible(m_timestampLabel);
                addAndMakeVisible(m_timestampValueLabel);
                addAndMakeVisible(m_saveButton);
                addAndMakeVisible(m_cancelButton);

                m_saveButton.onClick = [this] { saveChanges(); };
                m_cancelButton.onClick = [this] { closeDialog(false); };

                m_saveButton.setClickingTogglesState(true);
                m_saveButton.addShortcut(juce::KeyPress(juce::KeyPress::returnKey));
                m_nameEditor.onReturnKey = [this] { m_saveButton.triggerClick(); };
                m_nameEditor.onEscapeKey = [this] { closeDialog(false); };

                setSize(300, 260);  // Increased height for separate stats fields
            }

            // Pure virtual methods that derived classes must implement
            virtual bool performRename(const std::string& newName) = 0;
            virtual std::string getErrorMessage() const = 0;

        private:
            void saveChanges()
            {
                const auto newName = m_nameEditor.getText().toStdString();
                if (newName.empty() || newName == m_initialName)
                {
                    closeDialog(false);
                    return;
                }

                if (performRename(newName))
                {
                    closeDialog(true);
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, 
                        "Error", 
                        getErrorMessage());
                }
            }

            void closeDialog(bool changed)
            {
                if (m_onFinishedCallback)
                {
                    const auto newName = m_nameEditor.getText().toStdString();
                    m_onFinishedCallback(changed, newName);
                }

                if (auto *parent = findParentComponentOfClass<juce::DialogWindow>())
                {
                    parent->exitModalState(0);
                }
            }

            // Data
            std::string m_initialName;
            OnDialogFinished m_onFinishedCallback;

            // UI Components
            juce::Label m_titleLabel;
            juce::Label m_nameLabel;
            juce::TextEditor m_nameEditor;

            juce::Label m_tracksLabel;
            juce::Label m_tracksValueLabel;
            juce::Label m_durationLabel;
            juce::Label m_durationValueLabel;
            juce::Label m_timestampLabel;
            juce::Label m_timestampValueLabel;

            juce::TextButton m_saveButton;
            juce::TextButton m_cancelButton;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetaDataEditorDialogBase)
        };
    } // namespace ui
} // namespace jucyaudio