/*
 * This file is part of jucyaudio.
 * Copyright (C) 2025 Gerson Kurz <not@p-nand-q.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file Constants.h
 * @brief Core type definitions and constants for the jucyaudio application
 *
 * This header defines fundamental types, constants, and data structures used
 * throughout the jucyaudio application. It provides type safety through strong
 * typing and normalization constants to avoid floating-point precision issues.
 */

/**
 * @brief Version string for the NGAUDIO engine component
 * @note This version may differ from the main application version
 */
#define NGAUDIO_ENGINE_VERSION "0.1.0"

namespace jucyaudio
{
    /**
     * @brief Duration type representing time intervals in milliseconds
     * @note Using milliseconds provides sufficient precision for audio applications
     *       while avoiding floating-point precision issues
     */
    using Duration_t = std::chrono::milliseconds;

    /**
     * @brief Timestamp type representing absolute points in time
     * @note Uses system clock for compatibility with standard time operations
     *       and database storage
     */
    using Timestamp_t = std::chrono::system_clock::time_point;

    /**
     * @brief Type for database row indices
     * @note 64-bit unsigned integer supports very large datasets
     */
    typedef uint64_t RowIndex_t;

    /**
     * @brief Type for column indices in data views
     * @note 32-bit unsigned integer is sufficient for typical column counts
     */
    typedef uint32_t ColumnIndex_t;

    /**
     * @brief Unique identifier for objects in the database
     * @note 64-bit signed integer allows for a large number of unique objects
     *       and is suitable for future extensions
     */
    typedef int64_t ObjectId;

    /**
     * @brief Unique identifier for audio tracks in the database
     * @note 64-bit signed integer allows for large track collections
     *       and potential future extensions
     */
    typedef ObjectId TrackId;

    /**
     * @brief Unique identifier for metadata tags
     * @note Used for genre, mood, and other categorical metadata
     */
    typedef int64_t TagId;

    /**
     * @brief Unique identifier for working sets (track collections)
     * @note Working sets are user-defined collections of tracks for specific purposes
     */
    typedef ObjectId WorkingSetId;

    /**
     * @brief Unique identifier for mixes (DJ sets)
     * @note Mixes represent ordered sequences of tracks with crossfade information
     */
    typedef ObjectId MixId;
    
     /**
     * @brief Track identifier type for database namespace
     * @note Redefined in database namespace for compatibility
     * @deprecated Use jucyaudio::TrackId instead
     */
    typedef ObjectId TrackId;

    /**
     * @brief Tag identifier type for database namespace
     * @note Redefined in database namespace for compatibility
     * @deprecated Use jucyaudio::TagId instead
     */
    typedef ObjectId TagId;

    /**
     * @brief Unique identifier for folder structures in the database
     * @note Used for organizing tracks in hierarchical folder structures
     */
    typedef ObjectId FolderId;

    /**
     * @brief Unique identifier for albums in the database
     * @note Albums are identified by the combination of title and folder_id
     */
    typedef ObjectId AlbumId;

    /**
     * @brief Unique identifier for markers (track or mix markers)
     * @note Markers are user-defined annotations at specific time positions
     */
    typedef ObjectId MarkerId;

    /**
     * @brief Volume level representation as integer to avoid floating-point issues
     * @note Stored as integer * VOLUME_NORMALIZATION for precision
     * @see VOLUME_NORMALIZATION
     */
    typedef int64_t Volume_t;

    /**
     * @brief Beats per minute representation as integer to avoid floating-point issues
     * @note Stored as integer * BPM_NORMALIZATION to preserve decimal precision
     * @see BPM_NORMALIZATION
     */
    typedef int64_t BPM_t;



    namespace database
    {
        // Forward declarations
        class INavigationNode;
        
        /**
         * @brief Normalization factor for volume calculations
         * @details Volume values are stored as integers multiplied by this factor
         *          to avoid floating-point precision issues. A value of 1000 means
         *          volume 1.0 is stored as 1000, volume 0.5 as 500, etc.
         * @note Range: 0-1000 represents 0%-100% volume
         */
        constexpr Volume_t VOLUME_NORMALIZATION = 1000;

        /**
         * @brief Normalization factor for BPM calculations
         * @details BPM values are stored as integers multiplied by this factor
         *          to preserve decimal precision. A value of 1000 means
         *          120.5 BPM is stored as 120500.
         * @note Allows for precise BPM measurements with 3 decimal places
         */
        constexpr BPM_t BPM_NORMALIZATION = 1000;

        struct TrackInfo; ///< Forward declaration for track information structure

        /**
         * @brief Specifies sort order for database queries
         * @note Used in TrackQueryArgs to define multi-column sorting
         */
        struct SortOrderInfo
        {
            std::string columnName; ///< Column name from DataColumn::name
            bool descending;        ///< true for descending order, false for ascending
        };

