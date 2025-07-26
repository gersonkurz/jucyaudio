#pragma once

// Standard library includes first
#include <string>
#include <functional>

// JUCE includes
#include <juce_gui_basics/juce_gui_basics.h>

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
                
                auto topArea = bounds.removeFromTop(80);
                auto buttonArea = bounds.removeFromBottom(30);

                auto nameArea = topArea.removeFromTop(30);
                m_nameLabel.setBounds(nameArea.removeFromLeft(80));
                m_nameEditor.setBounds(nameArea);

                auto statsArea = topArea.removeFromTop(50);
                m_statsLabel.setBounds(statsArea.removeFromLeft(80));
                m_statsValueLabel.setBounds(statsArea);

                m_saveButton.setBounds(buttonArea.removeFromRight(80));
                buttonArea.removeFromRight(10);
                m_cancelButton.setBounds(buttonArea.removeFromRight(80));
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
                                   const std::string& statsText,
                                   OnDialogFinished onFinishedCallback)
                : m_onFinishedCallback{std::move(onFinishedCallback)},
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

                // Statistics
                m_statsLabel.setText("Statistics:", juce::dontSendNotification);
                m_statsValueLabel.setText(statsText, juce::dontSendNotification);

                addAndMakeVisible(m_titleLabel);
                addAndMakeVisible(m_nameLabel);
                addAndMakeVisible(m_nameEditor);
                addAndMakeVisible(m_statsLabel);
                addAndMakeVisible(m_statsValueLabel);
                addAndMakeVisible(m_saveButton);
                addAndMakeVisible(m_cancelButton);

                m_saveButton.onClick = [this] { saveChanges(); };
                m_cancelButton.onClick = [this] { closeDialog(false); };

                m_saveButton.setClickingTogglesState(true);
                m_saveButton.addShortcut(juce::KeyPress(juce::KeyPress::returnKey));
                m_nameEditor.onReturnKey = [this] { m_saveButton.triggerClick(); };
                m_nameEditor.onEscapeKey = [this] { closeDialog(false); };

                setSize(300, 220);  // Slightly taller to accommodate title
            }

            // Pure virtual methods that derived classes must implement
            virtual bool performRename(const std::string& newName) = 0;
            virtual std::string getErrorMessage() const = 0;
            virtual std::string getCurrentName() const = 0;

            // Protected method to get editor text
            std::string getEditorText() const { return m_nameEditor.getText().toStdString(); }

        private:
            void saveChanges()
            {
                const auto newName = m_nameEditor.getText().toStdString();
                if (newName.empty() || newName == getCurrentName())
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
            OnDialogFinished m_onFinishedCallback;

            // UI Components
            juce::Label m_titleLabel;
            juce::Label m_nameLabel;
            juce::TextEditor m_nameEditor;

            juce::Label m_statsLabel;
            juce::Label m_statsValueLabel;

            juce::TextButton m_saveButton;
            juce::TextButton m_cancelButton;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetaDataEditorDialogBase)
        };
    } // namespace ui
} // namespace jucyaudio