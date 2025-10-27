#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <optional>
#include <string>
#include <vector>
#include <Database/Includes/WorkingSetInfo.h>

namespace jucyaudio
{
    namespace database
    {
        class IWorkingSetManager
        {
        public:
            virtual ~IWorkingSetManager() = default;

            virtual std::vector<WorkingSetInfo> getWorkingSets(const TrackQueryArgs &args) const = 0;
            virtual bool createWorkingSetFromQuery(const TrackQueryArgs &args, std::string_view name, WorkingSetInfo &newWorkingSet) const = 0;
            virtual bool createWorkingSetFromVirtualFolder(
                int64_t folderId, std::string_view name, WorkingSetInfo &newWorkingSet, bool recursive = true) const = 0;
            virtual bool createWorkingSetFromTrackInfos(const std::vector<TrackInfo> &trackInfos, std::string_view name, WorkingSetInfo &newWorkingSet) const = 0;
            virtual bool renameWorkingSet(WorkingSetId workingSetId, std::string_view name) const = 0;
            virtual bool addToWorkingSet(WorkingSetId workingSetId, const std::vector<TrackInfo> &trackInfos) = 0;
            virtual bool removeTracksFromWorkingSet(WorkingSetId workingSetId, const std::vector<TrackId> &trackIds) = 0;
            virtual bool removeTrackFromWorkingSet(WorkingSetId workingSetId, TrackId trackId) = 0;
            virtual bool removeWorkingSet(WorkingSetId workingSetId) = 0;
            virtual bool removeWorkingSets(const std::vector<WorkingSetId> &workingSetIds) = 0;

            /// @brief Update the sort order for a working set
            /// @param workingSetId The working set to update
            /// @param sortOrder The new sort order configuration
            /// @return true if successful, false otherwise
            virtual bool updateSortOrder(WorkingSetId workingSetId, const std::vector<SortOrderInfo> &sortOrder) = 0;

            /// @brief Get the next mix number for a working set WITHOUT incrementing it
            /// @param workingSetId The working set to get the number for
            /// @return The next mix number (starts at 1), or 0 on error
            virtual int getNextMixNumber(WorkingSetId workingSetId) const = 0;

            /// @brief Increment the mix number for a working set (call when mix is actually created)
            /// @param workingSetId The working set to increment the number for
            /// @return true on success, false on error
            virtual bool incrementMixNumber(WorkingSetId workingSetId) = 0;

            /// @brief Set the next mix number for a working set (for editing/resetting)
            /// @param workingSetId The working set to set the number for
            /// @param nextMixNumber The new next mix number value
            /// @return true on success, false on error
            virtual bool setNextMixNumber(WorkingSetId workingSetId, int nextMixNumber) = 0;
        };

    } // namespace database
} // namespace jucyaudio
