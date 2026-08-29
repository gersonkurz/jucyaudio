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

#include <Database/Includes/Constants.h>
#include <functional>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief Sets the headline genre of an album, from inside the mix editor.
         *
         * Occupies the strip the waveform vacates while a mix is being edited. The vocabulary comes from
         * the Genres table; the assignment is written straight to Albums.genres, ordered, with the first
         * entry being the headline genre that decides where the album would eventually be filed.
         *
         * The layout is alphabetical and fixed on purpose: a chip must never move between the glance and
         * the click. How often a genre is used is expressed as colour only (see @ref Tier).
         */
        class GenreCloudComponent final : public juce::Component
        {
        public:
            GenreCloudComponent();
            ~GenreCloudComponent() override = default;

            void paint(juce::Graphics &g) override;
            void resized() override;
            void mouseDown(const juce::MouseEvent &event) override;
            void mouseMove(const juce::MouseEvent &event) override;
            void mouseExit(const juce::MouseEvent &event) override;

            /// @brief Re-read the vocabulary and its usage counts from the database.
            /// @note Call when the panel becomes visible; the component outlives any single library state.
            void refreshVocabulary();

            /// @brief Point the cloud at a track, so its album's genres can be edited.
            /// @param trackId The selected mix track, or -1 to clear.
            void setContextTrack(TrackId trackId);

            /// @brief Height needed to lay out every chip at the given width.
            /// @param width The width this component would be given.
            /// @return Required height in pixels, including the header line.
            int getHeightForWidth(int width) const;

            /// @brief Called when the chip layout changed shape and the owner should re-run its layout.
            std::function<void()> onLayoutNeeded;

            /// @brief Called with a user-facing message when a genre change could not be saved.
            std::function<void(const juce::String &)> onError;

        private:
            /// @brief Usage bands, by rank within the vocabulary rather than by absolute count.
            enum class Tier
            {
                Top5,  ///< Most-used 5% - accent colour.
                Top20, ///< Next band up to 20% - full foreground.
                Top50, ///< Next band up to 50% - dimmed foreground.
                Rest   ///< Everything else - disabled foreground.
            };

            struct Chip final
            {
                juce::String name;
                int usage{0};
                Tier tier{Tier::Rest};
                juce::Rectangle<int> bounds;
            };

            static constexpr int kHeaderHeight = 18;
            static constexpr int kHeaderGap = 4;
            static constexpr int kChipHeight = 22;
            static constexpr int kChipHPad = 9;
            static constexpr int kChipGap = 6;
            static constexpr int kRowGap = 4;
            static constexpr int kMargin = 6;

            void assignTiers();
            int chipWidth(const juce::String &name) const;
            /// @brief Runs the flow layout; writes chip bounds when @p store is true.
            int layoutChips(int width, bool store);
            juce::Colour tierColour(Tier tier) const;
            /// @brief Chip index at a point, m_chips.size() for the "add genre" chip, or -1.
            int indexAt(juce::Point<int> position) const;
            int selectionIndexOf(const juce::String &name) const;
            /// @brief Left-click: make this the album's only genre, or clear it if it already is.
            void setOnlyGenre(int chipIndex);
            /// @brief Ctrl/Cmd-click: add or remove this genre alongside the headline.
            void toggleSecondaryGenre(int chipIndex);
            void promoteGenre(int chipIndex);
            void promptForNewGenre();
            /// @brief Right-click: the actions available for one chip.
            /// @param chipIndex The chip under the mouse.
            /// @note Promoting needs an album to write to; renaming does not, because it changes the
            ///       vocabulary rather than a label. The menu is offered either way and drops the
            ///       entries that cannot act.
            void showChipMenu(int chipIndex);
            /// @brief Index of the chip with this exact name, or -1. Used to re-find a chip after an
            ///        asynchronous menu, where the index it was built from may no longer mean anything.
            int chipIndexOf(const juce::String &name) const;
            /// @brief Asks for a new name for a chip and applies it to the vocabulary and every album.
            void promptToRenameGenre(int chipIndex);
            /// @brief Writes a genre list to Albums.genres, creating the album row if needed.
            /// @param genres The list to store, headline first.
            /// @return true if it reached the database.
            bool writeGenres(const std::vector<std::string> &genres);
            /// @brief Writes a genre list and, only if that succeeded, adopts it as the shown state.
            /// @param next The candidate selection.
            void commitGenres(const std::vector<std::string> &next);
            void reportError(const juce::String &message);
            void adjustUsage(const juce::String &name, int delta);
            void updateHeaderText();
            void relayout();

            std::vector<Chip> m_chips;
            Chip m_addChip{"+ add genre", 0, Tier::Rest, {}};
            /// @brief Genre names in headline-first order; the strings, not indices, so a genre that is
            ///        no longer in the vocabulary survives a round trip instead of being silently dropped.
            std::vector<std::string> m_selected;
            int m_hoverIndex{-1};

            // Context: the album the current selection belongs to.
            FolderId m_folderId{-1};
            AlbumId m_albumId{-1};
            std::string m_albumTitle;
            std::string m_albumArtist;
            std::optional<int> m_year;
            /// @brief Preserved verbatim so writing genres does not wipe the other metadata columns.
            std::vector<std::string> m_moods;
            std::vector<std::string> m_tags;

            juce::String m_albumText{"(no track selected)"};
            juce::String m_headerText;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GenreCloudComponent)
        };

    } // namespace ui
} // namespace jucyaudio
