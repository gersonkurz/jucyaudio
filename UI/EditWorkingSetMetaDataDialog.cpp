#include <Database/TrackLibrary.h>
#include <UI/EditWorkingSetMetaDataDialog.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        EditWorkingSetMetaDataDialog::EditWorkingSetMetaDataDialog(const WorkingSetInfo &workingSetInfo, OnDialogFinished onFinishedCallback)
            : m_workingSetInfo{workingSetInfo},
              m_onFinishedCallback{std::move(onFinishedCallback)},
              m_titleLabel{"titleLabel", "Working Set Details"},
              m_saveButton{"Save"},
              m_cancelButton{"Cancel"}
        {
            // Title Label
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(20.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);
            
            // Name Editor
            m_nameLabel.setText("Name:", juce::dontSendNotification);
            m_nameEditor.setText(m_workingSetInfo.name);
            m_nameEditor.setSelectAllWhenFocused(true);

            // Statistics
            std::string statsText = "Tracks: " + formatStandardStringNumber(m_workingSetInfo.numberOfTracks) + "\n" +
                                    "Duration: " + durationToString(m_workingSetInfo.totalDuration) + "\n" +
                                    "Created: " + timestampToString(m_workingSetInfo.timestamp);
            m_statsLabel.setText("Statistics:", juce::dontSendNotification);
            m_statsValueLabel.setText(statsText, juce::dontSendNotification);

            addAndMakeVisible(m_titleLabel);
            addAndMakeVisible(m_nameLabel);
            addAndMakeVisible(m_nameEditor);
            addAndMakeVisible(m_statsLabel);
            addAndMakeVisible(m_statsValueLabel);
            addAndMakeVisible(m_saveButton);
            addAndMakeVisible(m_cancelButton);

            m_saveButton.onClick = [this]
            {
                saveChanges();
            };
            m_cancelButton.onClick = [this]
            {
                closeDialog(false);
            };

            m_saveButton.setClickingTogglesState(true);
            m_saveButton.addShortcut(juce::KeyPress(juce::KeyPress::returnKey));
            m_nameEditor.onReturnKey = [this] { m_saveButton.triggerClick(); };
            m_nameEditor.onEscapeKey = [this] { closeDialog(false); };

            setSize(300, 220);  // Slightly taller to accommodate title
        }

        void EditWorkingSetMetaDataDialog::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void EditWorkingSetMetaDataDialog::resized()
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
        
        void EditWorkingSetMetaDataDialog::parentHierarchyChanged()
        {
            if (isShowing() && !isTimerRunning())
            {
                // Start a short timer to grab focus after the dialog window is active
                startTimer(100);
            }
        }
        
        void EditWorkingSetMetaDataDialog::timerCallback()
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

        void EditWorkingSetMetaDataDialog::saveChanges()
        {
            const auto newName = m_nameEditor.getText().toStdString();
            if (newName.empty() || newName == m_workingSetInfo.name)
            {
                closeDialog(false);
                return;
            }

            if (theTrackLibrary.getWorkingSetManager().renameWorkingSet(m_workingSetInfo.id, newName))
            {
                closeDialog(true);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Failed to rename the working set.");
            }
        }

        void EditWorkingSetMetaDataDialog::closeDialog(bool changed)
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
    } // namespace ui
} // namespace jucyaudio
