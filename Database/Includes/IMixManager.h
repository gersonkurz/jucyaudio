#pragma once

#include <Audio/Includes/ActiveExportSettings.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/MixSummary.h>
#include <Database/Includes/MixRecoveryEntry.h>
#include <Database/Includes/ExportFolderInfo.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/UndoManager.h>
#include <optional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class IMixManager
        {
        public:
            virtual ~IMixManager() = default;

            // @brief Get all mixes in the database. We assume that the total # of mixes is not too large to fit in memory.
            // @return A vector of MixInfo objects representing all mixes.
            //
            // Despite the name this is a query for a list the user is looking at: it returns only mixes
            // that are still editable (export_folder IS NULL), and may additionally hide offline ones
            // depending on a UI setting. For every mix regardless of state, use getAllMixes().
            virtual std::vector<MixInfo> getMixes(const TrackQueryArgs& args) const = 0;

            /**
             * @brief Every mix in the database, in id order, filtered by nothing.
             *
             * getMixes() and getMixesByLocation(nullopt) both return only mixes with no export folder,
             * which is what a list of editable mixes wants and the opposite of what an enumeration
             * wants - in a library where every mix has been exported, both return nothing at all. This
             * exists for the cases that must genuinely see all of them.
             *
             * Status-bearing, because an enumeration that cannot say it failed is worse than none: a
             * caller acting on every mix would silently act on some of them instead, and report success.
             *
             * @param mixes Receives every mix, ordered by mix_id. Assigned only on success, so a failed
             *        read leaves whatever the caller had rather than looking like an empty library.
             * @return Ok if the whole enumeration completed.
             */
            virtual DbResult getAllMixes(std::vector<MixInfo> &mixes) const = 0;

            virtual MixInfo getMix(MixId mixId) const
            {
                // Default implementation returns an empty MixInfo if not overridden
                const auto mixes{getMixes({.mixId = mixId})};
                if (mixes.empty())
                {
                    return MixInfo{}; // Return an empty MixInfo if no mix found
                }
                else
                {
                    return mixes.front(); // Return the first match
                }
            }

            ExtendedMixInfo getExtendedMixInfo(MixId mixId) const
            {
                ExtendedMixInfo extMixInfo;
                extMixInfo.mixInfo = getMix(mixId);
                if (extMixInfo.mixInfo.mixId != 0) // Valid mix found
                {
                    extMixInfo.tracks = getMixTracks(mixId);
                }
                return extMixInfo;
            }

            void recordMixChange(MixId mixId) const
            {
                theUndoManager.recordMixChange(getExtendedMixInfo(mixId));
            }

            // @brief Get the mix-track information associated with a specific mix ID.
            // @param mixId The ID of the mix to retrieve tracks for.
            // @return A vector of MixTrack objects representing the tracks in the specified mix.
            virtual std::vector<MixTrack> getMixTracks(MixId mixId) const = 0;

            /**
             * @brief The rows of a mix, with a reason when they cannot all be read.
             *
             * Prefer this to getMixTracks anywhere the result is going to be saved back. That one has
             * nowhere to report a failure, so it returns an empty vector - and a caller that cannot
             * tell an empty mix from an unreadable one will eventually write the wrong one down.
             *
             * Nothing is written to @p tracksOut unless every row was read and decoded.
             */
            virtual DbResult readMixTracks(MixId mixId, std::vector<MixTrack> &tracksOut) const = 0;

            /**
             * @brief What this mix contained when it was last captured, in order.
             *
             * Reads the recovery record rather than the live mix. The two are expected to differ: the
             * record is a snapshot, and its whole value is that it still says what the mix held after
             * the live rows have gone.
             *
             * Returns a status rather than just the vector, because "this mix was never captured" and
             * "the read failed" are both empty and mean opposite things. The first is ordinary; the
             * second means the caller cannot conclude anything - and a caller that treats a failed read
             * as "no snapshot exists" is exactly how a good snapshot gets overwritten or reported lost.
             *
             * @param mixId The mix to look up.
             * @param entries Receives the entries, ordered by position. Assigned only on success, so a
             *        failure leaves whatever the caller had untouched. Empty with an Ok status means the
             *        mix has never been captured, which is not an error.
             * @return Ok on a completed read, whether or not it found anything.
             */
            virtual DbResult getRecoveryData(MixId mixId, std::vector<MixRecoveryEntry> &entries) const = 0;

            /**
             * @brief Record what this mix currently contains, replacing any previous record of it.
             *
             * Refuses to record a mix that is not demonstrably intact. The row count must match
             * `Mixes.track_count` and the positions must run contiguously from zero, and if either fails
             * the mix is left exactly as it was - including whatever was recorded for it previously.
             *
             * That refusal is the point of the whole function. A mix quietly loses rows when the tracks
             * behind them are deleted, and the loss is invisible until someone looks. Replacing a good
             * forty-track record with the thirty-three that survive would destroy the only evidence of
             * what went missing, at exactly the moment it became worth having.
             *
             * @param mixId The mix to record.
             * @param result What happened: recorded, with the rows that were written; or skipped, with
             *        the reason. Assigned only when this returns Ok.
             * @param renderedTracks Optional. The track list a renderer actually used, for callers that
             *        produced something from this mix and need the record to match it. The live rows are
             *        compared against it and the mix is refused if they differ.
             *
             *        Rendering a two-hour mix takes minutes, and the renderer works from a copy it took
             *        before starting. Another instance can edit the mix while that runs, and the
             *        transaction here only covers its own read and write - so without this the mp3 and
             *        the record of it could describe different mixes.
             *
             *        The comparison is over the parsed, audio-relevant fields, not the stored text: a
             *        MixTrack does not retain the JSON it came from, and two texts differing only in key
             *        order or whitespace are the same mix. The stored text is still written verbatim.
             * @return Ok if the attempt completed, whether it recorded or refused. A failure means
             *         something went wrong and nothing can be concluded about the mix.
             */
            virtual DbResult captureRecoveryData(MixId mixId,
                MixRecoveryCapture &result,
                const std::vector<MixTrack> *renderedTracks = nullptr) const = 0;

            // @brief Get the track count for a specific mix (efficient COUNT query).
            // @param mixId The ID of the mix.
            // @return The number of tracks in the mix.
            virtual int getTrackCountForMix(MixId mixId) const = 0;

            // @brief Create or update a mix in the database.
            // @param mixInfo Input: mixInfo.mixId should be set by the caller. If it is -1 or uninitialized, a new mix will be created.
            //                Output: mixInfo.mixId will be populated for a new mix. mixInfo.timestamp will be set.
            // @param tracks The detailed MixTrack structures to be associated with the mix.
            // @return True if the mix was successfully created or updated, false otherwise.
            virtual bool createOrUpdateMix(MixInfo &mixInfo, std::vector<MixTrack> &tracks) const = 0;

            // @brief Remove a mix from the database by its ID.
            // @param mixId The ID of the mix to remove.
            // @return True if the mix was successfully removed, false otherwise.
            virtual bool removeMix(MixId mixId) const = 0;

            // @brief Remove multiple mixes from the database.
            // @param mixIds A vector of MixIds to remove from the database.
            // @return True if all specified mixes were successfully removed, false otherwise.
            virtual bool removeMixes(const std::vector<MixId> &mixIds) const = 0;
            
            virtual bool renameMix(MixId mixId, std::string_view name) const = 0;

            // @brief Creates a new mix based on an ordered list of track IDs using an automatic mixing logic
            //        (e.g., sequential with predefined crossfades) and saves it to the database.
            // @param trackInfos The ordered list of track infos. Why not TrackIds? Because the UI has all this information
            //        already present - the mix details are shown on the screen with all the track information.
            //        Database pages might be smaller, but the underlying cache mechanism will ensure that the data is available.
            //        And we estimate that the number of tracks in a mix will not be too large (<1000 for sure)
            // @param mixInfo Input: mixInfo.name should be set by the caller. mixInfo.mixId can be -1 or uninitialized for a new mix.
            //                Output: mixInfo.mixId will be populated for a new mix. mixInfo.timestamp will be set.
            // @param resultingTracks Output: The detailed MixTrack structures as calculated and saved to the database.
            // @param defaultCrossfadeDuration The duration for the automatic crossfade between tracks.
            // @return True if the auto-mix was successfully created and saved, false otherwise.
            virtual bool createAndSaveAutoMix(const std::vector<TrackInfo> &trackInfos,
                                              /*in/out*/ MixInfo &mixInfo,
                                              /*out*/ std::vector<MixTrack> &resultingTracks, 
                                                WorkingSetId source_ws_id,
                                                const Duration_t defaultCrossfadeDuration = Duration_t{5000}
                                              ) const = 0;

            // @brief Remove a specific track from a mix.
            // @param mixId The ID of the mix from which to remove the track.
            // @param trackId The ID of the track to remove from the mix.
            // @return True if the track was successfully removed from the mix, false otherwise.            
            virtual bool removeTrackFromMix(MixId mixId, TrackId trackId) const = 0;

            // @brief Remove the concrete track row at a specific order from a mix.
            // @param mixId The ID of the mix from which to remove the track.
            // @param orderInMix The concrete 0-based row position to remove.
            // @return True if the track row was successfully removed from the mix, false otherwise.
            virtual bool removeTrackFromMixAtOrder(MixId mixId, int orderInMix) const = 0;

            // @brief Remove multiple tracks from a mix.
            // @param mixId The ID of the mix from which to remove the tracks.
            // @param trackIds A vector of TrackIds to remove from the mix.
            // @return True if all specified tracks were successfully removed from the mix, false otherwise.
            virtual bool removeTracksFromMix(MixId mixId, const std::vector<TrackId> &trackIds) const = 0;

            /**
             * @brief Recomputes one mix's stored track count and length from its rows, if they differ.
             *
             * Both columns together, never one alone: they describe the same set of rows, and a count
             * belonging to a different moment from the length is worse than neither.
             *
             * There is deliberately no way to *set* those figures. They used to be whatever a caller
             * passed in, six of the eight callers passed the value they were already holding, and a
             * five-hour mix ended up stored as sixty-six hours. Every operation that changes a mix now
             * derives them itself, so the only thing left to offer is this: recompute, and say whether
             * anything was wrong.
             *
             * The whole decision - read the rows, read the durations, compare, write - happens inside a
             * single immediate transaction. It has to: another instance of the application may be
             * editing the same mix, and a comparison made outside the transaction that writes could
             * commit a length describing a mix that no longer exists in that form. The before and after
             * figures are read inside it too, so a report quoting both describes one moment.
             *
             * A mix whose stored figures already agree is not written at all. That keeps its row
             * untouched, and makes running this twice a way to verify the first run rather than repeat
             * it. An empty mix is likewise left alone: it is already damaged, and its stored length is
             * the last description of what it held.
             *
             * @param mixId The mix to check.
             * @param result What was found, and what if anything was written.
             * @return Success if the mix was checked, whether or not it needed changing.
             */
            virtual DbResult recomputeMixDuration(MixId mixId, MixDurationCheck &result) const = 0;

            // @brief Reorder a concrete track row within a mix by moving it to a new position.
            // @param mixId The ID of the mix containing the track.
            // @param currentOrderInMix The current 0-based row position to move.
            // @param newOrderInMix The new 0-based order position (will shift other tracks accordingly).
            // @return True if the track was successfully reordered, false otherwise.
            virtual bool reorderTrackInMix(MixId mixId, int currentOrderInMix, int newOrderInMix) const = 0;

            // @brief Finalize a mix, prune the source working set, and prepare for export.
            // @param mixId The ID of the mix to finalize.
            // @return True if the operation was successful, false otherwise.
            virtual bool finalizeMix(MixId mixId) const = 0;
            
            // @brief Clear the working_set_id for a mix (set it to NULL).
            // @param mixId The ID of the mix to update.
            // @return True if the operation was successful, false otherwise.
            virtual bool clearMixWorkingSetId(MixId mixId) const = 0;

            // @brief Update a single MixTrack within a mix.
            // @param mixId The ID of the mix to which the track belongs.
            // @param updatedTrack The MixTrack object containing the updated data.
            // @return True if the track was successfully updated, false otherwise.
            virtual bool updateMixTrack(MixId mixId, const MixTrack& updatedTrack) const = 0;
            
            // @brief Set the status of a mix (e.g., "New", "Modified", "Exported", "Locked")
            // @param mixId The ID of the mix to update.
            // @param status The new status string.
            // @return True if the status was successfully updated, false otherwise.
            virtual bool setMixStatus(MixId mixId, std::string_view status) const = 0;

            // Export Organization System methods

            // @brief Mark a mix as exported to a specific folder
            // @param mixId The ID of the mix to mark as exported
            // @param exportFolder The export folder name (e.g., "Noise", "Techno")
            // @return True if the mix was successfully marked as exported, false otherwise
            virtual bool setMixExported(MixId mixId, std::string_view exportFolder) const = 0;

            // @brief Move a mix back to the Mixes area for editing
            // @param mixId The ID of the mix to move back
            // @return True if the mix was successfully moved back, false otherwise
            virtual bool moveBackToMixes(MixId mixId) const = 0;

            // @brief Check if a mix is exported (and thus read-only)
            // @param mixId The ID of the mix to check
            // @return True if the mix is exported, false otherwise
            virtual bool isExported(MixId mixId) const = 0;

            // @brief Get all export folders
            // @return A vector of export folder names
            virtual std::vector<ExportFolderInfo> getExportFolders() const = 0;

            // @brief Create a new export folder
            // @param name The name of the export folder
            // @param description Optional description for the folder
            // @return True if the folder was successfully created, false otherwise
            virtual bool createExportFolder(std::string_view name, std::string_view description = "") const = 0;

            // @brief Get mixes by location (export folder or editable)
            // @param exportFolder If provided, get mixes in this export folder. If empty, get editable mixes
            // @return A vector of MixInfo objects matching the criteria
            virtual std::vector<MixInfo> getMixesByLocation(std::optional<std::string_view> exportFolder = std::nullopt) const = 0;

            // --- Scheduled Export methods ---

            // @brief Save export settings on a mix for deferred batch export.
            // @param mixId The mix to schedule.
            // @param settings The export settings to persist.
            // @return True on success.
            virtual bool setPendingExportSettings(MixId mixId, const audio::ActiveExportSettings& settings) const = 0;

            // @brief Finalize a mix for deferred export and persist its export settings atomically.
            // @param mixId The mix to schedule.
            // @param settings The export settings to persist.
            // @return True on success.
            virtual bool scheduleMixForExport(MixId mixId, const audio::ActiveExportSettings& settings) const = 0;

            // @brief Clear pending export settings for a mix (unschedule).
            // @param mixId The mix to unschedule.
            // @return True on success.
            virtual bool clearPendingExportSettings(MixId mixId) const = 0;

            // @brief Retrieve saved export settings for a mix, if any.
            // @param mixId The mix to query.
            // @return The settings if scheduled, nullopt otherwise.
            virtual std::optional<audio::ActiveExportSettings> getPendingExportSettings(MixId mixId) const = 0;

            // @brief A mix paired with its pending export settings.
            struct ScheduledExport
            {
                MixInfo mixInfo;
                audio::ActiveExportSettings settings;
            };

            // @brief Get all mixes that have pending export settings.
            // @return A vector of ScheduledExport entries.
            virtual std::vector<ScheduledExport> getMixesScheduledForExport() const = 0;
        };

    } // namespace database
} // namespace jucyaudio