        /**
         * @brief Column alignment options for UI display
         * @note Used by DataColumn to specify text alignment in table views
         */
        enum class ColumnAlignment
        {
            Left,   ///< Left-aligned text (default for strings)
            Center, ///< Center-aligned text
            Right   ///< Right-aligned text (typical for numbers)
        };

        /**
         * @brief Data type hints for column formatting and validation
         * @note Provides UI components with information about expected data types
         *       for appropriate formatting and input validation
         */
        enum class ColumnDataTypeHint
        {
            String,   ///< Text data (artist, title, etc.)
            Integer,  ///< Whole numbers (track ID, BPM, etc.)
            Double,   ///< Decimal numbers (ratings, percentages)
            Date,     ///< Date/timestamp values
            Duration, ///< Time duration values (track length, etc.)
            Rating    ///< Rating values (typically 1-5 stars)
        };

        /**
         * @brief Available actions that can be performed on data items
         * @details Represents user interface actions available in context menus,
         *          toolbars, and other UI elements. These actions are context-sensitive
         *          and may not all be available for every data item.
         * @note This enum may be extended as new features are added
         */
        enum class DataAction
        {
            None,                   ///< No action available/selected
            Play,                   ///< Start playback of the selected item(s)
            CreateWorkingSet,       ///< Create a new working set from selected items
            CreateMix,              ///< Create a new DJ mix from selected tracks
            ShowDetails,            ///< Display detailed information in a separate view
            EditWorkingSetMetadata, ///< Open metadata editor for the item
            EditMixMetadata,        ///< Open metadata editor for the item
            RemoveTracks,           ///< Remove selected tracks from a mix or working set
            Delete,                 ///< Delete the selected working set
            ExportMix,              ///< Export mix to audio file
            RunBpmAnalysis,         ///< Run BPM analysis on selected items
            ShowMixEditor,          ///< Open the mix editor for the selected mix
            ShowTrackEditor,        ///< Open the track editor for the selected track
            ShowInFolder,           ///< Navigate to the folder containing the item
            Separator,              ///< Separator in context menus (not an action)
            RemoveDuplicates,       ///< Remove duplicate tracks from the selection
            Settings,               ///< Open application settings dialog
            ScanFolders,            ///< Open folder scanning dialog 
            ShowEqualizer,          ///< Show/hide the equalizer window
            ShowReverb,             ///< Show/hide the reverb window
        };

        // @brief Collection of data actions available in the application
        using DataActions = std::vector<DataAction>;

        /**
         * @brief Simple structure combining tag ID with display name
         * @note Used for efficient transfer of tag information between
         *       database layer and UI components
         */
        struct TagInfo
        {
            TagId id;         ///< Unique database identifier for the tag
            std::string name; ///< Human-readable tag name for display
        };

        // ===== Node-Centric Command Architecture Types =====
        
        /**
         * @brief Semantic states for a row or cell. The UI theme maps these to actual styles.
         */
        enum class RenderState
        {
            Normal,     ///< Default text for standard items like track titles
            Accent,     ///< Important information, such as a folder name or selected item
            Subdued,    ///< Secondary information, like track count or file format
            Inactive    ///< Offline or otherwise unavailable content
        };
        
        /**
         * @brief Information for rendering a cell in the data view
         */
        struct CellRenderInfo
        {
            std::string text;
            RenderState state = RenderState::Normal;
        };
        
        /**
         * @brief Types of results from row activation
         */
        enum class RowActivationResultType
        {
            NoAction,
            NavigateToNode,
            NavigateToFolder,  // Navigate to a folder by ID (e.g., album folder)
            PlayTrack
        };
        
        /**
         * @brief Result of a row activation event
         */
        struct RowActivationResult
        {
            RowActivationResultType type = RowActivationResultType::NoAction;
            
            // Valid if type is NavigateToNode.
            // Ownership: Node returns a retained pointer; caller must release.
            INavigationNode* newNode = nullptr;
            
            // Valid if type is NavigateToFolder.
            FolderId targetFolderId = -1;
            
            // No additional data needed for PlayTrack - we have the row index
        };

        // Simple status for operations, can be expanded
        enum class DbResultStatus
        {
            Ok,
            ErrorGeneric,
            ErrorNotFound,
            ErrorAlreadyExists, // e.g., for unique constraints
            ErrorConstraintFailed,
            ErrorIO,
            ErrorConnection,
            ErrorDB
        };

        struct DbResult
        {
            DbResultStatus status = DbResultStatus::Ok;
            std::string errorMessage;

            DbResult(DbResultStatus s = DbResultStatus::Ok, std::string msg = "")
                : status{s},
                  errorMessage{std::move(msg)}
            {
            }

            bool isOk() const
            {
                return status == DbResultStatus::Ok;
            }
            static DbResult success()
            {
                return DbResult{};
            }
            static DbResult failure(DbResultStatus s, std::string msg)
            {
                return DbResult{s, std::move(msg)};
            }
        };

    } // namespace database

} // namespace jucyaudio