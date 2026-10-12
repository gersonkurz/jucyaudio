#pragma once

#include <Database/Includes/ITrackDatabase.h>
#include <Database/Includes/IWorkingSetManager.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class SqliteWorkingSetManager : public IWorkingSetManager
        {
        public:
            SqliteWorkingSetManager(database::SqliteDatabase &db)
                : m_db{db}
            {
            }
            ~SqliteWorkingSetManager() override = default;

        private:
            std::vector<WorkingSetInfo> getWorkingSets(
                const TrackQueryArgs &args) const override;
            bool createWorkingSetFromQuery(
                const TrackQueryArgs &args, std::string_view name,
                WorkingSetInfo &newWorkingSet) const override;
            bool createWorkingSetFromVirtualFolder(
                int64_t folderId, std::string_view name,
                WorkingSetInfo &newWorkingSet, bool recursive = true) const override;
            bool createWorkingSetFromTrackInfos(
                const std::vector<TrackInfo> &trackInfos, std::string_view name,
                WorkingSetInfo &newWorkingSet) const override;
            bool addToWorkingSet(WorkingSetId workingSetId,
                                 const std::vector<TrackInfo> &trackInfos) override;
            bool addVirtualFolderToWorkingSet(WorkingSetId workingSetId,
                                              int64_t folderId,
                                              bool recursive = true) override;
            bool renameWorkingSet(WorkingSetId workingSetId, std::string_view name) const override;
            bool removeTracksFromWorkingSet(
                WorkingSetId workingSetId,
                const std::vector<TrackId> &trackIds) override;
            bool removeTrackFromWorkingSet(WorkingSetId workingSetId,
                                      TrackId trackId) override;
            bool removeWorkingSet(WorkingSetId workingSetId) override;
            bool removeWorkingSets(const std::vector<WorkingSetId> &workingSetIds) override;
            bool applySortOrder(WorkingSetId workingSetId, const std::vector<SortOrderInfo> &sortOrder) override;
            bool shuffleTracks(WorkingSetId workingSetId, const std::vector<TrackId> &trackIds) override;
            int getNextMixNumber(WorkingSetId workingSetId) const override;
            bool incrementMixNumber(WorkingSetId workingSetId) override;
            bool setNextMixNumber(WorkingSetId workingSetId, int nextMixNumber) override;

        private:
            /// @brief The insert paths' common frame: repair, insert, number the new rows.
            bool insertTracksInOrder(WorkingSetId workingSetId, const std::function<bool()> &insert) const;

            database::SqliteDatabase &m_db;
        };

        /// @brief The order a new working set starts in, and the one a set without a saved sort is given: BPM ascending.
        std::vector<SortOrderInfo> defaultWorkingSetSortOrder();

        /// @brief Parse WorkingSets.sort_order. Empty or malformed JSON gives an empty vector.
        std::vector<SortOrderInfo> parseSortOrderJson(std::string_view json);

        /// @brief The WorkingSets.sort_order text for a sort; empty for an empty sort.
        std::string sortOrderToJson(const std::vector<SortOrderInfo> &sortOrder);

        /// @brief Give every track of a working set that has no position one, after the set's current maximum.
        /// @details The tracks are numbered among themselves in @p sortOrder, then by track_id. An empty
        ///          sort means defaultWorkingSetSortOrder(). Column names that are not columns of Tracks are
        ///          skipped with a warning: sort_order is read back from the database, so it is not trusted
        ///          as SQL. Opens no transaction - every caller is already inside one, and SqliteTransaction
        ///          does not nest.
        bool numberUnpositionedTracks(SqliteDatabase &db, WorkingSetId workingSetId, const std::vector<SortOrderInfo> &sortOrder);

    } // namespace database
} // namespace jucyaudio
