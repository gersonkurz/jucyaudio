#include <UI/EditWorkingSetMetaDataDialog.h>
#include <Database/TrackLibrary.h>

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

    } // namespace ui
} // namespace jucyaudio
