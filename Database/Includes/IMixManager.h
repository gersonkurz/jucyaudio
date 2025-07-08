#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackQueryArgs.h>
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

            // @brief Get the mix-track information associated with a specific mix ID.
            // @param mixId The ID of the mix to retrieve tracks for.
            // @return A vector of MixTrack objects representing the tracks in the specified mix.
            virtual std::vector<MixTrack> getMixTracks(MixId mixId) const = 0;

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
                                                const Duration_t defaultCrossfadeDuration = Duration_t{5000}
                                              ) const = 0;

            // @brief Remove a specific track from a mix.
            // @param mixId The ID of the mix from which to remove the track.
            // @param trackId The ID of the track to remove from the mix.
            // @return True if the track was successfully removed from the mix, false otherwise.            
            virtual bool removeTrackFromMix(MixId mixId, TrackId trackId) const = 0;
        };

    } // namespace database
} // namespace jucyaudio
