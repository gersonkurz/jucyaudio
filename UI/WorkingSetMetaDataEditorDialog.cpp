#include <Database/TrackLibrary.h>
#include <UI/WorkingSetMetaDataEditorDialog.h>
#include <Utils/AssortedUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        WorkingSetMetaDataEditorDialog::WorkingSetMetaDataEditorDialog(const database::WorkingSetInfo &workingSetInfo, OnDialogFinished onFinishedCallback)
            : m_workingSetInfo{workingSetInfo},
              m_onFinishedCallback{std::move(onFinishedCallback)},
              m_saveButton{"Save"},
              m_cancelButton{"Cancel"}
        {
            // Name Editor
            m_nameLabel.setText("Name:", juce::dontSendNotification);
            m_nameEditor.setText(m_workingSetInfo.name);
            m_nameEditor.setSelectAllWhenFocused(true);

            // Statistics
            std::string statsText = "Tracks: " + std::to_string(m_workingSetInfo.track_count) + "\n" +
                                    "Duration: " + durationToString(m_workingSetInfo.total_duration) + "\n" +
                                    "Created: " + timestampToString(m_workingSetInfo.timestamp);
            m_statsLabel.setText("Statistics:", juce::dontSendNotification);
            m_statsValueLabel.setText(statsText, juce::dontSendNotification);

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

            setSize(300, 200);
        }

        void WorkingSetMetaDataEditorDialog::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void WorkingSetMetaDataEditorDialog::resized()
        {
            auto bounds = getLocalBounds().reduced(10);
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

        void WorkingSetMetaDataEditorDialog::saveChanges()
        {
            const auto newName = m_nameEditor.getText().toStdString();
            if (newName.empty() || newName == m_workingSetInfo.name)
            {
                closeDialog(false);
                return;
            }

            if (database::theTrackLibrary.getWorkingSetManager().renameWorkingSet(m_workingSetInfo.id, newName))
            {
                closeDialog(true);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Failed to rename the working set.");
            }
        }

        void WorkingSetMetaDataEditorDialog::closeDialog(bool changed)
        {
            if (m_onFinishedCallback)
            {
                m_onFinishedCallback(changed);
            }

            if (auto *parent = findParentComponentOfClass<juce::DialogWindow>())
            {
                parent->exitModalState(0);
            }
        }
    } // namespace ui
} // namespace jucyaudio
