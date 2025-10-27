#pragma once

#include <string>
#include <vector>

namespace jucyaudio
{
    namespace utils
    {
        /**
         * @brief Represents a single filter criterion extracted from search text
         * @details Examples:
         *   - year:1991 → fieldName="year", op="=", value="1991"
         *   - bpm:>120 → fieldName="bpm", op=">", value="120"
         *   - year:1990..1995 → fieldName="year", op="..", value="1990", value2="1995"
         */
        struct FilterCriterion
        {
            std::string fieldName;  ///< Field to filter on (e.g., "year", "bpm", "artist")
            std::string op;         ///< Operator: "=", ">", "<", ">=", "<=", ".."
            std::string value;      ///< Primary value or first value in range
            std::string value2;     ///< Second value for range operator (..)
        };

        /**
         * @brief Result of parsing a filter string
         * @details Separates structured filters from free-text search terms
         */
        struct ParsedFilter
        {
            std::vector<FilterCriterion> criteria;  ///< Extracted filter criteria
            std::string remainingText;              ///< Text to pass to FTS5 search
        };

        /**
         * @brief Parses a search string into structured filters and remaining text
         *
         * @details
         * Syntax:
         *   - field:value → exact match
         *   - field:>value, field:<value, field:>=value, field:<=value → comparisons
         *   - field:value1..value2 → range (inclusive)
         *   - "quoted text" → literal, not parsed as filter
         *   - Unrecognized patterns → passed to remainingText for FTS5
         *
         * Examples:
         *   - "year:1991" → {criteria: [{year, =, 1991}], remainingText: ""}
         *   - "bpm:>120 techno" → {criteria: [{bpm, >, 120}], remainingText: "techno"}
         *   - "year:1990..1995 artist:Boards" → {criteria: [{year, .., 1990, 1995}, {artist, =, Boards}], remainingText: ""}
         *   - "\"year:1999\"" → {criteria: [], remainingText: "year:1999"}
         *
         * @param input The search string to parse
         * @return ParsedFilter containing structured criteria and remaining text
         */
        ParsedFilter parseFilterString(const std::string& input);

        /**
         * @brief Maps a user-friendly field name to the SQL column name
         *
         * @details
         * Mappings:
         *   - year, track_number, disc_number, bitrate, rating, channels, samplerate, play_count → same
         *   - artist → artist_name
         *   - album → album_title
         *   - album_artist → album_artist_name
         *   - title → title
         *   - codec → codec_name
         *   - bpm → bpm
         *   - key → key_string
         *   - liked → liked_status
         *
         * @param fieldName User-provided field name
         * @return SQL column name, or empty string if field is unknown
         */
        std::string mapFieldToColumn(const std::string& fieldName);

        /**
         * @brief Determines if a field represents a numeric column
         * @param fieldName The field name to check
         * @return true if the field is numeric (int), false for string fields
         */
        bool isNumericField(const std::string& fieldName);

    } // namespace utils
} // namespace jucyaudio
