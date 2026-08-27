#pragma once

#include <Audio/Includes/ActiveExportSettings.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/MixInfo.h>
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
            virtual std::vector<MixInfo> getMixes(const TrackQueryArgs& args) const = 0;

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

            // @brief Refresh a mix's cached track count and total length.
            // @details The per-track removal calls above deliberately do not touch these columns, so a
            //          caller that changes the track list must refresh them or the mix list will show
            //          stale figures. createOrUpdateMix writes them as a side effect of rewriting
            //          everything; this is the cheap way to correct them on their own.
            // @param mixId The mix to update.
            // @param trackCount Number of tracks now in the mix.
            // @param totalLength Total playing length of the mix, including crossfades.
            // @return True if the row was updated.
            virtual bool updateMixSummary(MixId mixId, int64_t trackCount, Duration_t totalLength) const = 0;

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
