#include <UI/EditWorkingSetMetaDataDialog.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        EditWorkingSetMetaDataDialog::EditWorkingSetMetaDataDialog(const WorkingSetInfo &workingSetInfo, OnDialogFinished onFinishedCallback)
            : MetaDataEditorDialogBase{
                "Working Set Details",
                workingSetInfo.name,
                "Tracks: " + formatStandardStringNumber(workingSetInfo.numberOfTracks) + "\n" +
                "Duration: " + durationToString(workingSetInfo.totalDuration) + "\n" +
                "Created: " + timestampToString(workingSetInfo.timestamp),
                std::move(onFinishedCallback)
              },
              m_workingSetInfo{workingSetInfo}
        {
        }

        bool EditWorkingSetMetaDataDialog::performRename(const std::string& newName)
        {
            return theTrackLibrary.getWorkingSetManager().renameWorkingSet(m_workingSetInfo.id, newName);
        }

        std::string EditWorkingSetMetaDataDialog::getErrorMessage() const
        {
            return "Failed to rename the working set.";
        }

        std::string EditWorkingSetMetaDataDialog::getCurrentName() const
        {
            return m_workingSetInfo.name;
        }

    } // namespace ui
} // namespace jucyaudio
