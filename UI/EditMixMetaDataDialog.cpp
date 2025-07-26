#include <UI/EditMixMetaDataDialog.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        EditMixMetaDataDialog::EditMixMetaDataDialog(const MixInfo &mixInfo, OnDialogFinished onFinishedCallback)
            : MetaDataEditorDialogBase{
                "Mix Details",
                mixInfo.name,
                "Tracks: " + formatStandardStringNumber(mixInfo.numberOfTracks) + "\n" +
                "Duration: " + durationToString(mixInfo.totalDuration) + "\n" +
                "Created: " + timestampToString(mixInfo.timestamp),
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

        std::string EditMixMetaDataDialog::getCurrentName() const
        {
            return m_mixInfo.name;
        }

    } // namespace ui
} // namespace jucyaudio
