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
     * @brief Unique identifier for audio tracks in the database
     * @note 64-bit signed integer allows for large track collections
     *       and potential future extensions
     */
    typedef int64_t TrackId;

    /**
     * @brief Unique identifier for metadata tags
     * @note Used for genre, mood, and other categorical metadata
     */
    typedef int64_t TagId;

    /**
     * @brief Unique identifier for working sets (track collections)
     * @note Working sets are user-defined collections of tracks for specific purposes
     */
    typedef int64_t WorkingSetId;

    /**
     * @brief Unique identifier for mixes (DJ sets)
     * @note Mixes represent ordered sequences of tracks with crossfade information
     */
    typedef int64_t MixId;

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

    /**
     * @brief Unique identifier for folder structures in the database
     * @note Used for organizing tracks in hierarchical folder structures
     */
    typedef int64_t FolderId;

    namespace database
    {
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
         * @brief Track identifier type for database namespace
         * @note Redefined in database namespace for compatibility
         * @deprecated Use jucyaudio::TrackId instead
         */
        typedef long long TrackId;

        /**
         * @brief Tag identifier type for database namespace
         * @note Redefined in database namespace for compatibility
         * @deprecated Use jucyaudio::TagId instead
         */
        typedef long long TagId;

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
            None,             ///< No action available/selected
            Play,             ///< Start playback of the selected item(s)
            CreateWorkingSet, ///< Create a new working set from selected items
            ShowDetails,      ///< Display detailed information in a separate view
            EditMetadata,     ///< Open metadata editor for the item
            Delete,           ///< Remove the item from database (with confirmation)
            CreateMix,        ///< Create a new DJ mix from selected tracks
            RemoveMix,        ///< Remove an existing mix
            ExportMix         ///< Export mix to audio file
        };

        /**
         * @brief Collection of available actions for a given context
         * @note Used to specify which actions are available for specific
         *       data items or UI contexts
         */
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

        /**
         * @brief Complete information about a working set
         * @details Working sets are user-defined collections of tracks organized
         *          for specific purposes (e.g., "Workout Music", "Party Playlist").
         *          They provide quick access to frequently used track combinations.
         */
        struct WorkingSetInfo
        {
            WorkingSetId id;           ///< Unique database identifier
            std::string name;          ///< User-defined name for the working set
            Timestamp_t timestamp;     ///< Creation or last modification time
            int64_t track_count;       ///< Number of tracks in the working set
            Duration_t total_duration; ///< Combined duration of all tracks
        };

    } // namespace database
} // namespace jucyaudio