#include <Database/Nodes/AlbumsNode.h>
#include <Database/Nodes/RootNode.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <cassert>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <juce_core/juce_core.h>


namespace jucyaudio
{
    // anonymous namespace: defines are only valid in this translation unit
    namespace
    {
        enum class Column : ColumnIndex_t
        {
            Artist = 0,
            Title,
            Year,
            TrackCount,
            Duration,
            Folder,
            AlbumId
        };
    } // namespace

    namespace database
    {
        const DataActions AlbumsNodeActions{
            DataAction::CreateWorkingSet, DataAction::CreateMix, DataAction::Delete};

        const DataActions AlbumsRowActions{
            DataAction::ShowInFolder,
            DataAction::CreateWorkingSet,
            DataAction::CreateMix,
            DataAction::ShowDetails};

        const std::vector<DataColumn> AlbumsColumns = {
            DataColumn{(ColumnIndex_t)Column::Artist, "album_artist", "Artist", 200, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{(ColumnIndex_t)Column::Title, "title", "Album", 250, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{(ColumnIndex_t)Column::Year, "year", "Year", 60, ColumnAlignment::Left, ColumnDataTypeHint::Integer},
            DataColumn{(ColumnIndex_t)Column::TrackCount, "track_count", "Tracks", 60, ColumnAlignment::Right, ColumnDataTypeHint::Integer},
            DataColumn{(ColumnIndex_t)Column::Duration, "duration", "Duration", 100, ColumnAlignment::Right, ColumnDataTypeHint::Duration},
            DataColumn{(ColumnIndex_t)Column::Folder, "folder", "Folder", 300, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{(ColumnIndex_t)Column::AlbumId, "album_id", "Album ID", 80, ColumnAlignment::Left, ColumnDataTypeHint::Integer}
        };

        const DataActions &AlbumsNode::getNodeActions() const
        {
            return AlbumsNodeActions;
        }

        const DataActions &AlbumsNode::getRowActions([[maybe_unused]] RowIndex_t rowIndex) const
        {
            return AlbumsRowActions;
        }

        AlbumsNode::~AlbumsNode()
        {
            m_albums.clear();
        }

        AlbumsNode::AlbumsNode(INavigationNode *root,
            const std::string &name,
            std::string_view typeNameForSingleObject,
            std::string_view typeNameForMultipleObjects)
            : BaseNode{root, name.empty() ? "Albums" : name, typeNameForSingleObject, typeNameForMultipleObjects},
              m_bCacheInitialized{false}
        {
        }

        bool AlbumsNode::expand([[maybe_unused]] std::vector<INavigationNode *> &outChildren)
        {
            assert(false && "AlbumsNode cannot be expanded");
            return false;
        }

        bool AlbumsNode::canExpand()
        {
            return false;
        }

        const std::vector<DataColumn> &AlbumsNode::getColumns() const
        {
            return AlbumsColumns;
        }

        bool AlbumsNode::getNumberOfRows(int64_t &outCount) const
        {
            if (m_cachedRowCount == -1 || !m_bCacheInitialized)
            {
                const_cast<AlbumsNode*>(this)->prepareToShowData();
            }
            outCount = m_cachedRowCount;
            return true;
        }

        bool AlbumsNode::prepareToShowData()
        {
            try
            {
                const auto &trackLibrary = theTrackLibrary;
                const auto &albumManager = trackLibrary.getAlbumManager();
                const auto &folderDb = trackLibrary.getFolderDatabase();
                
                // Get all albums from the database
                m_albums = albumManager.getAllAlbums();
                
                // Apply search filter if we have search terms
                if (!m_searchTerms.empty())
                {
                    std::vector<AlbumInfo> filteredAlbums;
                    for (const auto &album : m_albums)
                    {
                        bool matches = false;
                        for (const auto &term : m_searchTerms)
                        {
                            const auto lowerTerm = juce::String(term).toLowerCase().toStdString();
                            const auto lowerArtist = juce::String(album.albumArtist).toLowerCase().toStdString();
                            const auto lowerTitle = juce::String(album.title).toLowerCase().toStdString();
                            if (lowerArtist.find(lowerTerm) != std::string::npos ||
                                lowerTitle.find(lowerTerm) != std::string::npos)
                            {
                                matches = true;
                                break;
                            }
                        }
                        if (matches)
                        {
                            filteredAlbums.push_back(album);
                        }
                    }
                    m_albums = std::move(filteredAlbums);
                }
                
                // Calculate track counts and durations for each album
                for (auto &album : m_albums)
                {
                    const auto trackIds = albumManager.getAlbumTracks(album.albumId);
                    album.trackCount = static_cast<int>(trackIds.size());
                    
                    // Calculate total duration
                    Duration_t totalDuration{0};
                    for (const auto trackId : trackIds)
                    {
                        if (const auto trackInfo = trackLibrary.getTrackDatabase()->getTrackById(trackId))
                        {
                            totalDuration += trackInfo->duration;
                        }
                    }
                    album.totalDuration = totalDuration;
                }
                
                // Apply sorting if specified
                if (!m_sortOrders.empty())
                {
                    const auto &sortOrder = m_sortOrders.front();
                    // Find the column index from the column name
                    ColumnIndex_t columnIdx = 0;
                    for (const auto &col : AlbumsColumns)
                    {
                        if (col.name == sortOrder.columnName)
                        {
                            columnIdx = col.index;
                            break;
                        }
                    }
                    const auto column = static_cast<Column>(columnIdx);
                    const bool ascending = !sortOrder.descending;
                    
                    std::sort(m_albums.begin(), m_albums.end(), 
                        [column, ascending](const AlbumInfo &a, const AlbumInfo &b) {
                            int comparison = 0;
                            switch (column)
                            {
                                case Column::Artist:
                                    comparison = a.albumArtist.compare(b.albumArtist);
                                    break;
                                case Column::Title:
                                    comparison = a.title.compare(b.title);
                                    break;
                                case Column::Year:
                                    comparison = (a.year.value_or(0) - b.year.value_or(0));
                                    break;
                                case Column::TrackCount:
                                    comparison = a.trackCount - b.trackCount;
                                    break;
                                case Column::Duration:
                                    comparison = static_cast<int>(a.totalDuration.count() - b.totalDuration.count());
                                    break;
                                case Column::AlbumId:
                                    comparison = static_cast<int>(a.albumId - b.albumId);
                                    break;
                                default:
                                    break;
                            }
                            return ascending ? (comparison < 0) : (comparison > 0);
                        });
                }
                
                m_cachedRowCount = static_cast<int64_t>(m_albums.size());
                m_bCacheInitialized = true;
                
                spdlog::debug("AlbumsNode loaded {} albums", m_cachedRowCount);
                return true;
            }
            catch (const std::exception &e)
            {
                spdlog::error("Failed to load albums: {}", e.what());
                m_cachedRowCount = 0;
                return false;
            }
        }

        std::string AlbumsNode::getCellText(RowIndex_t rowIndex, ColumnIndex_t columnIndex) const
        {
            if (!m_bCacheInitialized)
            {
                const_cast<AlbumsNode*>(this)->prepareToShowData();
            }
            
            if (rowIndex < 0 || rowIndex >= static_cast<RowIndex_t>(m_albums.size()))
            {
                return "";
            }
            
            const auto &album = m_albums[rowIndex];
            const auto column = static_cast<Column>(columnIndex);
            
            switch (column)
            {
                case Column::Artist:
                    return album.albumArtist.empty() ? "Unknown Artist" : album.albumArtist;
                    
                case Column::Title:
                    return album.title;
                    
                case Column::Year:
                    return album.year.has_value() ? std::to_string(album.year.value()) : "";
                    
                case Column::TrackCount:
                    return std::to_string(album.trackCount);
                    
                case Column::Duration:
                {
                    const auto totalMs = album.totalDuration.count();
                    const auto seconds = totalMs / 1000;
                    const auto minutes = seconds / 60;
                    const auto hours = minutes / 60;
                    if (hours > 0)
                    {
                        return std::format("{}:{:02d}:{:02d}", hours, minutes % 60, seconds % 60);
                    }
                    else
                    {
                        return std::format("{}:{:02d}", minutes, seconds % 60);
                    }
                }
                    
                case Column::Folder:
                {
                    const auto &trackLibrary = theTrackLibrary;
                    const auto &folderDb = trackLibrary.getFolderDatabase();
                    const auto folderInfo = folderDb.getFolderById(album.folderId);
                    return folderInfo.has_value() ? folderInfo->path : "";
                }
                    
                case Column::AlbumId:
                    return std::to_string(album.albumId);
                    
                default:
                    return "";
            }
        }

        const TrackInfo *AlbumsNode::getTrackInfoForRow([[maybe_unused]] RowIndex_t rowIndex) const
        {
            // Albums don't have associated TrackInfo
            return nullptr;
        }

        const AlbumInfo *AlbumsNode::getAlbumInfoForRow(RowIndex_t rowIndex) const
        {
            if (!m_bCacheInitialized)
            {
                const_cast<AlbumsNode*>(this)->prepareToShowData();
            }
            
            if (rowIndex < 0 || rowIndex >= static_cast<RowIndex_t>(m_albums.size()))
            {
                return nullptr;
            }
            
            return &m_albums[rowIndex];
        }

        FolderId AlbumsNode::getFolderIdForRow(RowIndex_t rowIndex) const
        {
            const auto albumInfo = getAlbumInfoForRow(rowIndex);
            return albumInfo ? albumInfo->folderId : -1;
        }

        ObjectId AlbumsNode::getObjectIdForRow(RowIndex_t rowIndex) const
        {
            const auto albumInfo = getAlbumInfoForRow(rowIndex);
            return albumInfo ? albumInfo->albumId : -1;
        }

        void AlbumsNode::dataNoLongerShowing()
        {
            m_albums.clear();
            m_cachedRowCount = -1;
            m_bCacheInitialized = false;
        }

        bool AlbumsNode::setSearchTerms(const std::vector<std::string> &searchTerms)
        {
            m_searchTerms = searchTerms;
            refreshCache(true);
            return true;
        }

        std::vector<std::string> AlbumsNode::getCurrentSearchTerms() const
        {
            return m_searchTerms;
        }

        std::vector<SortOrderInfo> AlbumsNode::getCurrentSortOrder() const
        {
            return m_sortOrders;
        }

        bool AlbumsNode::setSortOrder(const std::vector<SortOrderInfo> &sortOrders)
        {
            m_sortOrders = sortOrders;
            if (m_bCacheInitialized)
            {
                prepareToShowData(); // Re-sort the data
            }
            return true;
        }

        const TrackQueryArgs *AlbumsNode::getQueryArgs() const
        {
            return nullptr; // Albums don't use TrackQueryArgs
        }

        void AlbumsNode::refreshCache(bool flushCache) const
        {
            if (flushCache)
            {
                m_albums.clear();
                m_cachedRowCount = -1;
                m_bCacheInitialized = false;
            }
            
            if (!m_bCacheInitialized)
            {
                const_cast<AlbumsNode*>(this)->prepareToShowData();
            }
        }

        std::vector<TrackId> AlbumsNode::getAllTrackIds() const
        {
            std::vector<TrackId> allTrackIds;
            
            if (!m_bCacheInitialized)
            {
                const_cast<AlbumsNode*>(this)->prepareToShowData();
            }
            
            const auto &trackLibrary = theTrackLibrary;
            const auto &albumManager = trackLibrary.getAlbumManager();
            
            // Collect all track IDs from all albums
            for (const auto &album : m_albums)
            {
                const auto trackIds = albumManager.getAlbumTracks(album.albumId);
                allTrackIds.insert(allTrackIds.end(), trackIds.begin(), trackIds.end());
            }
            
            return allTrackIds;
        }

    } // namespace database
} // namespace jucyaudio