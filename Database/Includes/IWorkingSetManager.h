#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <optional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class IWorkingSetManager
        {
        public:
            virtual ~IWorkingSetManager() = default;

            virtual std::vector<WorkingSetInfo> getWorkingSets(
                const TrackQueryArgs &args) const = 0;
            virtual bool createWorkingSetFromQuery(
                const TrackQueryArgs &args, std::string_view name,
                WorkingSetInfo &newWorkingSet) const = 0;
            virtual bool createWorkingSetFromVirtualFolder(
                int64_t folderId, std::string_view name,
                WorkingSetInfo &newWorkingSet, bool recursive = true) const = 0;
            virtual bool createWorkingSetFromTrackIds(
                const std::vector<TrackId> &trackIds, std::string_view name,
                WorkingSetInfo &newWorkingSet) const = 0;
            virtual bool renameWorkingSet(WorkingSetId workingSetId, std::string_view name) const = 0;
            virtual bool addToWorkingSet(
                WorkingSetId workingSetId,
                const std::vector<TrackId> &trackIds) = 0;
            virtual bool addToWorkingSet(WorkingSetId workingSetId,
                                         TrackId trackId) = 0;
            virtual bool removeFromWorkingSet(
                WorkingSetId workingSetId,
                const std::vector<TrackId> &trackIds) = 0;
            virtual bool removeFromWorkingSet(WorkingSetId workingSetId,
                                              TrackId trackId) = 0;
            virtual bool removeWorkingSet(WorkingSetId workingSetId) = 0;
            virtual bool removeWorkingSets(const std::vector<WorkingSetId>& workingSetIds) = 0;
            
            
            /// @brief Update the sort order for a working set
            /// @param workingSetId The working set to update
            /// @param sortOrder The new sort order configuration
            /// @return true if successful, false otherwise
            virtual bool updateSortOrder(WorkingSetId workingSetId, 
                                        const std::vector<SortOrderInfo>& sortOrder) = 0;
        };

    } // namespace database
} // namespace jucyaudio
