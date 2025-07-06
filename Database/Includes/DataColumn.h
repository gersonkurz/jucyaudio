#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include <Database/Includes/Constants.h>

namespace jucyaudio
{
    namespace database
    {
        struct DataColumn
        {
            /// @brief Column index type, starting from 0.
            ColumnIndex_t index{0}; // Column index, starting from 0

            /// @brief SQL-id for this column
            std::string sqlId;

            /// @brief Unique identifier for the column. This should be unique across all columns in a navigation node.
            std::string name;

            /// @brief Default display width. Not sure this is needed, probably the UI could decide upon that on its own?
            int32_t defaultWidth{100};

            /// @brief Alignment of the column in the UI. Not sure this is needed - because we also have the DataTypeHint, and the UI
            /// can use that to determine how to align the text.
            ColumnAlignment alignment{ColumnAlignment::Left};

            /// @brief Type hint for the column data. Needed mostly for sorting and potentially UI formatting but see DatValue belo
            ColumnDataTypeHint typeHint{ColumnDataTypeHint::String};
        };

    } // namespace database
} // namespace jucyaudio
