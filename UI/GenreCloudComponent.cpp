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

#include <UI/GenreCloudComponent.h>

#include <Database/TrackLibrary.h>
#include <UI/CustomColourIds.h>
#include <Utils/AssortedUtils.h>
#include <algorithm>
#include <numeric>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        namespace
        {
            juce::Font chipFont()
            {
                return juce::Font{juce::FontOptions{}.withHeight(13.0f)};
            }

            juce::Font headerFont()
            {
                return juce::Font{juce::FontOptions{}.withHeight(13.0f).withStyle("Bold")};
            }

            /// @brief Whether a genre name is in a list, by the vocabulary's own notion of sameness.
            ///
            /// noCaseKey rather than juce::String::equalsIgnoreCase, which folds Unicode. The Genres
            /// table is UNIQUE COLLATE NOCASE and SQLite folds ASCII only, so it can hold two rows a
            /// Unicode-aware comparison calls one. Everything that decides which vocabulary entry a
            /// label belongs to has to agree with the table, or the cloud shows one chip for two
            /// rows and acts on whichever it finds first.
            bool containsName(const std::vector<std::string> &names, const std::string &name)
            {
                const auto needle{noCaseKey(name)};
                return std::any_of(names.begin(),
                                   names.end(),
                                   [&needle](const std::string &candidate) { return noCaseKey(candidate) == needle; });
            }
        } // namespace

        GenreCloudComponent::GenreCloudComponent()
        {
            updateHeaderText();
        }

        void GenreCloudComponent::refreshVocabulary()
        {
            const auto vocabulary{database::theTrackLibrary.getAlbumManager().getGenresWithUsage()};

            m_chips.clear();
            m_chips.reserve(vocabulary.size());
            for (const auto &entry : vocabulary)
            {
                m_chips.push_back(Chip{juce::String::fromUTF8(entry.name.c_str()), entry.albumCount, Tier::Rest, {}});
            }

            assignTiers();
            relayout();
        }

        void GenreCloudComponent::assignTiers()
        {
            if (m_chips.empty())
            {
                return;
            }

            // Rank by usage, then band by position in that ranking rather than by absolute count: usage is
            // heavily skewed, so absolute thresholds would put almost everything in the bottom tier.
            std::vector<size_t> order(m_chips.size());
            std::iota(order.begin(), order.end(), size_t{0});
            std::stable_sort(order.begin(),
                             order.end(),
                             [this](size_t a, size_t b)
                             {
                                 return m_chips[a].usage > m_chips[b].usage;
                             });

            const auto total = static_cast<double>(m_chips.size());
            for (size_t rank = 0; rank < order.size(); ++rank)
            {
                const auto fraction = static_cast<double>(rank) / total;
                auto &chip = m_chips[order[rank]];
                if (fraction < 0.05)
                {
                    chip.tier = Tier::Top5;
                }
                else if (fraction < 0.20)
                {
                    chip.tier = Tier::Top20;
                }
                else if (fraction < 0.50)
                {
                    chip.tier = Tier::Top50;
                }
                else
                {
                    chip.tier = Tier::Rest;
                }
            }
        }

        juce::Colour GenreCloudComponent::tierColour(Tier tier) const
        {
            auto &lookAndFeel = getLookAndFeel();
            const auto foreground = lookAndFeel.findColour(mainForegroundColourId);
            const auto background = lookAndFeel.findColour(mainBackgroundColourId);

            switch (tier)
            {
            case Tier::Top5:
                return lookAndFeel.findColour(accentColourId);
            case Tier::Top20:
                return foreground;
            case Tier::Top50:
                return foreground.interpolatedWith(background, 0.35f);
            case Tier::Rest:
            default:
                return lookAndFeel.findColour(disabledForegroundColourId);
            }
        }

        int GenreCloudComponent::chipWidth(const juce::String &name) const
        {
            return juce::GlyphArrangement::getStringWidthInt(chipFont(), name) + (kChipHPad * 2);
        }

        int GenreCloudComponent::layoutChips(int width, bool store)
        {
            const int usableWidth = juce::jmax(1, width - (kMargin * 2));
            int x = kMargin;
            int y = kMargin + kHeaderHeight + kHeaderGap;

            const auto place = [&](Chip &chip)
            {
                const int w = chipWidth(chip.name);
                if (x > kMargin && (x - kMargin) + w > usableWidth)
                {
                    x = kMargin;
                    y += kChipHeight + kRowGap;
                }

                if (store)
                {
                    chip.bounds = juce::Rectangle<int>{x, y, w, kChipHeight};
                }

                x += w + kChipGap;
            };

            for (auto &chip : m_chips)
            {
                place(chip);
            }
            place(m_addChip);

            return y + kChipHeight + kMargin;
        }

        int GenreCloudComponent::getHeightForWidth(int width) const
        {
            // layoutChips only mutates chip bounds when asked to store, so this const_cast is safe and
            // keeps a single copy of the wrapping logic.
            return const_cast<GenreCloudComponent *>(this)->layoutChips(width, false);
        }

        void GenreCloudComponent::resized()
        {
            layoutChips(getWidth(), true);
        }

        void GenreCloudComponent::relayout()
        {
            layoutChips(getWidth(), true);
            if (onLayoutNeeded)
            {
                onLayoutNeeded();
            }
            repaint();
        }

        int GenreCloudComponent::indexAt(juce::Point<int> position) const
        {
            for (size_t i = 0; i < m_chips.size(); ++i)
            {
                if (m_chips[i].bounds.contains(position))
                {
                    return static_cast<int>(i);
                }
            }
            return m_addChip.bounds.contains(position) ? static_cast<int>(m_chips.size()) : -1;
        }

        int GenreCloudComponent::selectionIndexOf(const juce::String &name) const
        {
            // See containsName: the vocabulary's identity rule, not JUCE's Unicode-aware one.
            const auto needle{noCaseKey(name.toStdString())};
            for (size_t i = 0; i < m_selected.size(); ++i)
            {
                if (noCaseKey(m_selected[i]) == needle)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        void GenreCloudComponent::updateHeaderText()
        {
            if (m_selected.empty())
            {
                m_headerText = m_albumText + "   -   no genre";
                return;
            }

            juce::String genres;
            for (size_t i = 0; i < m_selected.size(); ++i)
            {
                genres << (i == 0 ? "" : ", ") << juce::String::fromUTF8(m_selected[i].c_str());
            }
            m_headerText = m_albumText + "   -   " + genres;
        }

        void GenreCloudComponent::setContextTrack(TrackId trackId)
        {
            m_folderId = -1;
            m_albumId = -1;
            m_albumTitle.clear();
            m_albumArtist.clear();
            m_year.reset();
            m_selected.clear();
            m_moods.clear();
            m_tags.clear();

            if (trackId < 0)
            {
                m_albumText = "(no track selected)";
                updateHeaderText();
                repaint();
                return;
            }

            const auto track{database::theTrackLibrary.getTrackById(trackId)};
            if (!track)
            {
                m_albumText = "(track not found)";
                updateHeaderText();
                repaint();
                return;
            }

            m_folderId = track->folderId;
            m_albumTitle = track->album_title;
            m_albumArtist = track->album_artist_name.empty() ? track->artist_name : track->album_artist_name;
            if (track->year > 0)
            {
                m_year = track->year;
            }

            // One folder holds at most one album row, so the first hit is the album. Deliberately does not
            // create a row: merely selecting tracks must not write to the database.
            const auto albums{database::theTrackLibrary.getAlbumManager().getAlbumsInFolder(m_folderId)};
            if (!albums.empty())
            {
                m_albumId = albums.front().albumId;
                m_selected = albums.front().genres;
                m_moods = albums.front().moods;
                m_tags = albums.front().tags;
                if (m_albumTitle.empty())
                {
                    m_albumTitle = albums.front().title;
                }
            }

            const auto artist{m_albumArtist.empty() ? std::string{"(unknown artist)"} : m_albumArtist};
            const auto title{m_albumTitle.empty() ? std::string{"(unknown album)"} : m_albumTitle};
            m_albumText = juce::String::fromUTF8(artist.c_str()) + " - " + juce::String::fromUTF8(title.c_str());

            updateHeaderText();
            repaint();
        }

        void GenreCloudComponent::adjustUsage(const juce::String &name, int delta)
        {
            // See containsName: this moves a usage count, so it must land on the chip the
            // database would have counted, not on a different row that merely folds the same.
            const auto needle{noCaseKey(name.toStdString())};
            for (auto &chip : m_chips)
            {
                if (noCaseKey(chip.name.toStdString()) == needle)
                {
                    chip.usage = juce::jmax(0, chip.usage + delta);
                    assignTiers();
                    return;
                }
            }
        }

        void GenreCloudComponent::setOnlyGenre(int chipIndex)
        {
            const auto &name = m_chips[static_cast<size_t>(chipIndex)].name;

            // One click labels an album. Clicking the current headline again clears it, so a mistake costs
            // one more click rather than a trip to a menu.
            std::vector<std::string> next;
            if (selectionIndexOf(name) != 0)
            {
                next.push_back(name.toStdString());
            }

            commitGenres(next);
        }

        void GenreCloudComponent::toggleSecondaryGenre(int chipIndex)
        {
            const auto &name = m_chips[static_cast<size_t>(chipIndex)].name;
            const int selectionIndex = selectionIndexOf(name);

            auto next{m_selected};
            if (selectionIndex >= 0)
            {
                next.erase(next.begin() + selectionIndex);
            }
            else
            {
                next.push_back(name.toStdString());
            }

            commitGenres(next);
        }

        void GenreCloudComponent::promoteGenre(int chipIndex)
        {
            const auto &name = m_chips[static_cast<size_t>(chipIndex)].name;
            const int selectionIndex = selectionIndexOf(name);
            if (selectionIndex == 0)
            {
                return;
            }

            auto next{m_selected};
            if (selectionIndex > 0)
            {
                next.erase(next.begin() + selectionIndex);
            }
            next.insert(next.begin(), name.toStdString());

            commitGenres(next);
        }

        void GenreCloudComponent::reportError(const juce::String &message)
        {
            spdlog::error("GenreCloud: {}", message.toStdString());
            if (onError)
            {
                onError(message);
            }
        }

        void GenreCloudComponent::commitGenres(const std::vector<std::string> &next)
        {
            // Write first, adopt second. If the database rejects the change the panel must keep showing
            // what is actually stored, not what the click implied.
            if (!writeGenres(next))
            {
                return;
            }

            for (const auto &name : next)
            {
                if (!containsName(m_selected, name))
                {
                    adjustUsage(juce::String::fromUTF8(name.c_str()), +1);
                }
            }
            for (const auto &name : m_selected)
            {
                if (!containsName(next, name))
                {
                    adjustUsage(juce::String::fromUTF8(name.c_str()), -1);
                }
            }

            m_selected = next;
            updateHeaderText();
            repaint();
        }

        bool GenreCloudComponent::writeGenres(const std::vector<std::string> &genres)
        {
            if (m_folderId < 0)
            {
                spdlog::warn("GenreCloud: no album context, genre change not saved.");
                return false;
            }

            auto &albumManager = database::theTrackLibrary.getAlbumManager();

            if (m_albumId < 0)
            {
                // Created on first assignment rather than on selection: roughly 10% of haul folders have
                // tracks but no album row, and we only want rows for albums actually being labelled.
                auto title{m_albumTitle};
                if (title.empty())
                {
                    if (const auto folder{database::theTrackLibrary.getFolderDatabase().getFolderById(m_folderId)})
                    {
                        title = folder->name;
                    }
                }
                if (title.empty())
                {
                    reportError("Cannot save genre: this album has no title.");
                    return false;
                }

                m_albumId = albumManager.findOrCreateAlbum(title, m_folderId, m_albumArtist, m_year);
                if (m_albumId < 0)
                {
                    reportError("Cannot save genre: failed to create the album record.");
                    return false;
                }
                m_albumTitle = title;
            }

            // Moods and tags are round-tripped: updateAlbumMetadata replaces all three columns.
            if (!albumManager.updateAlbumMetadata(m_albumId, genres, m_moods, m_tags))
            {
                reportError("Failed to save genre change to the database.");
                return false;
            }

            return true;
        }

        int GenreCloudComponent::chipIndexOf(const juce::String &name) const
        {
            for (size_t i = 0; i < m_chips.size(); ++i)
            {
                if (m_chips[i].name == name)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        void GenreCloudComponent::showChipMenu(int chipIndex)
        {
            const auto name = m_chips[static_cast<size_t>(chipIndex)].name;

            juce::PopupMenu menu;

            // Promoting writes to an album, so it is offered only when there is one, and greyed
            // rather than hidden when this genre is already the headline - the same no-op
            // promoteGenre() would perform, said out loud instead of silently.
            if (m_folderId >= 0)
            {
                menu.addItem(1, "Make headline genre", selectionIndexOf(name) != 0);
            }
            menu.addItem(2, "Rename...");

            // The album this menu was opened against, captured alongside the name. The menu is
            // asynchronous and playback moves the context on its own - MainComponent calls
            // setContextTrack() as the played track changes - so by the time an item is chosen the
            // cloud can be pointed at a different album. Promoting then would label an album the
            // user never had in front of them.
            const auto contextFolderId = m_folderId;

            juce::Component::SafePointer<GenreCloudComponent> safeThis{this};
            menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this).withTargetScreenArea(
                                   juce::Rectangle<int>{localPointToGlobal(m_chips[static_cast<size_t>(chipIndex)].bounds.getBottomLeft()), {1, 1}}),
                               [safeThis, name, contextFolderId](int result)
                               {
                                   if (safeThis == nullptr || result == 0)
                                   {
                                       return;
                                   }

                                   auto *self = safeThis.getComponent();

                                   // By name, not by the index the menu was built from: the
                                   // vocabulary can have been re-read while the menu was open, and
                                   // an index into a list that has since changed points at whatever
                                   // happens to sit there now.
                                   const auto current = self->chipIndexOf(name);
                                   if (current < 0)
                                   {
                                       return;
                                   }

                                   if (result == 1)
                                   {
                                       if (self->m_folderId != contextFolderId)
                                       {
                                           spdlog::info("GenreCloud: the album changed while the menu was open; not promoting.");
                                           return;
                                       }
                                       self->promoteGenre(current);
                                   }
                                   else if (result == 2)
                                   {
                                       // Deliberately not guarded on the album. Renaming edits the
                                       // vocabulary and every album using it, so which one happens to
                                       // be selected does not enter into it.
                                       self->promptToRenameGenre(current);
                                   }
                               });
        }

        void GenreCloudComponent::promptToRenameGenre(int chipIndex)
        {
            const auto oldName = m_chips[static_cast<size_t>(chipIndex)].name;

            auto *window = new juce::AlertWindow("Rename Genre", "Rename this genre everywhere it is used:", juce::AlertWindow::NoIcon);
            window->addTextEditor("genre", oldName, "Genre:");
            window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
            window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

            // The modal outlives the click, so the component must not be captured raw.
            juce::Component::SafePointer<GenreCloudComponent> safeThis{this};
            window->enterModalState(true,
                                    juce::ModalCallbackFunction::create(
                                        [safeThis, window, oldName](int result)
                                        {
                                            std::unique_ptr<juce::AlertWindow> windowDeleter{window}; //-V824

                                            if (result != 1 || safeThis == nullptr)
                                            {
                                                return;
                                            }

                                            const auto newName{window->getTextEditorContents("genre").trim()};
                                            if (newName.isEmpty() || newName == oldName)
                                            {
                                                return;
                                            }

                                            auto *self = safeThis.getComponent();

                                            bool merged = false;
                                            if (!database::theTrackLibrary.getAlbumManager().renameGenre(oldName.toStdString(), newName.toStdString(), &merged))
                                            {
                                                self->reportError("Could not rename \"" + oldName + "\".");
                                                return;
                                            }

                                            // Re-read rather than patching the chip in place: a merge removes a
                                            // second chip and changes the usage counts that decide the tiers, so
                                            // the cloud would otherwise show a vocabulary the database does not
                                            // have.
                                            self->refreshVocabulary();

                                            // The shown selection holds names, so it still carries the old
                                            // spelling. Re-read from the album rather than patched: on a merge
                                            // the surviving vocabulary row keeps its own spelling, which need not
                                            // be the one just typed, and the database is the only thing that
                                            // knows which. Whatever album is selected now is the right one to
                                            // read - a rename is vocabulary-wide, not album-scoped.
                                            if (self->m_albumId >= 0)
                                            {
                                                if (const auto album = database::theTrackLibrary.getAlbumManager().getAlbumById(self->m_albumId))
                                                {
                                                    self->m_selected = album->genres;
                                                }
                                            }
                                            self->updateHeaderText();
                                            self->repaint();

                                            spdlog::info("GenreCloud: renamed '{}' to '{}'{}.",
                                                         oldName.toStdString(),
                                                         newName.toStdString(),
                                                         merged ? " (merged into an existing genre)" : "");
                                        }));
        }

        void GenreCloudComponent::promptForNewGenre()
        {
            auto *window = new juce::AlertWindow("Add Genre", "Add a name to the genre vocabulary:", juce::AlertWindow::NoIcon);
            window->addTextEditor("genre", "", "Genre:");
            window->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey));
            window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

            // The modal outlives the click, so the component must not be captured raw.
            juce::Component::SafePointer<GenreCloudComponent> safeThis{this};
            window->enterModalState(true,
                                    juce::ModalCallbackFunction::create(
                                        [safeThis, window](int result)
                                        {
                                            std::unique_ptr<juce::AlertWindow> windowDeleter{window}; //-V824

                                            if (result != 1)
                                            {
                                                return;
                                            }

                                            const auto name{window->getTextEditorContents("genre").trim()};
                                            if (name.isEmpty())
                                            {
                                                return;
                                            }

                                            if (!database::theTrackLibrary.getAlbumManager().addGenre(name.toStdString()))
                                            {
                                                spdlog::error("GenreCloud: failed to add genre '{}'.", name.toStdString());
                                                return;
                                            }

                                            if (safeThis != nullptr)
                                            {
                                                safeThis->refreshVocabulary();
                                            }
                                        }),
                                    false);
        }

        void GenreCloudComponent::paint(juce::Graphics &g)
        {
            auto &lookAndFeel = getLookAndFeel();
            const auto accent = lookAndFeel.findColour(accentColourId);
            const auto foreground = lookAndFeel.findColour(mainForegroundColourId);

            g.fillAll(lookAndFeel.findColour(mainBackgroundColourId));

            g.setFont(headerFont());
            g.setColour(foreground);
            g.drawText(m_headerText,
                       juce::Rectangle<int>{kMargin, kMargin, getWidth() - (kMargin * 2), kHeaderHeight},
                       juce::Justification::centredLeft,
                       true);

            g.setFont(chipFont());
            for (size_t i = 0; i < m_chips.size(); ++i)
            {
                const auto &chip = m_chips[i];
                const int selectionIndex = selectionIndexOf(chip.name);
                const auto rect = chip.bounds.toFloat();

                juce::Colour textColour;
                if (selectionIndex == 0)
                {
                    g.setColour(accent);
                    g.fillRoundedRectangle(rect, 4.0f);
                    textColour = accent.contrasting(0.9f);
                }
                else if (selectionIndex > 0)
                {
                    g.setColour(accent.withAlpha(0.35f));
                    g.fillRoundedRectangle(rect, 4.0f);
                    textColour = foreground;
                }
                else
                {
                    textColour = tierColour(chip.tier);
                }

                if (static_cast<int>(i) == m_hoverIndex)
                {
                    g.setColour(accent.withAlpha(0.7f));
                    g.drawRoundedRectangle(rect.reduced(0.5f), 4.0f, 1.0f);
                }

                g.setColour(textColour);
                g.drawText(chip.name, chip.bounds, juce::Justification::centred, false);
            }

            const auto addRect = m_addChip.bounds.toFloat();
            g.setColour(lookAndFeel.findColour(disabledForegroundColourId));
            g.drawRoundedRectangle(addRect.reduced(0.5f), 4.0f, 1.0f);
            if (m_hoverIndex == static_cast<int>(m_chips.size()))
            {
                g.setColour(accent);
            }
            g.drawText(m_addChip.name, m_addChip.bounds, juce::Justification::centred, false);
        }

        void GenreCloudComponent::mouseDown(const juce::MouseEvent &event)
        {
            const int index = indexAt(event.getPosition());
            if (index < 0)
            {
                return;
            }

            if (index == static_cast<int>(m_chips.size()))
            {
                // Vocabulary management, not labelling - allowed with no track selected.
                promptForNewGenre();
                return;
            }

            if (event.mods.isRightButtonDown())
            {
                // Before the album guard below, unlike the labelling gestures. The menu also
                // offers renaming, which is vocabulary management and is allowed with nothing
                // selected - the same rule the "+ add genre" chip already follows.
                showChipMenu(index);
                return;
            }

            if (m_folderId < 0)
            {
                // No album to write to: ignore rather than show a selection that cannot be saved.
                return;
            }

            if (event.mods.isCommandDown())
            {
                toggleSecondaryGenre(index);
            }
            else
            {
                setOnlyGenre(index);
            }
        }

        void GenreCloudComponent::mouseMove(const juce::MouseEvent &event)
        {
            const int index = indexAt(event.getPosition());
            if (index != m_hoverIndex)
            {
                m_hoverIndex = index;
                repaint();
            }
        }

        void GenreCloudComponent::mouseExit(const juce::MouseEvent &)
        {
            if (m_hoverIndex != -1)
            {
                m_hoverIndex = -1;
                repaint();
            }
        }

    } // namespace ui
} // namespace jucyaudio
