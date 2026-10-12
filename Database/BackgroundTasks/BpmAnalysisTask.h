#pragma once

#include <Database/Includes/ILongRunningTask.h>
#include <Database/Includes/IWorkingSetManager.h>
#include <Database/Includes/TrackInfo.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            /// @brief Why BPM analysis did not analyze a track.
            enum class SkipReason
            {
                FileMissing, ///< There is no file at the track's path.
                Mp3PreCheck, ///< Mp3QuickCheck turned the MP3 down before decoding (size, bitrate, or no frame where expected).
                NoDecoder,   ///< No audio format could open the file.
                ReadError    ///< The file opened, but reading or analyzing it threw.
            };

            /// @brief One track the analysis skipped, and why.
            struct SkippedTrack final
            {
                TrackInfo track;
                SkipReason reason{SkipReason::ReadError};
                std::string detail; ///< The exception text for ReadError; empty otherwise.
            };

            /// @brief What the reason means, in words for the user.
            std::string_view describe(SkipReason reason);

            /// @brief Whether a track skipped for this reason is one to offer to take out of a working set.
            /// @details Not for Mp3PreCheck: the pre-check turns down files the decoder can read (#77), so
            ///          those tracks are not known to be broken.
            bool isRemovable(SkipReason reason);

            /// @brief The tracks to offer for removal, in the order they were skipped.
            std::vector<TrackId> removableTrackIds(const std::vector<SkippedTrack> &skipped);

            /// @brief "12 skipped: 3 file missing, 9 turned down by the MP3 pre-check." - counts per reason, in enum order.
            std::string summarizeSkippedTracks(const std::vector<SkippedTrack> &skipped);

            /// @brief Act on the user's answer to the skipped-tracks report.
            /// @details Confirmed: removes removableTrackIds(skipped) from @p workingSetId, and from no other
            ///          set. Not confirmed, or nothing removable: changes nothing.
            /// @return The number of tracks asked to be removed (0 if nothing was done), or nullopt if the removal failed.
            std::optional<size_t> applySkippedTracksChoice(IWorkingSetManager &workingSets,
                WorkingSetId workingSetId,
                const std::vector<SkippedTrack> &skipped,
                bool confirmed);

            class BpmAnalysisTask final : public ILongRunningTask
            {
            public:
                explicit BpmAnalysisTask(std::vector<TrackInfo> trackInfos);
                ~BpmAnalysisTask() override = default;

                /// @brief Every track that was not analyzed, with the reason. Complete once the task has run.
                const std::vector<SkippedTrack> &getSkippedTracks() const
                {
                    return m_skippedTracks;
                }

            private:
                void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override;
                void runInternal(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel);

                std::vector<TrackInfo> m_trackInfos;
                std::vector<SkippedTrack> m_skippedTracks;
            };
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
