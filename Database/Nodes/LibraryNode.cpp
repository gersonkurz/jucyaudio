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
        enum class Column : ColumnIndex_t
        {
            Title = 0,
            Artist,
            Album,
            Duration,
            BPM,
            Intro,
            Outro,
            TrackId,
            Filepath,
            Filename,
            LastModified
        };
    } // namespace

    namespace database
    {

        const DataActions LibraryNodeActions{DataAction::CreateWorkingSet, DataAction::ShowDetails, DataAction::CreateMix};
        const DataActions LibraryRowActions{DataAction::Play,        DataAction::CreateWorkingSet, DataAction::CreateMix,
                                            DataAction::ShowDetails, DataAction::EditMetadata,     DataAction::Delete};
        const std::vector<DataColumn> LibraryColumns = {
            DataColumn{(ColumnIndex_t)Column::Title, "title", "Title", 200, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{(ColumnIndex_t)Column::Artist, "artist_name", "Artist", 150, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{(ColumnIndex_t)Column::Album, "album_title", "Album", 150, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{(ColumnIndex_t)Column::Duration, "duration", "Duration", 100, ColumnAlignment::Right, ColumnDataTypeHint::Duration},
            DataColumn{(ColumnIndex_t)Column::BPM, "bpm", "BPM at start", 80, ColumnAlignment::Left, ColumnDataTypeHint::Integer},
            DataColumn{(ColumnIndex_t)Column::Intro, "intro_end", "Intro", 80, ColumnAlignment::Left, ColumnDataTypeHint::Integer},
            DataColumn{(ColumnIndex_t)Column::Outro, "outro_start", "Outro", 80, ColumnAlignment::Left, ColumnDataTypeHint::Integer},
            DataColumn{(ColumnIndex_t)Column::TrackId, "track_id", "Track ID", 80, ColumnAlignment::Left, ColumnDataTypeHint::Integer},
            DataColumn{(ColumnIndex_t)Column::Filepath, "filepath", "Path", 80, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{(ColumnIndex_t)Column::Filename, "filepath", "Name", 80, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{(ColumnIndex_t)Column::LastModified, "last_modified_fs", "Last Modified", 80, ColumnAlignment::Left, ColumnDataTypeHint::Integer},
        };

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

            switch (static_cast<Column>(index))
            {
            case Column::Title:
                return track->title;
            case Column::Artist:
                return track->artist_name;
            case Column::Album:
                return track->album_title;
            case Column::Duration:
                return durationToString(track->duration);
            case Column::BPM:
                if (track->bpm.has_value())
                {
                    return std::format("{:.2f}", track->bpm.value() / 100.0);
                }
                return "-";
            case Column::Intro:
                return track->intro_end.has_value() ? durationToString(*track->intro_end) : "-";
            case Column::Outro:
                if (track->outro_start.has_value())
                {
                    using ms = std::chrono::milliseconds;
                    ms remaining = track->duration - *track->outro_start;
                    return durationToString(remaining);
                }
                return "-";
            case Column::TrackId:
                return std::to_string(track->trackId);
            case Column::Filepath:
                return track->filepath.string();
            case Column::Filename:
                return track->filepath.filename().string();
            case Column::LastModified:
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

        void LibraryNode::refreshCache(bool flushCache) const
        {
            // if the cache is invalid, or the rowIndex is out of bounds, we need to retrieve the rows
            const auto refreshCache = !m_bCacheInitialized || flushCache;
            if (refreshCache)
            {
                m_tracks = m_library.getTracks(m_queryArgs);
                m_bCacheInitialized = true;
            }
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
