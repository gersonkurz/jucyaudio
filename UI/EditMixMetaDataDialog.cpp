#include <UI/EditMixMetaDataDialog.h>
#include <Database/TrackLibrary.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        EditMixMetaDataDialog::EditMixMetaDataDialog(const MixInfo &mixInfo, OnDialogFinished onFinishedCallback)
            : MetaDataEditorDialogBase{
                "Mix Details",
                mixInfo.name,
                mixInfo.numberOfTracks,
                mixInfo.totalDuration,
                mixInfo.timestamp,
                std::move(onFinishedCallback)
              },
              m_mixInfo{mixInfo}
        {
        }

        bool EditMixMetaDataDialog::performRename(const std::string& newName)
        {
            return theTrackLibrary.getMixManager().renameMix(m_mixInfo.mixId, newName);
        }

        std::string EditMixMetaDataDialog::getErrorMessage() const
        {
            return "Failed to rename the mix.";
        }

    } // namespace ui
} // namespace jucyaudio
