#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Utils/StringWriter.h>
#include <string_view>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief The Tracks columns trackInfoFromStatement reads, in the order it reads them.
         *
         * Every query that feeds that decoder selects this rather than `SELECT *`. The decoder walks the
         * row by position, so with a star the field each value lands in depends on the order the table
         * happens to declare its columns in - and that order is not the same in a database created from
         * scratch as in one that migrated up, because ALTER TABLE ADD COLUMN appends while
         * initialSqlStatements is free to declare a column anywhere. Naming the columns makes the order
         * the query's business rather than the schema's, and it is the reason nothing in the project
         * needs the two to agree.
         *
         * Qualified with the table name, because the same string is used in the joined queries too,
         * where a bare track_id would be ambiguous. A qualified name is valid in the unjoined ones as
         * well, so there is one list rather than two that could drift apart.
         *
         * album_id is deliberately not here: the decoder does not read it. Selecting a column nobody
         * decodes only invites the next person to line the list up against the table again.
         *
         * This and trackInfoFromStatement are one contract. Change them together.
         */
        inline constexpr std::string_view trackColumnsForDecoding{
            "Tracks.track_id, Tracks.folder_id, Tracks.filename, Tracks.last_modified_fs, Tracks.filesize_bytes, "
            "Tracks.date_added, Tracks.last_scanned, Tracks.title, Tracks.artist_name, Tracks.album_title, "
            "Tracks.album_artist_name, Tracks.track_number, Tracks.disc_number, Tracks.year, Tracks.duration, "
            "Tracks.samplerate, Tracks.channels, Tracks.bitrate, Tracks.codec_name, Tracks.bpm, Tracks.intro_end, "
            "Tracks.outro_start, Tracks.key_string, Tracks.beat_locations_json, Tracks.rating, "
            "Tracks.liked_status, Tracks.play_count, Tracks.last_played, Tracks.internal_content_hash, "
            "Tracks.user_notes, Tracks.is_missing, Tracks.status"};

        class SqliteStatementConstruction final
        {
        public:
            SqliteStatementConstruction(SqliteStatement &stmt);

            bool createCountStatement(const TrackQueryArgs &trackQueryArgs);
            bool createAggregateStatement(const TrackQueryArgs &trackQueryArgs);
            bool createSelectStatement(const TrackQueryArgs &trackQueryArgs);
            bool createInsertIntoSelectTrackIdsStatement(const TrackQueryArgs &trackQueryArgs, WorkingSetId wsId);

        private:
            SqliteStatement &m_stmt;
            int m_paramIndex; // Renamed for clarity from m_searchTermIndex
            std::vector<std::string> m_filterParams; // Stores filter values for parameterized binding

            void addWhereClause(StringWriter &writer, const TrackQueryArgs &trackQueryArgs);
            void addOrderByClause(StringWriter &writer, const TrackQueryArgs &trackQueryArgs);
            void addFilterCriteria(StringWriter &writer, const TrackQueryArgs &trackQueryArgs);
            bool finalizeStatement(StringWriter &writer, const TrackQueryArgs &trackQueryArgs, WorkingSetId wsId = 0);
            bool populateQueryScopeFoldersTable(const std::vector<FolderId> &folderIds);
        };
    } // namespace database
} // namespace jucyaudio