#pragma once

#include <Database/Includes/ITrackDatabase.h>
#include <Database/Includes/IWorkingSetManager.h>
#include <Database/Sqlite/SqliteDatabase.h>

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
            bool createWorkingSetFromTrackIds(
                const std::vector<TrackId> &trackIds, std::string_view name,
                WorkingSetInfo &newWorkingSet) const override;
            bool addToWorkingSet(WorkingSetId workingSetId,
                                 const std::vector<TrackId> &trackIds) override;
            bool addToWorkingSet(WorkingSetId workingSetId,
                                 TrackId trackId) override;
            bool removeFromWorkingSet(
                WorkingSetId workingSetId,
                const std::vector<TrackId> &trackIds) override;
            bool removeFromWorkingSet(WorkingSetId workingSetId,
                                      TrackId trackId) override;
            bool removeWorkingSet(WorkingSetId workingSetId) override;

        private:
            database::SqliteDatabase &m_db;
        };

    } // namespace database
} // namespace jucyaudio
