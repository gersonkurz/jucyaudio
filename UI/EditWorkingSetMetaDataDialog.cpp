#include <UI/EditWorkingSetMetaDataDialog.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        EditWorkingSetMetaDataDialog::EditWorkingSetMetaDataDialog(const WorkingSetInfo &workingSetInfo, OnDialogFinished onFinishedCallback)
            : MetaDataEditorDialogBase{
                "Working Set Details",
                workingSetInfo.name,
                workingSetInfo.numberOfTracks,
                workingSetInfo.totalDuration,
                workingSetInfo.timestamp,
                std::move(onFinishedCallback)
              },
              m_workingSetInfo{workingSetInfo},
              m_initialMixNumber{0}
        {
            // Get the current next mix number
            m_initialMixNumber = theTrackLibrary.getWorkingSetManager().getNextMixNumber(workingSetInfo.id);

            // Add mix number field
            m_mixNumberLabel.setText("Next Mix #:", juce::dontSendNotification);
            m_mixNumberEditor.setText(juce::String(m_initialMixNumber));
            m_mixNumberEditor.setInputRestrictions(6, "0123456789"); // Only allow digits, max 6 chars

            addAndMakeVisible(m_mixNumberLabel);
            addAndMakeVisible(m_mixNumberEditor);

            // Increase dialog height to accommodate new field
            setSize(300, 290);
        }

        void EditWorkingSetMetaDataDialog::resized()
        {
            // Call base class layout first
            MetaDataEditorDialogBase::resized();

            // Now add our custom field below the base fields
            auto bounds = getLocalBounds().reduced(10);

            // Skip past the existing fields (title + name + 3 stats + spacing)
            bounds.removeFromTop(30);  // title //-V525
            bounds.removeFromTop(10);  // spacing
            bounds.removeFromTop(30);  // name
            bounds.removeFromTop(25);  // tracks
            bounds.removeFromTop(25);  // duration
            bounds.removeFromTop(25);  // timestamp
            bounds.removeFromTop(10);  // spacing

            // Add mix number field
            auto mixNumberArea = bounds.removeFromTop(25);
            m_mixNumberLabel.setBounds(mixNumberArea.removeFromLeft(80));
            m_mixNumberEditor.setBounds(mixNumberArea);
        }

        bool EditWorkingSetMetaDataDialog::performRename(const std::string& newName)
        {
            bool success = true;

            // Update name if changed
            if (newName != m_workingSetInfo.name)
            {
                success = theTrackLibrary.getWorkingSetManager().renameWorkingSet(m_workingSetInfo.id, newName);
                if (!success)
                {
                    return false;
                }
            }

            // Update mix number if changed
            const int newMixNumber = m_mixNumberEditor.getText().getIntValue();
            if (newMixNumber != m_initialMixNumber && newMixNumber > 0)
            {
                success = theTrackLibrary.getWorkingSetManager().setNextMixNumber(m_workingSetInfo.id, newMixNumber);
                if (!success)
                {
                    spdlog::error("Failed to update next mix number for working set {}", m_workingSetInfo.id);
                    return false;
                }
            }

            return success;
        }

        std::string EditWorkingSetMetaDataDialog::getErrorMessage() const
        {
            return "Failed to update the working set metadata.";
        }

    } // namespace ui
} // namespace jucyaudio
