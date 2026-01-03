#include "FilterParser.h"
#include <spdlog/spdlog.h>
#include <cctype>
#include <unordered_set>
#include <unordered_map>

namespace jucyaudio
{
    namespace utils
    {
        // Field name to SQL column mapping
        static const std::unordered_map<std::string, std::string> fieldToColumnMap = {
            {"year", "year"},
            {"track_number", "track_number"},
            {"track", "track_number"},  // Alias
            {"disc_number", "disc_number"},
            {"disc", "disc_number"},  // Alias
            {"bitrate", "bitrate"},
            {"rating", "rating"},
            {"channels", "channels"},
            {"samplerate", "samplerate"},
            {"sample_rate", "samplerate"},  // Alias
            {"play_count", "play_count"},
            {"artist", "artist_name"},
            {"album", "album_title"},
            {"album_artist", "album_artist_name"},
            {"title", "title"},
            {"codec", "codec_name"},
            {"bpm", "bpm"},
            {"key", "key_string"},
            {"liked", "liked_status"}
        };

        // Numeric fields (for type validation)
        static const std::unordered_set<std::string> numericFields = {
            "year", "track_number", "disc_number", "bitrate", "rating",
            "channels", "samplerate", "play_count", "bpm", "liked_status"
        };

        std::string mapFieldToColumn(const std::string& fieldName)
        {
            auto it = fieldToColumnMap.find(fieldName);
            if (it != fieldToColumnMap.end())
            {
                return it->second;
            }
            return "";  // Unknown field
        }

        bool isNumericField(const std::string& fieldName)
        {
            // Map to column name first, then check if numeric
            const auto columnName = mapFieldToColumn(fieldName);
            return numericFields.find(columnName) != numericFields.end();
        }

        // Helper: Skip whitespace
        static void skipWhitespace(const std::string& str, size_t& pos)
        {
            while (pos < str.length() && std::isspace(static_cast<unsigned char>(str[pos])))
            {
                ++pos;
            }
        }

        // Helper: Extract quoted string
        static std::string extractQuotedString(const std::string& str, size_t& pos)
        {
            ++pos;  // Skip opening quote
            std::string result;
            while (pos < str.length())
            {
                if (str[pos] == '"')
                {
                    ++pos;  // Skip closing quote
                    return result;
                }
                if (str[pos] == '\\' && pos + 1 < str.length())
                {
                    ++pos;  // Skip escape char
                    result += str[pos];  // Add escaped char
                    ++pos;
                }
                else
                {
                    result += str[pos];
                    ++pos;
                }
            }
            // Unclosed quote - return what we have
            return result;
        }

        // Helper: Extract a token (non-whitespace, non-special)
        static std::string extractToken(const std::string& str, size_t& pos)
        {
            std::string token;
            while (pos < str.length() && !std::isspace(static_cast<unsigned char>(str[pos])))
            {
                token += str[pos];
                ++pos;
            }
            return token;
        }

        // Helper: Validate that a string is a valid numeric value (digits, optional decimal point, optional leading minus)
        static bool isValidNumericValue(const std::string& value)
        {
            if (value.empty())
            {
                return false;
            }

            size_t start = 0;
            if (value[0] == '-')
            {
                start = 1;
                if (value.length() == 1)
                {
                    return false;  // Just a minus sign
                }
            }

            bool hasDecimal = false;
            for (size_t i = start; i < value.length(); ++i)
            {
                if (value[i] == '.')
                {
                    if (hasDecimal)
                    {
                        return false;  // Multiple decimal points
                    }
                    hasDecimal = true;
                }
                else if (!std::isdigit(static_cast<unsigned char>(value[i])))
                {
                    return false;  // Non-digit, non-decimal character
                }
            }

            return true;
        }

        // Helper: Try to parse a filter criterion from a token
        static bool tryParseFilter(const std::string& token, FilterCriterion& criterion)
        {
            // Find the colon
            const auto colonPos = token.find(':');
            if (colonPos == std::string::npos || colonPos == 0)
            {
                return false;  // No colon or colon at start
            }

            const auto fieldName = token.substr(0, colonPos);

            // Check if field is recognized
            if (mapFieldToColumn(fieldName).empty())
            {
                return false;  // Unknown field
            }

            std::string valueStr = token.substr(colonPos + 1);
            if (valueStr.empty())
            {
                return false;  // No value after colon
            }

            criterion.fieldName = fieldName;

            // Check for range operator (..)
            const auto rangePos = valueStr.find("..");
            if (rangePos != std::string::npos && rangePos > 0 && valueStr.length() > 2 && rangePos < valueStr.length() - 2)
            {
                criterion.op = "..";
                criterion.value = valueStr.substr(0, rangePos);
                criterion.value2 = valueStr.substr(rangePos + 2);

                // Validate numeric values for range queries (ranges only make sense for numeric fields)
                if (!isValidNumericValue(criterion.value) || !isValidNumericValue(criterion.value2))
                {
                    return false;  // Invalid numeric values in range
                }
                return true;
            }

            // Check for comparison operators (>=, <=, >, <)
            if (valueStr[0] == '>')
            {
                if (valueStr.length() > 1 && valueStr[1] == '=')
                {
                    criterion.op = ">=";
                    criterion.value = valueStr.substr(2);
                }
                else
                {
                    criterion.op = ">";
                    criterion.value = valueStr.substr(1);
                }
                // Comparison operators require valid numeric values
                return !criterion.value.empty() && isValidNumericValue(criterion.value);
            }
            else if (valueStr[0] == '<')
            {
                if (valueStr.length() > 1 && valueStr[1] == '=')
                {
                    criterion.op = "<=";
                    criterion.value = valueStr.substr(2);
                }
                else
                {
                    criterion.op = "<";
                    criterion.value = valueStr.substr(1);
                }
                // Comparison operators require valid numeric values
                return !criterion.value.empty() && isValidNumericValue(criterion.value);
            }

            // Default to exact match
            criterion.op = "=";
            criterion.value = valueStr;

            // For numeric fields, validate the value is actually numeric
            if (isNumericField(fieldName) && !isValidNumericValue(criterion.value))
            {
                return false;  // Non-numeric value for a numeric field
            }

            return true;
        }

        ParsedFilter parseFilterString(const std::string& input)
        {
            ParsedFilter result;
            std::string remainingTokens;

            size_t pos = 0;
            while (pos < input.length())
            {
                skipWhitespace(input, pos);
                if (pos >= input.length())
                {
                    break;
                }

                // Handle quoted strings - pass through to remaining text WITH quotes for FTS5 phrase search
                if (input[pos] == '"')
                {
                    const auto quotedStr = extractQuotedString(input, pos);
                    if (!remainingTokens.empty())
                    {
                        remainingTokens += " ";
                    }
                    // Re-add quotes so FTS5 can do phrase search
                    remainingTokens += "\"";
                    remainingTokens += quotedStr;
                    remainingTokens += "\"";
                    continue;
                }

                // Extract next token
                const auto token = extractToken(input, pos);
                if (token.empty())
                {
                    continue;
                }

                // Try to parse as filter
                FilterCriterion criterion;
                if (tryParseFilter(token, criterion))
                {
                    result.criteria.push_back(criterion);
                    spdlog::debug("Parsed filter: {}:{}{}", criterion.fieldName, criterion.op, criterion.value);
                }
                else
                {
                    // Not a filter, add to remaining text
                    if (!remainingTokens.empty())
                    {
                        remainingTokens += " ";
                    }
                    remainingTokens += token;
                }
            }

            result.remainingText = remainingTokens;
            return result;
        }

    } // namespace utils
} // namespace jucyaudio
