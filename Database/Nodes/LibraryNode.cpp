#include <Database/Nodes/LibraryNode.h>
#include <Database/Nodes/RootNode.h>
#include <Utils/AssortedUtils.h>
#include <cassert>
#include <spdlog/spdlog.h>
namespace jucyaudio
{
    // anonymous namespace: defines are only valid in this translation unit
    namespace
    {
        const ColumnIndex_t Column_Title{0};
        const ColumnIndex_t Column_Artist{1};
        const ColumnIndex_t Column_Album{2};
        const ColumnIndex_t Column_Duration{3};
        const ColumnIndex_t Column_BPM{4};
        const ColumnIndex_t Column_TrackId{5};
        const ColumnIndex_t Column_Filepath{6};
        const ColumnIndex_t Column_Filename{7};
        const ColumnIndex_t Column_LastModified{8};
    } // namespace
    
    namespace database
    {

        const DataActions LibraryNodeActions{DataAction::CreateWorkingSet, DataAction::ShowDetails, DataAction::CreateMix};
        const DataActions LibraryRowActions{DataAction::Play,        DataAction::CreateWorkingSet, DataAction::CreateMix,
                                            DataAction::ShowDetails, DataAction::EditMetadata,     DataAction::Delete};
        const std::vector<DataColumn> LibraryColumns = {
            DataColumn{Column_Title, "title", "Title", 200, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{Column_Artist, "artist_name", "Artist", 150, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{Column_Album, "album_title", "Album", 150, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{Column_Duration, "duration", "Duration", 100, ColumnAlignment::Right, ColumnDataTypeHint::Duration},
            DataColumn{Column_BPM, "bpm", "BPM", 80, ColumnAlignment::Left, ColumnDataTypeHint::Double},
            DataColumn{Column_TrackId, "track_id", "Track ID", 80, ColumnAlignment::Left, ColumnDataTypeHint::Integer},
            DataColumn{Column_Filepath, "filepath", "Path", 80, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{Column_Filename, "filepath", "Name", 80, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{Column_LastModified, "last_modified_fs", "Last Modified", 80, ColumnAlignment::Left, ColumnDataTypeHint::Integer},
        };
        /*

            filesize_bytes INTEGER,
            date_added INTEGER,
            last_scanned INTEGER,
            title TEXT,
            artist_name TEXT,
            album_title TEXT,
            album_artist_name TEXT,
            track_number INTEGER,
            disc_number INTEGER,
            year INTEGER,
            duration INTEGER,
            samplerate INTEGER,
            channels INTEGER,
            bitrate INTEGER,
            codec_name TEXT,
            bpm INTEGER,
            key_string TEXT,
            beat_locations_json TEXT,
            rating INTEGER DEFAULT 0, liked_status INTEGER DEFAULT 0, play_count INTEGER DEFAULT 0, last_played INTEGER,
            internal_content_ha
        */
        const DataActions &LibraryNode::getNodeActions() const
        {
            return LibraryNodeActions;
        }

        const DataActions &LibraryNode::getRowActions([[maybe_unused]] RowIndex_t rowIndex) const
        {
            return LibraryRowActions;
        }

        LibraryNode::~LibraryNode()
        {
            // No dynamic memory to clean up, but we can log if needed
            // spdlog::debug("LibraryNode destructor called, cleaning up resources if any.");
            m_tracks.clear();
        }

        LibraryNode::LibraryNode(INavigationNode *root, TrackLibrary &library, const std::string &name)
            : BaseNode{root, name.empty() ? getLibraryRootNodeName() : name},
              m_bCacheInitialized{false},
              m_library{library}
        {
        }

        bool LibraryNode::getChildren([[maybe_unused]] std::vector<INavigationNode *> &outChildren)
        {
            assert(false && "you should first check with hasChildren before you call this...");
            return false;
        }

        bool LibraryNode::hasChildren() const
        {
            return false;
        }

        const std::vector<DataColumn> &LibraryNode::getColumns() const
        {
            return LibraryColumns;
        }

        bool LibraryNode::getNumberOfRows(int64_t &outCount) const
        {
            outCount = m_library.getTotalTrackCount(m_queryArgs);
            return true;
        }

        bool LibraryNode::setSortOrder(const std::vector<SortOrderInfo> &sortOrders)
        {
            m_queryArgs.sortBy = sortOrders;
            m_bCacheInitialized = false;
            return true;
        }

        std::vector<SortOrderInfo> LibraryNode::getCurrentSortOrder() const
        {
            return m_queryArgs.sortBy;
        }

        bool LibraryNode::setSearchTerms(const std::vector<std::string> &searchTerms)
        {
            m_queryArgs.searchTerms = searchTerms;
            m_bCacheInitialized = false;
            return true;
        }

        std::vector<std::string> LibraryNode::getCurrentSearchTerms() const
        {
            return m_queryArgs.searchTerms;
        }

        std::string LibraryNode::getCellText(RowIndex_t rowIndex, ColumnIndex_t index) const
        {
            const auto track{getTrackInfoForRow(rowIndex)};
            if (track == nullptr)
            {
                return {};
            }

            switch (index)
            {
            case Column_Title:
                return track->title;
            case Column_Artist:
                return track->artist_name;
            case Column_Album: 
                return track->album_title;
            case Column_Duration:
                return durationToString(track->duration);
            case Column_BPM:
                return std::to_string(track->bpm);
            case Column_TrackId:
                return std::to_string(track->trackId);
            case Column_Filepath:
                return track->filepath.string();
            case Column_Filename:
                return track->filepath.filename().string();
            case Column_LastModified:
                return jucyaudio::timestampToString(track->last_modified_fs);

            default:
                spdlog::warn("Invalid column index {} for LibraryRow", index);
                return "";
            }
        }

        const TrackInfo *LibraryNode::getTrackInfoForRow(RowIndex_t rowIndex) const
        {
            if (rowIndex < 0)
            {
                spdlog::warn("Row index {} is negative, returning nullptr", rowIndex);
                return nullptr;
            }
            // if the cache is invalid, or the rowIndex is out of bounds, we need to retrieve the rows
            const auto refreshCache = !m_bCacheInitialized || (rowIndex < m_queryArgs.offset) || (rowIndex >= m_queryArgs.offset + QUERY_PAGE_SIZE);
            if (refreshCache)
            {
                volatile auto temp = rowIndex / QUERY_PAGE_SIZE;
                m_queryArgs.offset = temp * QUERY_PAGE_SIZE;
                // we should maybe switch the limit from a variable to a constant: this is an implementation detail
                // callers of our library shouldn't need to know about this
                m_tracks = m_library.getTracks(m_queryArgs);
                m_bCacheInitialized = true;
            }
            const auto targetIndex = rowIndex - m_queryArgs.offset;
            if (targetIndex >= m_tracks.size())
            {
                spdlog::warn("Target index {} is out of bounds for the current track cache size {}", targetIndex, m_tracks.size());
                return nullptr;
            }
            return &m_tracks[targetIndex];
        }

        const TrackQueryArgs *LibraryNode::getQueryArgs() const
        {
            return &m_queryArgs;
        }

        bool LibraryNode::prepareToShowData()
        {
            return true;
        }

        void LibraryNode::dataNoLongerShowing()
        {
        }

    } // namespace database
} // namespace jucyaudio
