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

#include <Tests/SchemaV12Fixture.h>
#include <Tests/SelfTests.h>

#include <Audio/ExportMixToMp3.h>
#include <Audio/ExportMixToWav.h>
#include <Audio/Includes/ActiveExportSettings.h>
#include <Audio/MixExporter.h>
#include <Audio/MixRecoveryM3U.h>
#include <Database/DatabaseBackupManager.h>
#include <Database/Includes/AlbumInfo.h>
#include <Database/Includes/IAlbumManager.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/MixRecoveryEntry.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/Scanners/Id3TagScanner.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/TrackLibrary.h>
#include <Database/TrackScanner.h>
#include <UI/CreateMixDialogComponent.h>

#include <Audio/Plugins/PluginChain.h>
#include <UI/CreateWorkingSetDialogComponent.h>
#include <UI/EditMixMetaDataDialog.h>
#include <UI/ExportMixDialog.h>
#include <UI/LibraryRootsComponent.h>
#include <UI/MarkerEditDialog.h>
#include <UI/Settings.h>
#include <UI/SingletonDialog.h>
#include <UI/TimelineComponent.h>
#include <Utils/AssortedUtils.h>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

#include <lame.h>

// TagLib, for the compressed-frame fixture: read the way the scanner reads, then ask the tag what it
// parsed. tzlib.h is not part of TagLib's public API, but the build already puts the toolkit
// directory on the include path for exactly this kind of internal header, and it is the only place
// TagLib says whether it was built with a zlib at all.
#include "id3v2frame.h"
#include "id3v2tag.h"
#include "mpegfile.h"
#include "tzlib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        /// @brief The names on the faults PluginChain has recorded but not yet reported.
        ///
        /// What reportPendingFaults puts in the log, before it puts it there. Reading it is the only
        /// way to check *which* plugin a fault is attributed to, which is the half of the recording
        /// that a count cannot see.
        struct PluginChainTestAccess
        {
            static std::vector<std::string> pendingFaultNames(const PluginChain &chain)
            {
                std::vector<std::string> names;
                for (size_t i = 0; i < chain.m_faultCount; ++i)
                {
                    names.emplace_back(chain.m_faults[i].name.data());
                }
                return names;
            }

            static size_t droppedFaults(const PluginChain &chain)
            {
                return chain.m_faultsDropped.load(std::memory_order_relaxed);
            }

            /// @brief Takes the guard, so the next record or drain finds it busy.
            ///
            /// This is how the failed-acquisition branches are reached without a second thread: they
            /// do not care who holds the flag, only that somebody does.
            static bool holdGuard(PluginChain &chain)
            {
                return !chain.m_faultGuard.test_and_set(std::memory_order_acquire);
            }

            static void releaseGuard(PluginChain &chain)
            {
                chain.m_faultGuard.clear(std::memory_order_release);
            }
        };
    } // namespace audio

    namespace ui
    {
        /// @brief A plugin that throws out of processBlock, for the audio-thread fault checks.
        ///
        /// Everything but processBlock is the minimum juce::AudioPluginInstance demands. The chain
        /// only ever calls processBlock, isSuspended, suspendProcessing and getName on it.
        class ThrowingPlugin final : public juce::AudioPluginInstance
        {
        public:
            explicit ThrowingPlugin(juce::String name, bool throwKnown)
                : m_name{std::move(name)},
                  m_throwKnown{throwKnown}
            {
            }

            int blocksProcessed() const noexcept
            {
                return m_blocksProcessed;
            }

            const juce::String getName() const override
            {
                return m_name;
            }

            void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override
            {
                ++m_blocksProcessed;
                if (m_throwKnown)
                {
                    throw std::runtime_error("the plugin said no");
                }
                throw 42; // not derived from std::exception, so the other handler takes it
            }

            void prepareToPlay(double, int) override
            {
            }
            void releaseResources() override
            {
            }
            double getTailLengthSeconds() const override
            {
                return 0.0;
            }
            bool acceptsMidi() const override
            {
                return false;
            }
            bool producesMidi() const override
            {
                return false;
            }
            juce::AudioProcessorEditor *createEditor() override
            {
                return nullptr;
            }
            bool hasEditor() const override
            {
                return false;
            }
            int getNumPrograms() override
            {
                return 1;
            }
            int getCurrentProgram() override
            {
                return 0;
            }
            void setCurrentProgram(int) override
            {
            }
            const juce::String getProgramName(int) override
            {
                return {};
            }
            void changeProgramName(int, const juce::String &) override
            {
            }
            void getStateInformation(juce::MemoryBlock &) override
            {
            }
            void setStateInformation(const void *, int) override
            {
            }
            void fillInPluginDescription(juce::PluginDescription &) const override
            {
            }

        private:
            juce::String m_name;
            bool m_throwKnown;
            int m_blocksProcessed{0};
        };

        /// @brief The registry behind SingletonComponentDialog, read without validating it.
        ///
        /// getValidDialogWindow erases a dead entry as a side effect of being asked, so through it an
        /// eager unregister and a lazy clean-up are the same answer. The check this serves is about
        /// *when* the entry goes, so it has to see the map as it is.
        struct SingletonComponentDialogTestAccess
        {
            static bool registered(const juce::String &dialogId)
            {
                const juce::ScopedLock lock{SingletonComponentDialog::s_dialogLock};
                return SingletonComponentDialog::s_openDialogs.find(dialogId) != SingletonComponentDialog::s_openDialogs.end();
            }

            static juce::DialogWindow *window(const juce::String &dialogId)
            {
                const juce::ScopedLock lock{SingletonComponentDialog::s_dialogLock};
                const auto it = SingletonComponentDialog::s_openDialogs.find(dialogId);
                return it != SingletonComponentDialog::s_openDialogs.end() ? it->second.getComponent() : nullptr;
            }
        };

        /// @brief What the export dialog checks below reach through.
        ///
        /// Everything here is a getter or a simulated user action; no test logic lives in it.
        struct ExportMixDialogTestAccess
        {
            static juce::TextEditor &mixName(ExportMixDialog &d)
            {
                return d.m_mixNameEditor;
            }
            static juce::TextEditor &title(ExportMixDialog &d)
            {
                return d.m_trackTitleEditor;
            }
            static juce::TextEditor &trackNumber(ExportMixDialog &d)
            {
                return d.m_trackNumberEditor;
            }
            static juce::File outputFile(const ExportMixDialog &d)
            {
                return d.m_filenameComponent != nullptr ? d.m_filenameComponent->getCurrentFile() : juce::File{};
            }
            static const database::MixInfo &mixInfo(const ExportMixDialog &d)
            {
                return d.m_mixInfo;
            }
            static bool commit(ExportMixDialog &d)
            {
                std::string ignored;
                return d.commitMixNameIfChanged(ignored);
            }

            /// @brief The same, keeping what it would have told the user.
            static bool commit(ExportMixDialog &d, std::string &errorOut)
            {
                return d.commitMixNameIfChanged(errorOut);
            }

            // The three static helpers are private again now that this exists; these keep them
            // reachable without widening the dialog's own interface for a test.
            static juce::String leadingTrackNumber(const juce::String &name)
            {
                return ExportMixDialog::leadingTrackNumber(name);
            }
            static juce::String effectiveMixName(const juce::String &text)
            {
                return ExportMixDialog::effectiveMixName(text);
            }

            /// @brief The Export button, through the code it actually runs.
            static void pressExport(ExportMixDialog &d)
            {
                d.handleExport();
            }

            /// @brief The Cancel button, likewise.
            static void pressCancel(ExportMixDialog &d)
            {
                d.handleCancel();
            }

            static void setSchedule(ExportMixDialog &d, bool scheduled)
            {
                d.m_scheduleCheckbox.setToggleState(scheduled, juce::dontSendNotification);
            }

            /// @brief Picks the first export folder, which handleExport refuses to proceed without.
            static bool selectAnExportFolder(ExportMixDialog &d)
            {
                if (d.m_exportFolderCombo.getNumItems() == 0)
                {
                    return false;
                }
                d.m_exportFolderCombo.setSelectedItemIndex(0, juce::sendNotification);
                return d.m_exportFolderCombo.getSelectedId() != 0;
            }

            static void setOutputFile(ExportMixDialog &d, const juce::File &file)
            {
                d.m_filenameComponent->setCurrentFile(file, false, juce::dontSendNotification);
            }

            /// @brief What picking a file in the component does, including the notification.
            ///
            /// The one above deliberately does not notify - it is for arranging a starting state. This
            /// one is the user choosing, which is what makes the dialog stop renaming the file.
            static void chooseOutputFile(ExportMixDialog &d, const juce::File &file)
            {
                d.m_filenameComponent->setCurrentFile(file, false, juce::dontSendNotification);
                d.filenameComponentChanged(d.m_filenameComponent.get());
            }

            /// @brief What JUCE does when the user types, minus the wait.
            ///
            /// TextEditor posts textEditorTextChanged with postCommandMessage rather than calling it,
            /// so this sets the text without a notification and then makes the call JUCE would have
            /// delivered. Same order, same argument, no message loop.
            static void type(ExportMixDialog &d, juce::TextEditor &editor, const juce::String &text)
            {
                editor.setText(text, juce::dontSendNotification);
                d.textEditorTextChanged(editor);
            }
        };
    } // namespace ui

    namespace tests
    {
        using namespace database;
        using json = nlohmann::json;

        namespace
        {
            constexpr const char *kAlbumFolder = "AlbumOne";
            constexpr int kTrackCount = 4;
            // Whole seconds, and at least one. TagLib reports length through lengthInSeconds(), so
            // anything under a second scans as a duration of zero - which is what every fixture used
            // to be. Nothing noticed until a test needed the scanned value rather than this constant.
            constexpr int kFixtureDurationMs = 2000;
            constexpr const char *kMixName = "SelfTest Mix";
            constexpr const char *kRootEnvVar = "JUCYAUDIO_SELFTEST_ROOT";
            constexpr const char *kSentinelFile = ".jucyaudio-selftest";
            constexpr const char *kWorkingSetName = "SelfTest Working Set";

            /// @brief Collects check results so a failure does not stop the run - later checks usually
            /// explain the earlier one, and a single pass should say everything it can.
            class Report final
            {
            public:
                void check(bool passed, const std::string &what)
                {
                    m_lines.push_back(std::format("{}  {}", passed ? "PASS" : "FAIL", what));
                    if (passed)
                    {
                        spdlog::info("[SelfTest] PASS  {}", what);
                    }
                    else
                    {
                        ++m_failures;
                        spdlog::error("[SelfTest] FAIL  {}", what);
                    }
                }

                void note(const std::string &text)
                {
                    m_lines.push_back(std::format("....  {}", text));
                    spdlog::info("[SelfTest] {}", text);
                }

                /// @brief Record why the run stopped early. Used only where continuing would test
                /// nothing - the caller returns straight after.
                void abort(const std::string &why)
                {
                    ++m_failures;
                    m_lines.push_back(std::format("STOP  {}", why));
                    spdlog::error("[SelfTest] STOP  {}", why);
                }

                int failures() const
                {
                    return m_failures;
                }

                const std::vector<std::string> &lines() const
                {
                    return m_lines;
                }

            private:
                std::vector<std::string> m_lines;
                int m_failures{0};
            };

            /// @brief Writes a small but structurally valid 16-bit mono WAV of silence.
            ///
            /// Generated rather than copied so the test carries its own fixtures and depends on nothing
            /// in the user's library. WAV rather than MP3 because a correct file can be written by hand
            /// in a few lines - the scanner accepts *.wav, and TagLib reads it without complaint.
            /// @brief Writes a mono 16-bit 44.1 kHz WAV fixture.
            /// @param amplitude Zero for silence, which is what most fixtures want - they are scanned,
            ///        not listened to. Non-zero writes a square wave, for the one check that reads an
            ///        exported file back and asks where a track's audio starts.
            bool writeSilentWav(const std::filesystem::path &path, uint32_t sampleCount, int16_t amplitude = 0)
            {
                const uint32_t sampleRate = 44100;
                const uint16_t channels = 1;
                const uint16_t bitsPerSample = 16;
                const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
                const uint32_t dataBytes = sampleCount * channels * (bitsPerSample / 8);

                std::ofstream out{path, std::ios::binary};
                if (!out)
                {
                    return false;
                }

                const auto u32 = [&out](uint32_t v)
                {
                    // Written byte-by-byte rather than by dumping the struct: WAV is little-endian by
                    // definition, and this keeps that true regardless of the host.
                    const unsigned char bytes[4]{static_cast<unsigned char>(v & 0xFF),
                        static_cast<unsigned char>((v >> 8) & 0xFF),
                        static_cast<unsigned char>((v >> 16) & 0xFF),
                        static_cast<unsigned char>((v >> 24) & 0xFF)};
                    out.write(reinterpret_cast<const char *>(bytes), 4);
                };
                const auto u16 = [&out](uint16_t v)
                {
                    const unsigned char bytes[2]{static_cast<unsigned char>(v & 0xFF), static_cast<unsigned char>((v >> 8) & 0xFF)};
                    out.write(reinterpret_cast<const char *>(bytes), 2);
                };

                out.write("RIFF", 4);
                u32(36 + dataBytes);
                out.write("WAVE", 4);
                out.write("fmt ", 4);
                u32(16);       // PCM chunk size
                u16(1);        // PCM
                u16(channels);
                u32(sampleRate);
                u32(byteRate);
                u16(static_cast<uint16_t>(channels * (bitsPerSample / 8))); // block align
                u16(bitsPerSample);
                out.write("data", 4);
                u32(dataBytes);

                if (amplitude == 0)
                {
                    const std::vector<char> silence(dataBytes, 0);
                    out.write(silence.data(), static_cast<std::streamsize>(silence.size()));
                }
                else
                {
                    // A square wave at 441 Hz - a hundred samples up, a hundred down. Square rather
                    // than sine because the only thing read back is "is this sample loud", and a
                    // square wave is loud from its very first sample, so an onset found in the
                    // exported file is the track's start and not the first audible part of a ramp.
                    for (uint32_t i = 0; i < sampleCount; ++i)
                    {
                        const int16_t sample = ((i / 50) % 2 == 0) ? amplitude : static_cast<int16_t>(-amplitude);
                        u16(static_cast<uint16_t>(sample));
                    }
                }

                return out.good();
            }

            /// @brief Every track under a folder, keyed by filename, so checks can be written by name.
            std::map<std::string, TrackInfo> tracksUnder(ITrackDatabase &db, FolderId folderId)
            {
                TrackQueryArgs args{};
                args.folderIds = {folderId};
                args.recursive = true;
                args.usePaging = false;

                std::map<std::string, TrackInfo> byName;
                for (auto &track : db.getTracks(args))
                {
                    byName[track.filename] = track;
                }
                return byName;
            }

            /// @brief Every track row under a folder, unkeyed.
            ///
            /// tracksUnder keys by filename, which is right for comparing identity across a scan and
            /// wrong for counting: two files of the same name in different folders collapse into one
            /// entry, so a check that a duplicate row was inserted would read as though it had not been.
            std::vector<TrackInfo> trackRowsUnder(ITrackDatabase &db, FolderId folderId)
            {
                TrackQueryArgs args{};
                args.folderIds = {folderId};
                args.recursive = true;
                args.usePaging = false;
                return db.getTracks(args);
            }

            int countMissing(const std::vector<TrackInfo> &tracks)
            {
                int n = 0;
                for (const auto &track : tracks)
                {
                    n += track.is_missing ? 1 : 0;
                }
                return n;
            }

            int countMissing(const std::map<std::string, TrackInfo> &tracks)
            {
                int n = 0;
                for (const auto &entry : tracks)
                {
                    n += entry.second.is_missing ? 1 : 0;
                }
                return n;
            }

            std::map<std::string, TrackId> idsOf(const std::map<std::string, TrackInfo> &tracks)
            {
                std::map<std::string, TrackId> ids;
                for (const auto &entry : tracks)
                {
                    ids[entry.first] = entry.second.trackId;
                }
                return ids;
            }

            std::map<std::string, TrackId> workingSetMembers(ITrackDatabase &db, WorkingSetId workingSetId)
            {
                TrackQueryArgs args{};
                args.workingSetId = workingSetId;
                args.usePaging = false;

                std::map<std::string, TrackId> ids;
                for (const auto &track : db.getTracks(args))
                {
                    ids[track.filename] = track.trackId;
                }
                return ids;
            }

            /// @brief "0, 2, 3" - so a failing position check says what it actually found.
            std::string idsToText(const std::vector<int> &values)
            {
                std::string text;
                for (const auto value : values)
                {
                    text += (text.empty() ? "" : ", ") + std::to_string(value);
                }
                return text;
            }

            /// @brief How many playlist temporaries are lying about in a directory.
            ///
            /// The temporary is named uniquely per attempt, so no single path can be tested for. A
            /// leftover is worse than untidy: after the link it is a second name for the published
            /// playlist, and opening it for writing truncates what was published.
            int strayTempCount(const std::filesystem::path &directory)
            {
                // Advanced by hand for the same reason the backfill does it: a range-for calls the
                // throwing operator++, and a construction error would otherwise report zero strays -
                // a clean answer from a check that never ran.
                int strays = 0;
                std::error_code listEc;
                std::filesystem::directory_iterator entry{directory, listEc};
                const std::filesystem::directory_iterator end;

                while (!listEc && entry != end)
                {
                    if (entry->path().extension() == ".m3utmp")
                    {
                        ++strays;
                    }
                    entry.increment(listEc);
                }

                // A failure to look is not the same as nothing being there, and this feeds an assertion
                // that something is absent. Reported as one stray so that check fails rather than passes.
                return listEc ? strays + 1 : strays;
            }

            /// @brief Are these two recovery records identical in every field?
            ///
            /// Row counts are not enough: a rewrite that happens to produce the same number of rows would
            /// pass a size check while having replaced everything. capturedAt is the giveaway - it is
            /// stamped fresh on every capture, so any rewrite at all changes it.
            bool sameRecord(const std::vector<MixRecoveryEntry> &a, const std::vector<MixRecoveryEntry> &b)
            {
                if (a.size() != b.size())
                {
                    return false;
                }
                for (size_t i = 0; i < a.size(); ++i)
                {
                    if (a[i].mixId != b[i].mixId || a[i].orderInMix != b[i].orderInMix || a[i].capturedAt != b[i].capturedAt ||
                        a[i].mixName != b[i].mixName || a[i].mixTotalDuration != b[i].mixTotalDuration ||
                        a[i].trackId != b[i].trackId || a[i].artistName != b[i].artistName ||
                        a[i].albumTitle != b[i].albumTitle || a[i].title != b[i].title || a[i].filename != b[i].filename ||
                        a[i].folderPath != b[i].folderPath || a[i].duration != b[i].duration || a[i].filesizeBytes != b[i].filesizeBytes ||
                        a[i].bpm != b[i].bpm || a[i].mixData != b[i].mixData || a[i].isComplete != b[i].isComplete ||
                        a[i].sourceOrderInMix != b[i].sourceOrderInMix)
                    {
                        return false;
                    }
                }
                return true;
            }

            /// @brief Writes a JSON field the MixTrack parser knows nothing about into one mix_data row.
            ///
            /// The whole point of storing mix_data verbatim is that a field nobody understands today
            /// still survives. Nothing reachable through the public interfaces can produce such a field -
            /// mix_data only ever gets written by createOrUpdateMix, which serialises with the same
            /// to_json a re-serialising implementation would use, so both would emit identical bytes and
            /// no assertion could tell them apart.
            ///
            /// So the fixture is staged over the test's own connection to the scratch database, opened
            /// and closed here and used for nothing else. This touches no production code, and the
            /// assertion that follows goes entirely through public interfaces.
            ///
            /// @return The exact text written, to compare the recovery row against, or empty on failure.
            std::string injectUnknownFieldIntoMixData(const std::filesystem::path &databasePath, MixId mixId, int orderInMix)
            {
                // Scoped: the destructor closes it, so the fixture connection is gone before anything
                // else in the test runs.
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return {};
                }

                std::string original;
                {
                    SqliteStatement read{fixtureDb};
                    if (!read.query(
                            [&original, &read]() -> bool
                            {
                                original = read.getText(0);
                                return true;
                            },
                            "SELECT mix_data FROM MixTracks WHERE mix_id=? AND order_in_mix=?",
                            mixId,
                            orderInMix))
                    {
                        return {};
                    }
                }

                if (original.empty())
                {
                    return {};
                }

                std::string doctored;
                try
                {
                    auto parsed = json::parse(original);
                    parsed["selfTestUnknownField"] = "kept verbatim or not at all";
                    doctored = parsed.dump();
                }
                catch (const std::exception &)
                {
                    return {};
                }

                {
                    SqliteStatement write{fixtureDb, "UPDATE MixTracks SET mix_data=? WHERE mix_id=? AND order_in_mix=?"};
                    if (!write.isValid() || !write.addParam(doctored) || !write.addParam(mixId) || !write.addParam(orderInMix) || !write.execute())
                    {
                        return {};
                    }
                }

                return doctored;
            }

            /// @brief Puts unparseable text into one row's mix_data.
            ///
            /// The row stays, the mix keeps its shape, and only that one row becomes unreadable - which
            /// is the shape of real corruption and the one case a partial read turns into data loss.
            bool corruptMixData(const std::filesystem::path &databasePath, MixId mixId, int orderInMix)
            {
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return false;
                }

                SqliteStatement stmt{fixtureDb, "UPDATE MixTracks SET mix_data = ? WHERE mix_id = ? AND order_in_mix = ?"};
                if (!stmt.isValid() || !stmt.addParam(std::string{"{not json at all"}) || !stmt.addParam(mixId) || !stmt.addParam(orderInMix) ||
                    !stmt.execute())
                {
                    return false;
                }

                // execute() succeeds whether or not any row matched, and a fixture that quietly
                // changes nothing is worse than one that fails: every check built on it then passes
                // by describing a mix that was never corrupted. This one did exactly that.
                return fixtureDb.getChangesCount() == 1;
            }

            /// @brief Moves one mix row on top of another, so two share a position.
            ///
            /// MixTracks has only an index on (mix_id, order_in_mix), not a unique constraint, so this
            /// is a state the table genuinely allows and two mixes in the real library are in. Nothing
            /// reachable through the interfaces produces it - removeTracksFromMix renumbers - which is
            /// why it went unnoticed until a capture tried to copy those positions into MixRecovery,
            /// where they are the primary key.
            bool duplicateOrderInMix(const std::filesystem::path &databasePath, MixId mixId, int fromOrder, int toOrder)
            {
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return false;
                }

                SqliteStatement stmt{fixtureDb, "UPDATE MixTracks SET order_in_mix = ? WHERE mix_id = ? AND order_in_mix = ?"};
                if (!stmt.isValid() || !stmt.addParam(toOrder) || !stmt.addParam(mixId) || !stmt.addParam(fromOrder) || !stmt.execute())
                {
                    return false;
                }
                return fixtureDb.getChangesCount() == 1;
            }

            /// @brief Removes a mix's recovery record, so the mix looks like one that never had one.
            ///
            /// Over the test's own connection: nothing in the interface deletes a record on its own,
            /// which is deliberate - a record is meant to outlive the mix data it describes.
            bool clearRecoveryData(const std::filesystem::path &databasePath, MixId mixId)
            {
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return false;
                }

                SqliteStatement stmt{fixtureDb, "DELETE FROM MixRecovery WHERE mix_id = ?"};
                return stmt.isValid() && stmt.addParam(mixId) && stmt.execute();
            }

            /// @brief Writes deliberately wrong summary columns for one mix.
            ///
            /// Over the test's own connection because nothing reachable through the public interfaces
            /// can do this any more: the columns are derived on every write and there is no setter. That
            /// is the fix, and it is also why the repair that corrects such a row cannot be exercised
            /// without reaching past the interfaces to create one.
            bool setMixSummary(const std::filesystem::path &databasePath, MixId mixId, int64_t trackCount, int64_t totalLength)
            {
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return false;
                }

                SqliteStatement stmt{fixtureDb, "UPDATE Mixes SET track_count = ?, total_length = ? WHERE mix_id = ?"};
                return stmt.isValid() && stmt.addParam(trackCount) && stmt.addParam(totalLength) && stmt.addParam(mixId) && stmt.execute();
            }

            /// @brief Clears total_duration on a mix's recovery rows, as a v27 record would have it.
            ///
            /// Staged over the test's own connection for the same reason the unknown JSON field is:
            /// nothing reachable through the public interfaces can produce a NULL there any more, and the
            /// case still has to be exercised because the database on disk is full of rows that will.
            bool setRecoveryDurationToNull(const std::filesystem::path &databasePath, MixId mixId)
            {
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return false;
                }

                SqliteStatement stmt{fixtureDb, "UPDATE MixRecovery SET total_duration = NULL WHERE mix_id = ?"};
                return stmt.isValid() && stmt.addParam(mixId) && stmt.execute();
            }

            /// @brief Runs one scan to completion. Synchronous - there is no task thread here.
            bool runScan(std::vector<FolderId> folderIds,
                bool removeMissingFiles,
                Report &report,
                const std::string &label,
                bool forceRescanAllFiles = false)
            {
                report.note(std::format("scan: {}", label));
                bool scanReportedSuccess = false;
                theTrackLibrary.scanLibrary(
                    folderIds,
                    forceRescanAllFiles,
                    removeMissingFiles,
                    nullptr, // no progress reporting; nothing is watching
                    [&scanReportedSuccess](bool success, const std::string &message)
                    {
                        scanReportedSuccess = success;
                        spdlog::info("[SelfTest] scan finished: success={}, message='{}'", success, message);
                    },
                    nullptr);
                return scanReportedSuccess;
            }

            /// @brief Reads a database's structure as comparable facts, one sorted list per object.
            ///
            /// Text is the wrong thing to compare two schemas by. SQLite stores a CREATE statement
            /// exactly as it was written, and ALTER TABLE ADD COLUMN appends to that stored text - so a
            /// table the ladder built carries its old wording plus a tail of appended columns, while the
            /// same table in a new database carries whatever initialSqlStatements says today. Those two
            /// never match as strings, and the difference means nothing. What has to match is the
            /// structure: the same columns with the same types, nullability, defaults and primary key
            /// membership, the same foreign keys, and the same indexes over the same columns.
            ///
            /// Column ordinal position is deliberately not compared, and no longer needs to be. It used
            /// to matter for Tracks and MixTracks, which were read with `SELECT *` and decoded by a
            /// running index, so a column one path appended and the other declared earlier would land
            /// every following value in the wrong field. Those queries now name their columns -
            /// trackColumnsForDecoding and mixTrackColumnsForDecoding, each sitting beside the decoder it
            /// serves - so the order a table declares its columns in is no longer a behaviour anywhere.
            ///
            /// Which is just as well, because two tables genuinely disagree about it: the ladder appends
            /// Albums.bitrate and MixRecovery.total_duration where the fresh schema declares them
            /// mid-table. Neither reordering is safe to make - changing the fresh schema would hand every
            /// database already stamped at the latest version a different order from a new one, and
            /// changing the ladder means rebuilding two tables to renumber columns nobody addresses by
            /// number. Removing the dependency was the way out, so the difference is now genuinely
            /// without consequence rather than merely unnoticed.
            ///
            /// The facts are sorted before comparing, so the report reads in a stable order.
            ///
            /// Indexes come from PRAGMA index_xinfo rather than index_info, for the collation: Tags.name
            /// is UNIQUE COLLATE NOCASE, and an index that lost the NOCASE would compare equal on its
            /// columns alone while letting case-variant duplicates in.
            ///
            /// A virtual table's definition lives in its SQL and nowhere else - PRAGMA table_info shows
            /// the columns of TracksSearchFTS but not its tokenizer, its content table or its content
            /// rowid - so for those the normalized SQL is a fact of its own.
            ///
            /// Triggers and views have no pragma to interrogate either, so they are compared as SQL with
            /// whitespace collapsed and IF NOT EXISTS removed. The IF NOT EXISTS matters: the v12 rung
            /// wrote these objects without it and convergenceSqlStatements writes them with it, and that
            /// difference in the stored text says nothing about behaviour.
            ///
            /// @return Each object, keyed by kind and name, to its sorted facts. Empty on failure.
            std::map<std::string, std::vector<std::string>> readSchemaStructure(const std::filesystem::path &path, Report &report)
            {
                std::map<std::string, std::vector<std::string>> structure;

                SqliteDatabase db;
                if (!db.open(pathToString(path)))
                {
                    report.abort(std::format("Could not open {} to read its structure.", pathToString(path)));
                    return {};
                }

                // Identifiers come out of sqlite_master, so they are this project's own names rather
                // than anything a user typed - but a pragma takes no parameters, so they have to be
                // pasted into the SQL, and quoting them properly costs one line.
                const auto quoted = [](const std::string &name)
                {
                    std::string out{"\""};
                    for (const auto ch : name)
                    {
                        if (ch == '"')
                        {
                            out.push_back('"');
                        }
                        out.push_back(ch);
                    }
                    out.push_back('"');
                    return out;
                };

                // Whitespace outside a literal is formatting; whitespace inside one is data. These
                // triggers build search_content by concatenating with ' ' separators, so collapsing
                // runs of spaces indiscriminately would make a trigger that joins fields with two
                // spaces compare equal to one that joins them with one - a change to what gets stored,
                // reported as no change at all.
                const auto collapsed = [](const std::string &text)
                {
                    std::string out;
                    bool pendingSpace = false;
                    char quote = '\0';
                    for (const auto ch : text)
                    {
                        if (quote != '\0')
                        {
                            // Inside a literal or a quoted identifier: copied through exactly. A
                            // doubled quote closes and immediately reopens, which changes nothing here
                            // because either way every character is kept.
                            out.push_back(ch);
                            if (ch == quote)
                            {
                                quote = '\0';
                            }
                            continue;
                        }

                        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
                        {
                            pendingSpace = true;
                            continue;
                        }
                        if (pendingSpace && !out.empty())
                        {
                            out.push_back(' ');
                        }
                        pendingSpace = false;
                        out.push_back(ch);

                        if (ch == '\'' || ch == '"' || ch == '`')
                        {
                            quote = ch;
                        }
                    }

                    // "IF NOT EXISTS" is not part of what an object is, and the two creation paths
                    // disagree about writing it - so it goes, rather than being reported as a
                    // difference. Only where it can be the real thing though: ahead of the first
                    // literal, which is where a CREATE puts it. A trigger body that happens to contain
                    // the words inside a string keeps them.
                    constexpr std::string_view noise{"IF NOT EXISTS "};
                    const auto firstQuote = out.find_first_of("'\"`");
                    if (const auto at = out.find(noise); at != std::string::npos && (firstQuote == std::string::npos || at < firstQuote))
                    {
                        out.erase(at, noise.size());
                    }
                    return out;
                };

                // The ANALYZE tables are left out: they describe what each database happened to be
                // asked, not how it was built.
                std::vector<std::string> tables;
                std::map<std::string, std::string> virtualTableSql;
                {
                    SqliteStatement stmt{db,
                        "SELECT type, name, COALESCE(sql, '') FROM sqlite_master "
                        "WHERE name NOT LIKE 'sqlite_stat%' ORDER BY type, name;"};
                    if (!stmt.isValid())
                    {
                        report.abort(std::format("Could not read sqlite_master of {}.", pathToString(path)));
                        return {};
                    }

                    while (stmt.getNextResult())
                    {
                        const auto kind{stmt.getText(0)};
                        const auto name{stmt.getText(1)};
                        if (kind == "table")
                        {
                            tables.push_back(name);
                            structure[std::format("table {}", name)] = {};

                            // Its options - tokenizer, content table, content rowid - are in the SQL and
                            // in no pragma, so they are kept and compared.
                            const auto sql{collapsed(stmt.getText(2))};
                            if (sql.starts_with("CREATE VIRTUAL TABLE"))
                            {
                                virtualTableSql[name] = sql;
                            }
                        }
                        else if (kind == "trigger" || kind == "view")
                        {
                            structure[std::format("{} {}", kind, name)] = {collapsed(stmt.getText(2))};
                        }
                        // Indexes are picked up per table below, where their uniqueness is visible.
                    }

                    if (stmt.hasError())
                    {
                        report.abort(std::format("The read of sqlite_master of {} stopped early.", pathToString(path)));
                        return {};
                    }
                }

                for (const auto &table : tables)
                {
                    auto &facts = structure[std::format("table {}", table)];

                    {
                        SqliteStatement stmt{db, std::format("PRAGMA table_info({});", quoted(table))};
                        while (stmt.getNextResult())
                        {
                            facts.push_back(std::format("column {} type={} notnull={} default={} pk={}",
                                stmt.getText(1),
                                stmt.getText(2),
                                stmt.getInt32(3),
                                stmt.isNull(4) ? std::string{"<none>"} : stmt.getText(4),
                                stmt.getInt32(5)));
                        }
                    }

                    if (const auto it = virtualTableSql.find(table); it != virtualTableSql.end())
                    {
                        facts.push_back("definition " + it->second);
                    }

                    {
                        SqliteStatement stmt{db, std::format("PRAGMA foreign_key_list({});", quoted(table))};
                        while (stmt.getNextResult())
                        {
                            facts.push_back(std::format("foreign-key {} -> {}({}) on_update={} on_delete={}",
                                stmt.getText(3),
                                stmt.getText(2),
                                stmt.isNull(4) ? std::string{"<primary key>"} : stmt.getText(4),
                                stmt.getText(5),
                                stmt.getText(6)));
                        }
                    }

                    // Collected before they are interrogated: one statement at a time is easier to
                    // follow than a nested pair, and index_info needs the name index_list just gave.
                    std::vector<std::pair<std::string, std::string>> indexes;
                    {
                        SqliteStatement stmt{db, std::format("PRAGMA index_list({});", quoted(table))};
                        while (stmt.getNextResult())
                        {
                            indexes.emplace_back(stmt.getText(1),
                                std::format("unique={} origin={} partial={}", stmt.getInt32(2), stmt.getText(3), stmt.getInt32(4)));
                        }
                    }

                    for (const auto &[name, properties] : indexes)
                    {
                        auto &indexFacts = structure[std::format("index {}", name)];
                        indexFacts.push_back(std::format("on {}", table));
                        indexFacts.push_back(properties);

                        // Position matters for an index, so the column list is one fact in order rather
                        // than one fact per column: an index on (a, b) is not an index on (b, a). Each
                        // column carries its collation and direction, which is why this is index_xinfo -
                        // index_info does not report the collation, and an index that quietly lost a
                        // COLLATE NOCASE stops refusing the duplicates it was created to refuse.
                        std::string columns;
                        SqliteStatement stmt{db, std::format("PRAGMA index_xinfo({});", quoted(name))};
                        while (stmt.getNextResult())
                        {
                            // key=0 rows are the rowid SQLite appends to every index; they say nothing
                            // about how the index was declared.
                            if (stmt.getInt32(5) == 0)
                            {
                                continue;
                            }
                            if (!columns.empty())
                            {
                                columns.append(", ");
                            }
                            columns.append(stmt.isNull(2) ? std::string{"<expression>"} : stmt.getText(2));
                            columns.append(std::format(" collate={} desc={}", stmt.getText(4), stmt.getInt32(3)));
                        }
                        indexFacts.push_back(std::format("columns ({})", columns));
                    }
                }

                for (auto &[name, facts] : structure)
                {
                    std::ranges::sort(facts);
                }
                return structure;
            }

            /// @brief Writes through to a file for a while, then refuses everything.
            ///
            /// The header and the first blocks land, so the render is genuinely under way before it
            /// fails - a stream that refused from the first byte would fail createWriterFor instead and
            /// test the setup path that is already covered. What it accepted and how often it refused
            /// are recorded outside it, because the writer takes ownership of the stream and destroys
            /// it before the test can ask.
            struct WriteRefusalLog
            {
                int64_t bytesAccepted{0};
                int refusals{0};
                // Only the MP3 exporter seeks, and only to reach the LAME info frame, so this doubles
                // as "the render finished and finalisation began".
                int seeks{0};
            };

            class RefusingOutputStream final : public juce::OutputStream
            {
            public:
                RefusingOutputStream(std::unique_ptr<juce::FileOutputStream> inner, int64_t refuseAfterBytes, WriteRefusalLog &log)
                    : m_inner{std::move(inner)},
                      m_refuseAfterBytes{refuseAfterBytes},
                      m_log{log}
                {
                }

                void flush() override
                {
                    m_inner->flush();
                }

                bool setPosition(juce::int64 newPosition) override
                {
                    // Delegated rather than refused: destroying a WavAudioFormatWriter seeks back to
                    // patch the RIFF header, and a seek that failed would be a different fault from the
                    // one being injected.
                    return m_inner->setPosition(newPosition);
                }

                juce::int64 getPosition() override
                {
                    return m_inner->getPosition();
                }

                bool write(const void *data, size_t numBytes) override
                {
                    if (m_log.bytesAccepted >= m_refuseAfterBytes)
                    {
                        ++m_log.refusals;
                        return false;
                    }
                    if (!m_inner->write(data, numBytes))
                    {
                        return false;
                    }
                    m_log.bytesAccepted += static_cast<int64_t>(numBytes);
                    return true;
                }

            private:
                std::unique_ptr<juce::FileOutputStream> m_inner;
                const int64_t m_refuseAfterBytes;
                WriteRefusalLog &m_log;
            };

            /// @brief The real WAV export, writing to a stream that starts refusing mid-render.
            ///
            /// Only the writer's stream is substituted. onRunMixingLoop, releaseOutput and run() are
            /// the shipping implementations, so what this exercises is their handling of a write that
            /// comes back false.
            class WriteRefusingWavExport final : public audio::ExportWavMixImplementation
            {
            public:
                WriteRefusingWavExport(MixId mixId, const audio::ActiveExportSettings &settings, int64_t refuseAfterBytes, WriteRefusalLog &log)
                    : audio::ExportWavMixImplementation{mixId, settings, nullptr},
                      m_refuseAfterBytes{refuseAfterBytes},
                      m_log{log}
                {
                }

            protected:
                bool onSetupAudioFormatManagerAndWriter() override
                {
                    // Registered for reading the input tracks, exactly as the shipping override does.
                    m_formatManager.registerBasicFormats();

                    juce::File partial{juce::String{pathToString(renderTargetPath())}};
                    if (partial.existsAsFile())
                    {
                        partial.deleteFile();
                    }

                    std::unique_ptr<juce::FileOutputStream> file{partial.createOutputStream()};
                    if (!file)
                    {
                        return false;
                    }

                    const auto options = juce::AudioFormatWriterOptions{}
                                             .withSampleRate(outputSampleRate())
                                             .withNumChannels(static_cast<int>(outputNumChannels()))
                                             .withBitsPerSample(static_cast<int>(outputBitDepth()))
                                             .withQualityOptionIndex(0);

                    std::unique_ptr<juce::OutputStream> stream{std::make_unique<RefusingOutputStream>(std::move(file), m_refuseAfterBytes, m_log)};
                    juce::WavAudioFormat wavFormat;
                    m_writer = wavFormat.createWriterFor(stream, options);
                    return m_writer != nullptr;
                }

            private:
                const int64_t m_refuseAfterBytes;
                WriteRefusalLog &m_log;
            };

            /// @brief The same idea as RefusingOutputStream, as a juce::FileOutputStream.
            ///
            /// It has to be one: ExportMp3MixImplementation::releaseOutput calls getStatus() on its
            /// stream, which juce::OutputStream does not have, so a plain OutputStream cannot stand in
            /// here the way it can for the WAV writer. Writing through to the real file keeps
            /// getStatus meaningful - the refusal is this class's, not the file system's, which is the
            /// point: what is under test is the exporter's handling of a write that comes back false,
            /// not its handling of a broken disk.
            /// @brief When the stream starts refusing.
            ///
            /// Two triggers rather than one because the five writes are not equally easy to reach. A
            /// byte count lands in the middle of the render, where the per-block write is. It cannot
            /// reach the finalisation writes, which come after an unknown number of encoded bytes - so
            /// those are reached by the seek instead: this exporter seeks exactly once, to put the
            /// finished LAME info frame over the placeholder, so "after the first seek" names that
            /// moment exactly and does not move when the fixture's length changes.
            enum class RefusalTrigger
            {
                AfterBytes,
                AfterSeek,
            };

            class RefusingFileOutputStream final : public juce::FileOutputStream
            {
            public:
                RefusingFileOutputStream(const juce::File &target, RefusalTrigger trigger, int64_t refuseAfterBytes, WriteRefusalLog &log)
                    : juce::FileOutputStream{target},
                      m_trigger{trigger},
                      m_refuseAfterBytes{refuseAfterBytes},
                      m_log{log}
                {
                }

                /// Counted, not refused. The seek has to succeed for the write after it to be the
                /// thing under test - a refused seek fails at its own check, one line earlier.
                bool setPosition(juce::int64 newPosition) override
                {
                    ++m_log.seeks;
                    return juce::FileOutputStream::setPosition(newPosition);
                }

                bool write(const void *data, size_t numBytes) override
                {
                    const bool refusing = m_trigger == RefusalTrigger::AfterSeek ? m_log.seeks > 0 : m_log.bytesAccepted >= m_refuseAfterBytes;
                    if (refusing)
                    {
                        ++m_log.refusals;
                        return false;
                    }

                    if (!juce::FileOutputStream::write(data, numBytes))
                    {
                        return false;
                    }

                    m_log.bytesAccepted += static_cast<int64_t>(numBytes);
                    return true;
                }

            private:
                const RefusalTrigger m_trigger;
                const int64_t m_refuseAfterBytes;
                WriteRefusalLog &m_log;
            };

            /// @brief The real MP3 export, writing to a stream that starts refusing mid-render.
            ///
            /// Only createRenderStream is replaced. LAME's initialisation, the ID3v2 tag write, the
            /// tag-frame bookkeeping, onRunMixingLoop, every write check in it, releaseOutput and
            /// run() are the shipping implementations.
            class WriteRefusingMp3Export final : public audio::ExportMp3MixImplementation
            {
            public:
                WriteRefusingMp3Export(MixId mixId,
                    const audio::ActiveExportSettings &settings,
                    RefusalTrigger trigger,
                    int64_t refuseAfterBytes,
                    WriteRefusalLog &log,
                    audio::MixExporterProgressCallback progressCallback)
                    : audio::ExportMp3MixImplementation{mixId, settings, progressCallback},
                      m_trigger{trigger},
                      m_refuseAfterBytes{refuseAfterBytes},
                      m_log{log}
                {
                }

            protected:
                std::unique_ptr<juce::FileOutputStream> createRenderStream(const juce::File &target) override
                {
                    return std::make_unique<RefusingFileOutputStream>(target, m_trigger, m_refuseAfterBytes, m_log);
                }

            private:
                const RefusalTrigger m_trigger;
                const int64_t m_refuseAfterBytes;
                WriteRefusalLog &m_log;
            };

            /// @brief The first juce::TextEditor inside a component, wherever it sits.
            ///
            /// The dialogs below keep their editors private, and the property under test is about the
            /// key reaching the dialog rather than about any particular field, so walking the child
            /// tree asks the question without friending anything or widening an API for a test.
            juce::TextEditor *firstTextEditor(juce::Component &component)
            {
                for (int i = 0; i < component.getNumChildComponents(); ++i)
                {
                    auto *child = component.getChildComponent(i);
                    if (auto *editor = dynamic_cast<juce::TextEditor *>(child))
                    {
                        return editor;
                    }
                    if (auto *nested = firstTextEditor(*child))
                    {
                        return nested;
                    }
                }
                return nullptr;
            }

            // The audio format suite's fixtures. Both are deliberately malformed in one specific
            // way each, because that is the point: a decoder that only handles well-formed input
            // passes every other check in this project.
            constexpr uint32_t kOddSampleCount = 101;        // odd, so the data chunk needs a pad byte
            constexpr uint32_t kId3HeaderBytes = 10;
            constexpr uint32_t kDeclaredId3Padding = 1024;   // the tag body the ID3v2 header declares
            constexpr uint32_t kExtraPaddingAfterId3 = 2048; // bytes past it, before the first sync
            constexpr int kMp3SamplesToRead = 4096;
            constexpr uint32_t kListFixtureSampleCount = 6;
            // The fixed buffer the MP3 exporter used to read the ID3v2 tag into, and a comment long
            // enough to make a tag that does not fit in it.
            constexpr uint32_t kOldId3v2BufferBytes = 10 * 1024;
            constexpr size_t kOversizedCommentBytes = 12 * 1024;
            // The over-claiming fixture: the header says this many samples, the file holds six bytes.
            constexpr uint32_t kOverclaimedSampleCount = 1000;
            // One second in at 44.1 kHz. MP3 frames hold 1152 samples, and the encoder adds its own
            // delay and padding, so the decoded length is a little over this - never under it, which
            // is what the check asserts.
            constexpr int64_t kMp3EncodedSamples = 44100;

            /// @brief Makes a directory refuse to be listed, and puts it back.
            ///
            /// Needed because "cannot be read" has two shapes and only one of them is a missing path.
            /// A directory whose permissions deny listing is still a directory: `is_directory` says
            /// yes, and enumerating it yields nothing. That is the shape a scan must not read as "the
            /// files are gone", and removing the directory - which is what the rest of this section
            /// does - cannot produce it.
            ///
            /// The only platform-specific code in this file, and it is here rather than in a shared
            /// seam because nothing outside this check wants it. Windows has no portable way to do
            /// this: `std::filesystem::permissions` only toggles the read-only attribute there and
            /// leaves listing untouched, so it has to be an ACL, and the cheapest way to set one
            /// without dragging in the security APIs is the tool Windows ships for it. Elsewhere the
            /// portable call does work.
            ///
            /// @return False if the denial could not be applied, so a caller can say the check did not
            ///         run instead of reporting a pass it did not earn.
            bool setDirectoryListable(const std::filesystem::path &path, bool listable)
            {
#if JUCE_WINDOWS
                // *S-1-1-0 is Everyone, by SID so it does not depend on the machine's language.
                const auto command = std::format("icacls \"{}\" {} >nul 2>&1", pathToString(path), listable ? "/remove:d *S-1-1-0" : "/deny *S-1-1-0:(RX)");
                if (std::system(command.c_str()) != 0)
                {
                    return false;
                }
#else
                std::error_code ec;
                std::filesystem::permissions(
                    path, listable ? std::filesystem::perms::owner_all : std::filesystem::perms::none, std::filesystem::perm_options::replace, ec);
                if (ec)
                {
                    return false;
                }
#endif
                // Asked rather than assumed: the command can report success and leave the directory
                // readable anyway - an administrator with backup privilege bypasses the deny.
                std::error_code checkEc;
                const std::filesystem::directory_iterator probe{path, checkEc};
                return listable ? !checkEc : static_cast<bool>(checkEc);
            }

            /// @brief Writes an 8-bit mono WAV whose data chunk has an odd length and is not padded.
            ///
            /// RIFF requires every chunk to occupy an even number of bytes, with a pad byte added when
            /// the payload is odd. Files in the wild routinely omit the final one, because nothing
            /// follows it - the writer had no next chunk to align. A reader that adds the pad
            /// unconditionally then looks for the next chunk one byte past the end of the file.
            ///
            /// 8-bit rather than 16-bit because an odd byte count is the point, and 16-bit samples
            /// cannot produce one. The audio is a square wave rather than silence so that a decode
            /// which quietly returns nothing can be told apart from one that works.
            bool writeOddLengthWavWithoutFinalPadByte(const std::filesystem::path &path, uint32_t sampleCount)
            {
                std::ofstream out{path, std::ios::binary};
                if (!out)
                {
                    return false;
                }

                const auto u32 = [&out](uint32_t v)
                {
                    const unsigned char bytes[4]{static_cast<unsigned char>(v & 0xFF),
                        static_cast<unsigned char>((v >> 8) & 0xFF),
                        static_cast<unsigned char>((v >> 16) & 0xFF),
                        static_cast<unsigned char>((v >> 24) & 0xFF)};
                    out.write(reinterpret_cast<const char *>(bytes), 4);
                };
                const auto u16 = [&out](uint16_t v)
                {
                    const unsigned char bytes[2]{static_cast<unsigned char>(v & 0xFF), static_cast<unsigned char>((v >> 8) & 0xFF)};
                    out.write(reinterpret_cast<const char *>(bytes), 2);
                };

                const uint32_t sampleRate = 44100;
                out.write("RIFF", 4);
                u32(36 + sampleCount); // odd, because sampleCount is
                out.write("WAVE", 4);
                out.write("fmt ", 4);
                u32(16);
                u16(1); // PCM
                u16(1); // mono
                u32(sampleRate);
                u32(sampleRate); // byte rate: one channel, one byte per sample
                u16(1);          // block align
                u16(8);          // bits per sample
                out.write("data", 4);
                u32(sampleCount);

                // 8-bit PCM is unsigned, so 128 is silence. This alternates either side of it.
                for (uint32_t i = 0; i < sampleCount; ++i)
                {
                    const unsigned char sample = (i % 2) == 0 ? static_cast<unsigned char>(200) : static_cast<unsigned char>(56);
                    out.write(reinterpret_cast<const char *>(&sample), 1);
                }

                // And here the file ends. No pad byte, which is the malformation under test.
                return out.good();
            }

            /// @brief Writes a WAV whose final chunk is an odd-length LIST that is not padded.
            ///
            /// An unpadded odd chunk at the very end is common - nothing follows it, so nothing needed
            /// the alignment - and a parser that adds the pad unconditionally computes a chunk end one
            /// byte past the file, then reads the last chunk against a boundary that is not there.
            ///
            /// Deliberately the same construction JUCE's own regression test uses, down to the cue
            /// label, so this suite checks the case upstream describes rather than one of its own
            /// invention. The data chunk here is even-length; the odd one is the LIST.
            ///
            /// This one does NOT discriminate between JUCE 9.0.0 and 9.0.2: it was run on both and
            /// passed on both. On a file stream an over-read past the end already returns nothing, so
            /// the clamp the upstream fix adds changes no outcome here. It is kept as a regression
            /// guard for a shape that currently works, not as evidence for the upgrade.
            ///
            /// @param labelOut Receives the cue label the fixture carries, so the check does not have
            ///        to repeat the literal.
            bool writeWavWithUnpaddedFinalListChunk(const std::filesystem::path &path, std::string &labelOut)
            {
                std::ofstream out{path, std::ios::binary};
                if (!out)
                {
                    return false;
                }

                const auto u32 = [&out](uint32_t v)
                {
                    const unsigned char bytes[4]{static_cast<unsigned char>(v & 0xFF),
                        static_cast<unsigned char>((v >> 8) & 0xFF),
                        static_cast<unsigned char>((v >> 16) & 0xFF),
                        static_cast<unsigned char>((v >> 24) & 0xFF)};
                    out.write(reinterpret_cast<const char *>(bytes), 4);
                };
                const auto u16 = [&out](uint16_t v)
                {
                    const unsigned char bytes[2]{static_cast<unsigned char>(v & 0xFF), static_cast<unsigned char>((v >> 8) & 0xFF)};
                    out.write(reinterpret_cast<const char *>(bytes), 2);
                };

                // WAVE(4) + fmt (8+16) + data (8+6) + LIST (8+19) = 69 bytes of body.
                constexpr uint32_t bodyBytes = 4 + 24 + 14 + 27;
                out.write("RIFF", 4);
                u32(bodyBytes);
                out.write("WAVE", 4);

                out.write("fmt ", 4);
                u32(16);
                u16(1);     // PCM
                u16(1);     // mono
                u32(44100); // sample rate
                u32(44100); // byte rate
                u16(1);     // block align
                u16(8);     // bits per sample

                out.write("data", 4);
                u32(kListFixtureSampleCount);
                for (uint32_t i = 0; i < kListFixtureSampleCount; ++i)
                {
                    // Either side of 128, which is silence for unsigned 8-bit.
                    const unsigned char sample = (i % 2) == 0 ? static_cast<unsigned char>(200) : static_cast<unsigned char>(56);
                    out.write(reinterpret_cast<const char *>(&sample), 1);
                }

                // 4 + 8 + 7 = 19: odd, and the file ends straight after it.
                out.write("LIST", 4);
                u32(19);
                out.write("adtl", 4);
                out.write("labl", 4);
                u32(7); // a cue id plus three bytes of text
                u32(1); // the cue id
                out.write("ab\0", 3);

                labelOut = "ab";
                return out.good();
            }

            /// @brief Writes a WAV whose data chunk declares far more bytes than the file contains.
            ///
            /// Truncation is the ordinary way an audio file goes wrong: an interrupted copy, a full
            /// disk, a download that stopped. The header still says how long the audio was meant to be.
            /// A reader that believes it hands back a length it cannot supply, and whatever reads those
            /// samples gets whatever was after the end of the file.
            ///
            /// This matters here more than the shape of any one chunk, because the scanner opens
            /// whatever is in the user's folders and has no say in how it was produced.
            bool writeWavWithOverclaimingDataChunk(const std::filesystem::path &path, uint32_t declaredSamples, uint32_t actualBytes)
            {
                std::ofstream out{path, std::ios::binary};
                if (!out)
                {
                    return false;
                }

                const auto u32 = [&out](uint32_t v)
                {
                    const unsigned char bytes[4]{static_cast<unsigned char>(v & 0xFF),
                        static_cast<unsigned char>((v >> 8) & 0xFF),
                        static_cast<unsigned char>((v >> 16) & 0xFF),
                        static_cast<unsigned char>((v >> 24) & 0xFF)};
                    out.write(reinterpret_cast<const char *>(bytes), 4);
                };
                const auto u16 = [&out](uint16_t v)
                {
                    const unsigned char bytes[2]{static_cast<unsigned char>(v & 0xFF), static_cast<unsigned char>((v >> 8) & 0xFF)};
                    out.write(reinterpret_cast<const char *>(bytes), 2);
                };

                // The RIFF size describes what is really here, so only the data chunk lies.
                const uint32_t bodyBytes = 4 + 24 + 8 + actualBytes;
                out.write("RIFF", 4);
                u32(bodyBytes);
                out.write("WAVE", 4);

                out.write("fmt ", 4);
                u32(16);
                u16(1);     // PCM
                u16(1);     // mono
                u32(44100); // sample rate
                u32(44100); // byte rate
                u16(1);     // block align
                u16(8);     // bits per sample

                out.write("data", 4);
                u32(declaredSamples); // the lie
                for (uint32_t i = 0; i < actualBytes; ++i)
                {
                    const unsigned char sample = (i % 2) == 0 ? static_cast<unsigned char>(200) : static_cast<unsigned char>(56);
                    out.write(reinterpret_cast<const char *>(&sample), 1);
                }

                return out.good();
            }

            /// @brief Encodes a second of a 440 Hz tone as a VBR MP3, Xing header first.
            ///
            /// LAME is already linked for the MP3 export, so the fixture is generated rather than
            /// checked in - the test carries its own input and depends on nothing in the user's
            /// library, which is how every other fixture here works.
            ///
            /// The Xing/LAME header frame matters: it is what makes a reader treat the file as VBR at
            /// all, and it is the frame whose discovery the padding is meant to disturb. Encoding
            /// emits a PLACEHOLDER for it at the head of the stream, and the finished frame - which
            /// only exists once the whole stream has been encoded and its frame and byte counts are
            /// known - is fetched afterwards and written OVER that placeholder. It does not go in
            /// front of it: doing that leaves the unfinished placeholder in the file as a second
            /// frame, and the fixture then carries a malformation nobody asked for on top of the ID3
            /// padding it is supposed to isolate. That is not hypothetical - it is what this function
            /// did until a reviewer parsed the output and found 42 frames behind a header declaring
            /// 40.
            ///
            /// @return An empty string on success, otherwise what went wrong.
            std::string encodeVbrMp3Frames(std::vector<unsigned char> &out)
            {
                lame_t flags = lame_init();
                if (flags == nullptr)
                {
                    return "lame_init() failed";
                }

                const auto closeAnd = [&flags](std::string why) -> std::string
                {
                    lame_close(flags);
                    return why;
                };

                constexpr int sampleRate = 44100;
                lame_set_in_samplerate(flags, sampleRate);
                lame_set_num_channels(flags, 1);
                lame_set_mode(flags, MONO);
                lame_set_VBR(flags, vbr_default);
                lame_set_VBR_q(flags, 4);
                lame_set_bWriteVbrTag(flags, 1);
                lame_set_quality(flags, 5);

                if (lame_init_params(flags) < 0)
                {
                    return closeAnd("lame_init_params() failed");
                }

                std::vector<short> pcm(sampleRate);
                for (int i = 0; i < sampleRate; ++i)
                {
                    constexpr double pi = 3.14159265358979323846;
                    pcm[static_cast<size_t>(i)] = static_cast<short>(std::sin(2.0 * pi * 440.0 * i / sampleRate) * 12000.0);
                }

                // LAME's own guidance for the worst case: 1.25x the sample count plus 7200.
                std::vector<unsigned char> buffer(static_cast<size_t>(sampleRate * 1.25) + 7200);
                // Mono still wants both pointers; LAME reads the right channel only when it was told
                // there are two.
                const int encoded = lame_encode_buffer(flags, pcm.data(), pcm.data(), sampleRate, buffer.data(), static_cast<int>(buffer.size()));
                if (encoded < 0)
                {
                    return closeAnd(std::format("lame_encode_buffer() returned {}", encoded));
                }
                out.assign(buffer.begin(), buffer.begin() + encoded);

                std::vector<unsigned char> flushBuffer(7200);
                const int flushed = lame_encode_flush(flags, flushBuffer.data(), static_cast<int>(flushBuffer.size()));
                if (flushed < 0)
                {
                    return closeAnd(std::format("lame_encode_flush() returned {}", flushed));
                }
                out.insert(out.end(), flushBuffer.begin(), flushBuffer.begin() + flushed);

                // The finished Xing/LAME header REPLACES the placeholder frame that encoding already
                // emitted at the head of the stream. Inserting it instead leaves the placeholder in
                // place, and the fixture then carries two malformations rather than the one it is
                // supposed to isolate - an unfinished leading frame as well as the ID3 padding.
                const size_t tagSize = lame_get_lametag_frame(flags, nullptr, 0);
                if (tagSize == 0)
                {
                    return closeAnd("lame_get_lametag_frame() reported no tag frame, so the fixture would not be VBR");
                }
                if (out.size() < tagSize)
                {
                    return closeAnd(std::format("the encoder produced {} bytes, too few to hold the {}-byte tag frame it reserved", out.size(), tagSize));
                }

                std::vector<unsigned char> tag(tagSize);
                if (lame_get_lametag_frame(flags, tag.data(), tag.size()) != tagSize)
                {
                    return closeAnd("lame_get_lametag_frame() would not write the Xing header");
                }
                std::copy(tag.begin(), tag.end(), out.begin());

                lame_close(flags);
                if (out.empty())
                {
                    return "the encoder produced no frames";
                }
                return {};
            }

            /// @brief What the MP3 fixture actually turned out to be, read back off the bytes.
            struct Mp3FixtureShape
            {
                bool firstFrameCarriesVbrTag{false};
                bool startsWithId3v2{false};    // an ID3v2 tag at offset zero
                uint32_t id3v2Bytes{0};         // its length, header included, as the tag declares it
                uint32_t firstSyncAt{0};        // where the audio actually starts
                bool id3v2WrittenTwice{false};  // and a second one straight after it
                int id3v1Footers{0};            // 0, 1, or - if both LAME and the caller wrote one - 2
                uint32_t declaredBytes{0};  // what the Xing header says the audio occupies
                uint32_t presentBytes{0};   // what is really there from the first sync to the end
                uint32_t declaredFrames{0}; // audio frames, per the Xing header - the tag frame is extra
                uint32_t parsedFrames{0};   // frames actually walked, the tag frame included
                std::string problem;        // empty when the walk succeeded
            };

            /// @brief Walks the frames of a generated MP3 and reports what shape it came out.
            ///
            /// Exists because a fixture can be malformed in a way its author did not intend, and then
            /// a check built on it proves something other than what it claims. This one was: the
            /// finished Xing frame was inserted in front of the placeholder rather than over it, so the
            /// file carried a stray unfinished frame as well as the ID3 padding under test, and 42
            /// frames sat behind a header declaring 40. Nothing caught it, because nothing looked.
            ///
            /// Only enough of the MPEG header is decoded to step from one frame to the next: version,
            /// layer, bitrate, sample rate and the padding bit.
            ///
            /// @param skipBytes Where to start looking for the first frame. A leading ID3v2 tag is
            ///        stepped over whether or not it is included here, so a caller with no reason to
            ///        know the layout can pass 0; the generated fixture passes more, because it has
            ///        padding past its tag that a search would otherwise walk into.
            Mp3FixtureShape describeMp3Fixture(const std::filesystem::path &path, uint32_t skipBytes)
            {
                Mp3FixtureShape shape;

                std::ifstream in{path, std::ios::binary};
                if (!in)
                {
                    shape.problem = "the fixture could not be reopened";
                    return shape;
                }
                const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};

                // A real exported file ends with a 128-byte ID3v1 footer. It is not audio, and counting
                // it would make the frame walk stop short of the end and report that as damage. Two of
                // them means LAME wrote one during its flush and the caller wrote another, which is
                // what happens when nobody turned the automatic tags off.
                const auto footerAt = [&bytes](size_t end)
                {
                    return end >= 128 && std::equal(bytes.begin() + static_cast<std::ptrdiff_t>(end - 128),
                                             bytes.begin() + static_cast<std::ptrdiff_t>(end - 125),
                                             "TAG");
                };
                size_t audioEnd = bytes.size();
                while (footerAt(audioEnd))
                {
                    ++shape.id3v1Footers;
                    audioEnd -= 128;
                }

                // Step over a leading ID3v2 tag. Its length is a syncsafe integer - seven bits per
                // byte - so the size field itself can never look like a frame sync, but the tag's
                // contents can, which is why this is decoded rather than searched past.
                const auto id3v2LengthAt = [&bytes](size_t at) -> size_t
                {
                    if (at + 10 > bytes.size() || bytes[at] != 'I' || bytes[at + 1] != 'D' || bytes[at + 2] != '3')
                    {
                        return 0;
                    }
                    size_t declared = 0;
                    for (size_t i = at + 6; i < at + 10; ++i)
                    {
                        declared = (declared << 7) | (bytes[i] & 0x7Fu);
                    }
                    return declared + 10;
                };

                size_t floorOffset = skipBytes;
                if (const auto firstTag = id3v2LengthAt(0); firstTag > 0)
                {
                    shape.startsWithId3v2 = true;
                    shape.id3v2Bytes = static_cast<uint32_t>(firstTag);
                    // A second tag straight after the first is the signature of LAME having queued its
                    // own copy in front of the placeholder frame.
                    if (const auto secondTag = id3v2LengthAt(firstTag); secondTag > 0)
                    {
                        shape.id3v2WrittenTwice = true;
                        floorOffset = std::max(floorOffset, firstTag + secondTag);
                    }
                    else
                    {
                        floorOffset = std::max(floorOffset, firstTag);
                    }
                }

                // Find the first frame sync at or after that point.
                size_t offset = floorOffset;
                while (offset + 1 < audioEnd && !(bytes[offset] == 0xFF && (bytes[offset + 1] & 0xE0) == 0xE0))
                {
                    ++offset;
                }
                if (offset + 4 > audioEnd)
                {
                    shape.problem = "no frame sync was found";
                    return shape;
                }

                const size_t firstSync = offset;
                shape.firstSyncAt = static_cast<uint32_t>(firstSync);
                shape.presentBytes = static_cast<uint32_t>(audioEnd - firstSync);

                static constexpr int bitrates[16]{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
                static constexpr int sampleRates[4]{44100, 48000, 32000, 0};

                while (offset + 4 <= audioEnd)
                {
                    const uint32_t header = (static_cast<uint32_t>(bytes[offset]) << 24) | (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
                        (static_cast<uint32_t>(bytes[offset + 2]) << 8) | static_cast<uint32_t>(bytes[offset + 3]);

                    // The sync bits are checked at every frame, not only at the one that was searched
                    // for. Without this the walk accepts any eleven bits it happens to land on as long
                    // as the fields after them read plausibly, so a fixture whose frame lengths are
                    // subtly wrong could still walk to the end of the file and look consistent.
                    if ((header & 0xFFE00000u) != 0xFFE00000u)
                    {
                        break;
                    }

                    const auto version = (header >> 19) & 3u;   // 3 is MPEG 1
                    const auto layer = (header >> 17) & 3u;     // 1 is Layer III
                    const auto bitrateIndex = (header >> 12) & 0xFu;
                    const auto sampleRateIndex = (header >> 10) & 3u;
                    const auto padding = (header >> 9) & 1u;

                    if (version != 3 || layer != 1 || bitrateIndex == 0 || bitrateIndex == 15 || sampleRateIndex == 3)
                    {
                        break;
                    }

                    const auto frameBytes = static_cast<size_t>(144 * (bitrates[bitrateIndex] * 1000) / sampleRates[sampleRateIndex]) + padding;
                    if (frameBytes == 0 || offset + frameBytes > audioEnd)
                    {
                        break;
                    }

                    if (shape.parsedFrames == 0)
                    {
                        // The Xing or Info identifier sits at a fixed offset from the header, after the
                        // side information, and which offset depends on the channel mode. Searching the
                        // frame is enough here and does not need that table.
                        const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
                        const auto end = begin + static_cast<std::ptrdiff_t>(frameBytes);
                        for (const char *needle : {"Xing", "Info"})
                        {
                            const auto found = std::search(begin, end, needle, needle + 4);
                            if (found != end)
                            {
                                shape.firstFrameCarriesVbrTag = true;
                                const auto flagsAt = found + 4;
                                if (flagsAt + 4 <= end)
                                {
                                    const auto read32 = [](std::vector<unsigned char>::const_iterator at)
                                    {
                                        return (static_cast<uint32_t>(*at) << 24) | (static_cast<uint32_t>(*(at + 1)) << 16) |
                                            (static_cast<uint32_t>(*(at + 2)) << 8) | static_cast<uint32_t>(*(at + 3));
                                    };
                                    const auto flags = read32(flagsAt);
                                    auto field = flagsAt + 4;
                                    if ((flags & 1u) != 0 && field + 4 <= end)
                                    {
                                        shape.declaredFrames = read32(field);
                                        field += 4;
                                    }
                                    if ((flags & 2u) != 0 && field + 4 <= end)
                                    {
                                        shape.declaredBytes = read32(field);
                                    }
                                }
                                break;
                            }
                        }
                    }

                    ++shape.parsedFrames;
                    offset += frameBytes;
                }

                if (offset != audioEnd)
                {
                    shape.problem = std::format("the frame walk stopped at byte {} of {}", offset, audioEnd);
                }
                return shape;
            }

            /// @brief Writes frames behind an ID3v2 header that declares less padding than is there.
            ///
            /// The header says its body is @p declaredPadding bytes. After those, @p extraPadding more
            /// zero bytes follow before the first frame sync. A reader that resumes exactly where the
            /// declared tag ends lands on zeros rather than on a sync word, and has to scan forward to
            /// find the audio. Real encoders produce this; the extra bytes are usually the remains of a
            /// tag that was edited to be smaller without rewriting the file.
            bool writeMp3WithPaddingAfterId3v2(const std::filesystem::path &path,
                const std::vector<unsigned char> &frames,
                uint32_t declaredPadding,
                uint32_t extraPadding)
            {
                std::ofstream out{path, std::ios::binary};
                if (!out)
                {
                    return false;
                }

                unsigned char header[kId3HeaderBytes]{'I', 'D', '3', 3, 0, 0, 0, 0, 0, 0};
                // ID3v2 sizes are syncsafe: seven bits per byte, so no byte can look like a frame sync.
                header[6] = static_cast<unsigned char>((declaredPadding >> 21) & 0x7F);
                header[7] = static_cast<unsigned char>((declaredPadding >> 14) & 0x7F);
                header[8] = static_cast<unsigned char>((declaredPadding >> 7) & 0x7F);
                header[9] = static_cast<unsigned char>(declaredPadding & 0x7F);
                out.write(reinterpret_cast<const char *>(header), kId3HeaderBytes);

                const std::vector<char> zeros(static_cast<size_t>(declaredPadding) + extraPadding, 0);
                out.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
                out.write(reinterpret_cast<const char *>(frames.data()), static_cast<std::streamsize>(frames.size()));
                return out.good();
            }

            /// @brief Deflates @p bytes into a zlib stream, the way an ID3v2 writer compresses a frame.
            ///
            /// Through JUCE's deflate rather than zlib's API directly, so the fixture is written by one
            /// of the two zlib users in this binary and read back by the other. On macOS both are meant
            /// to be the SDK's libz; a fixture deflated and inflated by TagLib alone would say nothing
            /// about the copy JUCE calls, and the other way round nothing about TagLib's.
            std::vector<unsigned char> deflateForId3(const std::vector<unsigned char> &bytes)
            {
                juce::MemoryOutputStream compressed;
                {
                    // windowBits 0 is zlib's MAX_WBITS: a zlib-wrapped stream, which is what
                    // inflateInit() expects and TagLib's decoder calls. The stream is finished when the
                    // compressor goes out of scope.
                    juce::GZIPCompressorOutputStream deflater{compressed, 9, 0};
                    deflater.write(bytes.data(), bytes.size());
                }
                const auto *data = static_cast<const unsigned char *>(compressed.getData());
                return {data, data + compressed.getDataSize()};
            }

            /// @brief Inflates a zlib stream through JUCE, for the round trip within JUCE alone.
            std::vector<unsigned char> inflateThroughJuce(const std::vector<unsigned char> &compressed)
            {
                juce::MemoryInputStream source{compressed.data(), compressed.size(), false};
                juce::GZIPDecompressorInputStream inflater{&source, false, juce::GZIPDecompressorInputStream::zlibFormat};
                juce::MemoryOutputStream inflated;
                inflated.writeFromInputStream(inflater, -1);
                const auto *data = static_cast<const unsigned char *>(inflated.getData());
                return {data, data + inflated.getDataSize()};
            }

            /// @brief Writes an ID3v2.4 tag whose title frame is zlib-compressed, then the frames.
            ///
            /// The shape a v2.4 writer produces with compression on: the frame header's second flag
            /// byte carries the compression bit (0x08) and the data length indicator (0x01) the spec
            /// requires next to it, the four syncsafe bytes after the header give the uncompressed
            /// length, and the body is a zlib stream over the text field - its encoding byte first, 3
            /// for UTF-8, then the title. TagLib is the only reader of this in the project; JUCE's MP3
            /// reader steps over the whole tag by the size in its header and never looks inside.
            ///
            /// @param compressed Receives the zlib stream that went into the frame, for the JUCE-only
            ///        round trip beside the TagLib one.
            bool writeMp3WithCompressedTitleFrame(const std::filesystem::path &path,
                const std::vector<unsigned char> &frames,
                const std::string &title,
                std::vector<unsigned char> &compressed)
            {
                std::vector<unsigned char> field{3}; // UTF-8
                field.insert(field.end(), title.begin(), title.end());
                compressed = deflateForId3(field);

                const auto appendSyncsafe = [](std::vector<unsigned char> &out, uint32_t value)
                {
                    out.push_back(static_cast<unsigned char>((value >> 21) & 0x7F));
                    out.push_back(static_cast<unsigned char>((value >> 14) & 0x7F));
                    out.push_back(static_cast<unsigned char>((value >> 7) & 0x7F));
                    out.push_back(static_cast<unsigned char>(value & 0x7F));
                };

                std::vector<unsigned char> frame{'T', 'I', 'T', '2'};
                appendSyncsafe(frame, static_cast<uint32_t>(4 + compressed.size())); // body: indicator + stream
                frame.push_back(0);
                frame.push_back(0x08 | 0x01); // compression, data length indicator
                appendSyncsafe(frame, static_cast<uint32_t>(field.size()));
                frame.insert(frame.end(), compressed.begin(), compressed.end());

                std::vector<unsigned char> tag{'I', 'D', '3', 4, 0, 0};
                appendSyncsafe(tag, static_cast<uint32_t>(frame.size()));
                tag.insert(tag.end(), frame.begin(), frame.end());

                std::ofstream out{path, std::ios::binary};
                if (!out)
                {
                    return false;
                }
                out.write(reinterpret_cast<const char *>(tag.data()), static_cast<std::streamsize>(tag.size()));
                out.write(reinterpret_cast<const char *>(frames.data()), static_cast<std::streamsize>(frames.size()));
                return out.good();
            }

            /// @brief A tag manager that knows no tags. The scanner asks it about genres; the fixture
            /// carries none, so it is never reached, but the scanner needs one to be constructed.
            class NoTagManager final : public database::ITagManager
            {
            public:
                std::optional<TagId> getOrCreateTagId(const std::string &, bool) override
                {
                    return std::nullopt;
                }

                std::optional<std::string> getTagNameById(TagId) const override
                {
                    return std::nullopt;
                }

                std::vector<database::TagInfo> getAllTags(const std::optional<std::string> &) const override
                {
                    return {};
                }
            };

            /// @param title Names the suite. Passed in rather than fixed, because two suites write two
            /// files and a results file that misnames itself is worse than one with no title at all.
            void writeResultsFile(const std::filesystem::path &path, const std::string &title, const Report &report)
            {
                std::ofstream out{path, std::ios::trunc};
                if (!out)
                {
                    spdlog::error("[SelfTest] Could not write results to {}", pathToString(path));
                    return;
                }

                out << title << "\n";
                out << (report.failures() == 0 ? "RESULT: PASS\n" : std::format("RESULT: FAIL ({} failed)\n", report.failures()));
                out << "\n";
                for (const auto &line : report.lines())
                {
                    out << line << "\n";
                }
            }
        } // namespace

        std::string prepareSelfTestEnvironment(std::filesystem::path &rootOut, std::filesystem::path &databasePathOut)
        {
            const char *const configured = std::getenv(kRootEnvVar);
            if (configured == nullptr || configured[0] == 0)
            {
                return std::format("{} is not set. This mode deletes and rewrites whatever it is pointed at, so it only runs"
                                   " against a directory named for that purpose - `just selftest` sets it up.",
                    kRootEnvVar);
            }

            const auto root = expandPath(configured);
            const auto sentinel = root / kSentinelFile;

            // Every filesystem call below gets its own error_code. Sharing one lets a later success
            // clear an earlier failure - a failed exists() followed by a working create_directories()
            // would report nothing at all.
            std::error_code rootExistsEc;
            const bool rootExists = std::filesystem::exists(root, rootExistsEc);
            if (rootExistsEc)
            {
                return std::format("Could not inspect {}: {}", pathToString(root), rootExistsEc.message());
            }

            std::error_code sentinelExistsEc;
            const bool hasSentinel = std::filesystem::exists(sentinel, sentinelExistsEc);
            if (sentinelExistsEc)
            {
                return std::format("Could not inspect {}: {}", pathToString(sentinel), sentinelExistsEc.message());
            }

            if (rootExists && !hasSentinel)
            {
                // Empty is fine - a fresh directory has nothing to lose. Anything else without our
                // sentinel is somebody's real directory, and the first thing this test does is empty it.
                std::error_code emptyEc;
                const bool empty = std::filesystem::is_empty(root, emptyEc);
                if (emptyEc)
                {
                    return std::format("Could not inspect {}: {}", pathToString(root), emptyEc.message());
                }
                if (!empty)
                {
                    return std::format("{} points at {}, which is not empty and carries no {} marker. Refusing to clear a"
                                       " directory this test did not create.",
                        kRootEnvVar, pathToString(root), kSentinelFile);
                }
            }

            std::error_code makeRootEc;
            std::filesystem::create_directories(root, makeRootEc);
            if (makeRootEc)
            {
                return std::format("Could not create the self test root {}: {}", pathToString(root), makeRootEc.message());
            }

            {
                std::ofstream marker{sentinel, std::ios::trunc};
                if (!marker)
                {
                    return std::format("Could not write the marker file {}", pathToString(sentinel));
                }
                marker << "Created by the jucyaudio scan self test. Everything in this directory is disposable." << std::endl;
            }

            // The database gets a directory of its own, removed whole. Deleting just the .db file would
            // leave selftest.db-wal and selftest.db-shm behind after an interrupted run - the database
            // runs in WAL mode (SqliteTrackDatabase.cpp:617) - and those carry committed pages, so the
            // "new" library would start with the previous run's tracks in it.
            //
            // Never the configured database either: settings may name a production path outright, and
            // opening one is not a read-only act, it can run schema migrations.
            const auto databaseDir = root / "db";

            std::error_code removeDbEc;
            std::filesystem::remove_all(databaseDir, removeDbEc);
            if (removeDbEc)
            {
                return std::format("Could not remove the previous self test database directory {}: {}", pathToString(databaseDir), removeDbEc.message());
            }

            std::error_code makeDbEc;
            std::filesystem::create_directories(databaseDir, makeDbEc);
            if (makeDbEc)
            {
                return std::format("Could not create the self test database directory {}: {}", pathToString(databaseDir), makeDbEc.message());
            }

            rootOut = root;
            databasePathOut = databaseDir / "selftest.db";
            return {};
        }

        int runScanSelfTest(const std::filesystem::path &selfTestRoot, const std::filesystem::path &databasePath)
        {
            Report report;
            const auto workRoot = selfTestRoot / "library";
            // Deliberately a sibling of workRoot, not a child. The scan is recursive, so an album parked
            // inside the scanned tree would simply be rediscovered under its new name: four extra tracks
            // inserted, the originals still flagged, and the test proving the opposite of what it claims.
            const auto awayRoot = selfTestRoot / "away";
            const auto resultsPath = selfTestRoot / "selftest-results.txt";

            spdlog::info("[SelfTest] Starting scan self test. Root: {}", pathToString(selfTestRoot));

            // Each step gets its own error_code and its own check. Sharing one across consecutive calls
            // means a success quietly clears the failure before it, and the run continues on a tree that
            // was never cleaned.
            const auto cleanUp = [&report](const std::filesystem::path &path) -> bool
            {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
                if (ec)
                {
                    report.abort(std::format("Could not clear {}: {}", pathToString(path), ec.message()));
                    return false;
                }
                return true;
            };
            const auto makeDirectory = [&report](const std::filesystem::path &path) -> bool
            {
                std::error_code ec;
                std::filesystem::create_directories(path, ec);
                if (ec)
                {
                    report.abort(std::format("Could not create {}: {}", pathToString(path), ec.message()));
                    return false;
                }
                return true;
            };

            const auto albumPath = workRoot / kAlbumFolder;
            const auto awayPath = awayRoot / kAlbumFolder;

            // Both trees, not just the scanned one: a previous run interrupted mid-cycle leaves the album
            // parked in awayRoot, and moving it back would then fail on an existing directory.
            if (!cleanUp(workRoot) || !cleanUp(awayRoot) || !makeDirectory(awayRoot) || !makeDirectory(albumPath))
            {
                writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                return 1;
            }

            for (int i = 1; i <= kTrackCount; ++i)
            {
                const auto file = albumPath / std::format("track{:02}.wav", i);
                // long enough to be a real file, short enough to be instant
                if (!writeSilentWav(file, static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
                {
                    report.abort(std::format("Could not write the fixture {}", pathToString(file)));
                    writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                    return 1;
                }
            }
            report.note(std::format("built a {}-track scratch library at {}", kTrackCount, pathToString(albumPath)));

            auto &db = theTrackLibrary.getTrackDatabase();
            if (!db.getLibraryRootManager().addRoot(pathToString(workRoot)).has_value())
            {
                report.abort("Could not add the scratch library as a library root.");
                writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                return 1;
            }

            const auto rootFolderId = db.getFolderDatabase().findOrCreateFolderByPath(workRoot);
            if (rootFolderId <= 0)
            {
                report.abort("Could not resolve a FolderId for the scratch library root.");
                writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                return 1;
            }

            // --- 1. First scan: the tracks are discovered and none of them is missing. ---

            report.check(runScan({rootFolderId}, false, report, "initial discovery"), "initial scan reports success");

            auto tracks = tracksUnder(db, rootFolderId);
            report.check(static_cast<int>(tracks.size()) == kTrackCount, std::format("{} tracks discovered (found {})", kTrackCount, tracks.size()));
            report.check(countMissing(tracks) == 0, "no track is flagged missing after discovery");

            const auto originalIds = idsOf(tracks);
            if (static_cast<int>(originalIds.size()) != kTrackCount)
            {
                report.abort("Discovery did not produce the expected tracks; the rest of the test would prove nothing.");
                writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                return 1;
            }

            // Real rows in MixTracks and WorkingSetTracks, created now so the missing/recovery cycle has
            // something to break. Stable ids alone would not prove these survived - nothing would have
            // referenced the tracks at all.
            std::vector<TrackInfo> trackInfos;
            std::vector<MixTrack> mixTracks;
            for (const auto &entry : tracks)
            {
                trackInfos.push_back(entry.second);

                MixTrack mixTrack{};
                mixTrack.trackId = entry.second.trackId;
                mixTrack.orderInMix = static_cast<int>(mixTracks.size());
                mixTracks.push_back(mixTrack);
            }

            MixInfo mixInfo{};
            mixInfo.name = kMixName;
            // createOrUpdateMix asserts that a non-empty mix carries a duration, so a zero here aborts a
            // Debug run outright. The value only has to be positive and plausible - nothing in this test
            // reads it back - so the fixtures' own length will do.
            mixInfo.totalDuration = Duration_t{kTrackCount * kFixtureDurationMs};
            const bool mixCreated = theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracks);
            report.check(mixCreated && mixInfo.mixId > 0, std::format("created a mix referencing all {} tracks", kTrackCount));

            WorkingSetInfo workingSetInfo{};
            const bool workingSetCreated =
                theTrackLibrary.getWorkingSetManager().createWorkingSetFromTrackInfos(trackInfos, kWorkingSetName, workingSetInfo);
            report.check(workingSetCreated && workingSetInfo.id > 0, std::format("created a working set holding all {} tracks", kTrackCount));

            if (!mixCreated || !workingSetCreated)
            {
                report.abort("Could not create the referencing rows; the recovery checks would prove nothing.");
                writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                return 1;
            }

            // --- 2. The folder goes away: every track is flagged, and nothing is deleted. ---

            {
                std::error_code renameEc;
                std::filesystem::rename(albumPath, awayPath, renameEc);
                if (renameEc)
                {
                    report.abort(std::format("Could not move the album folder aside: {}", renameEc.message()));
                    writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                    return 1;
                }
            }
            report.note("moved the album folder aside");

            report.check(runScan({rootFolderId}, false, report, "folder missing"), "scan reports success with the folder gone");

            tracks = tracksUnder(db, rootFolderId);
            report.check(countMissing(tracks) == kTrackCount, std::format("all {} tracks flagged missing (flagged {})", kTrackCount, countMissing(tracks)));
            report.check(static_cast<int>(tracks.size()) == kTrackCount, "no track row was deleted when the file vanished");
            report.check(idsOf(tracks) == originalIds, "track ids are unchanged after being flagged");

            // --- 3. Scanning again with the folder still gone changes nothing (B4). ---

            report.check(runScan({rootFolderId}, false, report, "folder still missing"), "repeat scan reports success");

            tracks = tracksUnder(db, rootFolderId);
            report.check(countMissing(tracks) == kTrackCount, "tracks are still flagged after a repeat scan");
            report.check(idsOf(tracks) == originalIds, "track ids survive a repeat scan");
            report.note("the log line above should read '0 newly missing ... (4 were already flagged)' - that is B4's skip");

            // --- 4. The folder comes back: the flag clears and the rows are the same rows. ---

            {
                std::error_code renameEc;
                std::filesystem::rename(awayPath, albumPath, renameEc);
                if (renameEc)
                {
                    report.abort(std::format("Could not move the album folder back: {}", renameEc.message()));
                    writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                    return 1;
                }
            }
            report.note("moved the album folder back");

            report.check(runScan({rootFolderId}, false, report, "folder restored"), "scan reports success with the folder back");

            tracks = tracksUnder(db, rootFolderId);
            report.check(countMissing(tracks) == 0, std::format("no track is flagged any more (still flagged: {})", countMissing(tracks)));

            // The point of the whole exercise.
            report.check(static_cast<int>(tracks.size()) == kTrackCount,
                std::format("still exactly {} track rows - recovery did not insert duplicates (found {})", kTrackCount, tracks.size()));
            report.check(idsOf(tracks) == originalIds, "every recovered track kept its original track_id");

            // Read the references back rather than inferring them from the ids. Both tables cascade on
            // track deletion, so had recovery gone through a delete-and-reinsert these would be empty
            // even though the track count looked right.
            const auto survivingMixTracks = theTrackLibrary.getMixManager().getMixTracks(mixInfo.mixId);
            report.check(static_cast<int>(survivingMixTracks.size()) == kTrackCount,
                std::format("the mix still lists {} tracks (lists {})", kTrackCount, survivingMixTracks.size()));

            std::map<std::string, TrackId> mixIdsByName;
            for (const auto &mixTrack : survivingMixTracks)
            {
                for (const auto &entry : tracks)
                {
                    if (entry.second.trackId == mixTrack.trackId)
                    {
                        mixIdsByName[entry.first] = mixTrack.trackId;
                    }
                }
            }
            report.check(mixIdsByName == originalIds, "the mix still points at the same track ids it was built from");

            report.check(workingSetMembers(db, workingSetInfo.id) == originalIds,
                std::format("the working set still holds the same {} tracks", kTrackCount));

            // --- 5. An empty waveform in the cache is a miss, not a hit. ---
            //
            // Here rather than in a suite of its own because this is the same failure the four
            // steps above describe, one layer down: a track whose file was missing when the mix
            // editor drew it had an AudioThumbnail that decoded nothing written to WaveformCache.
            // JUCE serialises that as a valid 52-byte header, so it read back as a cache hit and
            // the track kept a blank waveform for good - even after the scan above cleared
            // is_missing and the file was demonstrably back. 24 tracks in the real library were
            // in exactly that state.
            //
            // The bytes are written by hand: nothing reachable through the interfaces produces
            // one any more, which is the point of the write guards.
            {
                const auto victimId = originalIds.begin()->second;

                // "jatm", samplesPerThumbSample = 2048, then a zero totalSamples - what
                // MixTrackComponent stored for a source it could not read.
                std::vector<unsigned char> tombstone(52, 0);
                tombstone[0] = 'j';
                tombstone[1] = 'a';
                tombstone[2] = 't';
                tombstone[3] = 'm';
                tombstone[5] = 0x08;

                report.check(theTrackLibrary.saveWaveform(victimId, tombstone).isOk(), "an empty waveform could be staged into the cache");

                std::vector<unsigned char> readBack{0xFF};
                const auto emptyResult = theTrackLibrary.loadWaveform(victimId, readBack);
                report.check(!emptyResult.isOk(), "an empty cached waveform reads back as a miss, so the track regenerates");
                report.check(readBack.empty(), "a rejected waveform hands back nothing rather than the empty blob");

                // The same header carrying samples must still be returned, or the guard would have
                // turned every waveform in the library into a permanent cache miss.
                auto realOne = tombstone;
                realOne[8] = 0x00;
                realOne[9] = 0xD2;
                realOne[10] = 0xE4;
                report.check(theTrackLibrary.saveWaveform(victimId, realOne).isOk(), "a waveform with samples could be staged");

                std::vector<unsigned char> goodBack;
                report.check(theTrackLibrary.loadWaveform(victimId, goodBack).isOk() && goodBack == realOne,
                    "a waveform with samples still reads back unchanged");
            }

            // --- 6. Renaming a genre reaches the vocabulary and every album that uses it. ---
            //
            // Three branches worth separating: a rename to a free name, a rename onto a name that
            // already exists (a merge, because Genres.name is UNIQUE COLLATE NOCASE and refusing
            // would decline the case people rename for), and a change of capitalisation only, which
            // is the same row under that index and so cannot be done by insert-then-delete.
            {
                auto &albums = theTrackLibrary.getAlbumManager();

                const auto albumOne = albums.findOrCreateAlbum("SelfTest Album One", rootFolderId);
                const auto albumTwo = albums.findOrCreateAlbum("SelfTest Album Two", rootFolderId);
                const auto albumBoth = albums.findOrCreateAlbum("SelfTest Album Both", rootFolderId);
                report.check(albumOne > 0 && albumTwo > 0 && albumBoth > 0, "three scratch albums could be created");

                // Into the vocabulary as well as onto the albums. renameGenre resolves both rows in
                // Genres, so names that exist only inside album JSON are not renameable at all - and
                // the merge case below turns on the target already being in the vocabulary.
                report.check(albums.addGenre("selftest-alpha") && albums.addGenre("selftest-beta"), "two scratch genres could be added to the vocabulary");

                // The third album carries both names, in this order, so the merge below has to drop
                // one of them and keep the position of the earlier.
                const std::vector<std::string> moods{"nocturnal"};
                const std::vector<std::string> tags{"selftest"};
                albums.updateAlbumMetadata(albumOne, {"selftest-alpha"}, moods, tags);
                albums.updateAlbumMetadata(albumTwo, {"selftest-beta"}, moods, tags);
                albums.updateAlbumMetadata(albumBoth, {"selftest-alpha", "selftest-beta"}, moods, tags);

                const auto genresOf = [&albums](AlbumId id)
                {
                    const auto info = albums.getAlbumById(id);
                    return info.has_value() ? info->genres : std::vector<std::string>{};
                };
                const auto vocabularyHas = [&albums](const std::string &name)
                {
                    const auto vocabulary = albums.getGenresWithUsage();
                    return std::any_of(vocabulary.begin(),
                        vocabulary.end(),
                        [&name](const GenreUsage &entry) { return entry.name == name; });
                };

                // (a) Rename to a name nothing else uses.
                bool merged = true;
                report.check(albums.renameGenre("selftest-alpha", "selftest-gamma", &merged), "a genre can be renamed to a free name");
                report.check(!merged, "renaming to a free name does not report a merge");
                report.check(vocabularyHas("selftest-gamma") && !vocabularyHas("selftest-alpha"), "the vocabulary carries the new name and not the old");
                report.check(genresOf(albumOne) == std::vector<std::string>{"selftest-gamma"}, "the album using it was relabelled");

                // The rename must not disturb the columns it was not given. updateAlbumMetadata
                // writes genres, moods and tags together, so going through it would blank two of
                // them - which is why renameGenre writes the genres column on its own.
                const auto untouched = albums.getAlbumById(albumOne);
                report.check(untouched.has_value() && untouched->moods == moods && untouched->tags == tags,
                    "renaming a genre leaves the album's moods and tags alone");

                // (b) Rename onto a name that already exists: a merge.
                merged = false;
                report.check(albums.renameGenre("selftest-gamma", "selftest-beta", &merged), "a genre can be renamed onto an existing one");
                report.check(merged, "renaming onto an existing name reports a merge");
                report.check(!vocabularyHas("selftest-gamma"), "the merged-away name is gone from the vocabulary");
                report.check(genresOf(albumOne) == std::vector<std::string>{"selftest-beta"}, "an album holding only the old name now holds the new one");
                report.check(genresOf(albumTwo) == std::vector<std::string>{"selftest-beta"}, "an album that already held the new name is unchanged");

                // The point of the merge rule: one entry, not two, and in the position the first of
                // the two occupied - the leading genre is the headline and must stay the headline.
                report.check(genresOf(albumBoth) == std::vector<std::string>{"selftest-beta"},
                    std::format("an album that held both ends up with one entry (holds {})", genresOf(albumBoth).size()));

                // (c) Capitalisation only. The two names are the same row under COLLATE NOCASE, so
                // an insert-then-delete would delete the row it had just matched.
                merged = true;
                report.check(albums.renameGenre("selftest-beta", "SelfTest-Beta", &merged), "a genre can be recased");
                report.check(!merged, "recasing is a rename, not a merge");
                report.check(vocabularyHas("SelfTest-Beta"), "the vocabulary carries the new capitalisation");
                report.check(genresOf(albumOne) == std::vector<std::string>{"SelfTest-Beta"}, "albums carry the new capitalisation too");

                // (e) A merge adopts the surviving row's spelling, not the one that was typed.
                report.check(albums.addGenre("selftest-delta"), "another scratch genre could be added");
                albums.updateAlbumMetadata(albumTwo, {"selftest-delta"}, moods, tags);
                report.check(albums.renameGenre("selftest-delta", "SELFTEST-BETA", &merged) && merged, "a genre can be merged using a different capitalisation");
                report.check(vocabularyHas("SelfTest-Beta"), "the surviving vocabulary row kept its own spelling");
                report.check(genresOf(albumTwo) == std::vector<std::string>{"SelfTest-Beta"},
                    "the relabelled album carries the surviving spelling, not the typed one");

                // (f) Refusals leave everything alone.
                report.check(!albums.renameGenre("SelfTest-Beta", "   "), "a rename to an empty name is refused");
                report.check(!albums.renameGenre("selftest-not-in-the-vocabulary", "selftest-anything"),
                    "renaming a name that is not in the vocabulary is refused");
                report.check(!albums.renameGenre("selftest-not-in-the-vocabulary", "selftest-not-in-the-vocabulary"),
                    "renaming an absent name to itself is refused too, not waved through as a no-op");
                report.check(albums.renameGenre("SelfTest-Beta", "SelfTest-Beta"), "renaming a genre to exactly its own name is a no-op, not a failure");
                report.check(!vocabularyHas("selftest-anything"), "a refused rename did not invent a vocabulary entry");
                report.check(genresOf(albumOne) == std::vector<std::string>{"SelfTest-Beta"}, "a refused rename changed nothing");

                // (g) An album that lists an unrelated genre twice is not tidied up in passing.
                report.check(albums.addGenre("selftest-epsilon"), "one more scratch genre could be added");
                albums.updateAlbumMetadata(albumBoth, {"selftest-epsilon", "selftest-epsilon"}, moods, tags);
                report.check(albums.renameGenre("SelfTest-Beta", "selftest-zeta"), "an unrelated genre can be renamed");
                report.check(genresOf(albumBoth) == std::vector<std::string>{"selftest-epsilon", "selftest-epsilon"},
                    "an album not carrying the renamed genre was left exactly as it was");

                // (h) Two names that differ only by an accented capital are two rows to SQLite, whose
                // NOCASE collation folds A-Z and nothing else, and they have to stay two throughout.
                // A Unicode-aware fold calls them one, and everything downstream then acts on
                // whichever row it happens to find - relabelling albums that belong to the other.
                //
                // Written as bytes rather than as literals so the test does not depend on how the
                // compiler was told to read this file: C3 89 is U+00C9 E-acute, C3 A9 is U+00E9.
                const std::string accentedUpper{"\xC3\x89" "lectro-selftest"};
                const std::string accentedLower{"\xC3\xA9" "lectro-selftest"};
                report.check(albums.addGenre(accentedUpper) && albums.addGenre(accentedLower),
                    "two genres differing only by an accented capital could both be added");
                report.check(vocabularyHas(accentedUpper) && vocabularyHas(accentedLower), "the vocabulary keeps them as two separate rows");

                albums.updateAlbumMetadata(albumOne, {accentedUpper}, moods, tags);
                albums.updateAlbumMetadata(albumTwo, {accentedLower}, moods, tags);
                report.check(albums.renameGenre(accentedUpper, "selftest-eta"), "one of the two can be renamed to a free name");
                report.check(genresOf(albumOne) == std::vector<std::string>{"selftest-eta"}, "the album carrying the renamed spelling was relabelled");
                report.check(genresOf(albumTwo) == std::vector<std::string>{accentedLower},
                    "the album carrying the other spelling was left alone - this is the whole point");
                report.check(vocabularyHas(accentedLower), "the other vocabulary row survives the rename");
            }

            // --- 7. Mixes.total_length is derived on every write, not carried by the caller. ---
            //
            // It used to be whatever the caller put in MixInfo, and six of the eight paths that wrote
            // a mix passed along the value they were already holding. Appending therefore stored the
            // length the mix had before the append, again and again: one five-hour mix in the real
            // library had accumulated a stored length of sixty-six hours.
            //
            // Every check below reads the figure back out of the database rather than from the
            // MixInfo that was passed in. An in-memory check would have passed throughout the entire
            // period the bug existed, because the editor recomputed for its own display and only the
            // stored column was wrong - which is why reopening a mix brought the wrong number back.
            {
                auto &mixes = theTrackLibrary.getMixManager();

                // Attach points make a mix shorter than the sum of its tracks. Without them every
                // check here would pass against a plain sum, which is one of the wrong answers.
                static constexpr int64_t kOverlapMs = 20;

                // Read from a scanned fixture rather than assumed from kFixtureDurationMs: what the
                // decoder reports is what the walk uses, and a rounded value would put every expected
                // figure below slightly out while looking like a real failure.
                const auto fixtureDuration = tracks.begin()->second.duration;
                report.check(fixtureDuration > Duration_t{0}, std::format("the fixtures have a usable duration ({} ms)", fixtureDuration.count()));

                std::vector<TrackId> orderedIds;
                for (const auto &entry : tracks)
                {
                    orderedIds.push_back(entry.second.trackId);
                }

                const auto buildTracks = [&orderedIds, fixtureDuration](size_t count)
                {
                    std::vector<MixTrack> built;
                    for (size_t i = 0; i < count && i < orderedIds.size(); ++i)
                    {
                        MixTrack mixTrack{};
                        mixTrack.trackId = orderedIds[i];
                        mixTrack.orderInMix = static_cast<int>(i);
                        mixTrack.attachFrom = Duration_t{kOverlapMs};
                        mixTrack.attachTo = fixtureDuration;
                        built.push_back(mixTrack);
                    }
                    return built;
                };

                // What the walk should produce: the first track in full, then each further track
                // adding its length less the overlap. Written out here rather than by calling the
                // production walk, so this asserts a number and not that the code agrees with itself.
                const auto expectedLength = [fixtureDuration](size_t count) -> Duration_t
                {
                    if (count == 0)
                    {
                        return Duration_t{0};
                    }
                    return fixtureDuration + Duration_t{static_cast<int64_t>(count - 1) * (fixtureDuration.count() - kOverlapMs)};
                };

                const auto storedLengthOf = [&mixes](MixId mixId)
                {
                    return mixes.getMix(mixId).totalDuration;
                };

                // (a) Creation stores the walk, not the sum, and not what the caller passed.
                MixInfo durationMix{};
                durationMix.name = "SelfTest Duration Mix";
                // Deliberately absurd, and deliberately non-zero so it would have survived the old
                // assertion. If this value reaches the database, the caller is still being trusted.
                durationMix.totalDuration = Duration_t{99'999'999};

                auto twoTracks = buildTracks(2);
                const bool created = mixes.createOrUpdateMix(durationMix, twoTracks);
                report.check(created && durationMix.mixId > 0, "a mix could be created for the duration checks");

                if (created && durationMix.mixId > 0)
                {
                    report.check(storedLengthOf(durationMix.mixId) == expectedLength(2),
                        std::format("creation stores the walked length ({} ms, expected {} ms)",
                            storedLengthOf(durationMix.mixId).count(),
                            expectedLength(2).count()));
                    report.check(durationMix.totalDuration == expectedLength(2),
                        "creation hands the derived length back to the caller instead of keeping the one passed in");

                    // (b) Appending replaces the length. This is the one that regressed: the old code
                    // stored the previous total, so the figure never grew with the mix.
                    auto fourTracks = buildTracks(4);
                    report.check(mixes.createOrUpdateMix(durationMix, fourTracks), "tracks could be appended to the mix");
                    report.check(storedLengthOf(durationMix.mixId) == expectedLength(4),
                        std::format("appending stores the new length, not the previous one ({} ms, expected {} ms)",
                            storedLengthOf(durationMix.mixId).count(),
                            expectedLength(4).count()));

                    // (c) Deleting a track goes through removeTracksFromMix, which writes MixTracks
                    // directly and never touched Mixes.total_length.
                    report.check(mixes.removeTracksFromMix(durationMix.mixId, {orderedIds.back()}), "a track could be removed from the mix");
                    report.check(storedLengthOf(durationMix.mixId) == expectedLength(3),
                        std::format("removing a track updates the stored length ({} ms, expected {} ms)",
                            storedLengthOf(durationMix.mixId).count(),
                            expectedLength(3).count()));
                    report.check(mixes.getMix(durationMix.mixId).numberOfTracks == 3, "removing a track updates the stored count");

                    // (d) A cue edit changes the length without changing the membership. updateMixTrack
                    // is another direct MixTracks write.
                    auto edited = mixes.getMixTracks(durationMix.mixId);
                    report.check(edited.size() == 3, "the mix reads back with three tracks before the cue edit");
                    if (edited.size() == 3)
                    {
                        // Trims the last track, so only the tail of the mix moves.
                        edited.back().cueEnd = Duration_t{-10};
                        report.check(mixes.updateMixTrack(durationMix.mixId, edited.back()), "a cue point could be edited");
                        report.check(storedLengthOf(durationMix.mixId) == expectedLength(3) - Duration_t{10},
                            std::format("a cue edit updates the stored length ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                (expectedLength(3) - Duration_t{10}).count()));
                        edited.back().cueEnd = Duration_t{0};
                        mixes.updateMixTrack(durationMix.mixId, edited.back());
                    }

                    // (e) Reordering. With three identical tracks the length cannot change, so a check
                    // against it would pass whether or not reordering refreshes anything. The first
                    // track is therefore given a distinct attachTo, which makes the total depend on the
                    // order: the walk excludes the last track's attachTo and the first track's
                    // attachFrom, so moving that track from front to back changes the answer.
                    auto ordered = mixes.getMixTracks(durationMix.mixId);
                    report.check(ordered.size() == 3, "the mix has three tracks before the reorder");
                    if (ordered.size() == 3)
                    {
                        ordered.front().attachTo = fixtureDuration - Duration_t{40};
                        report.check(mixes.updateMixTrack(durationMix.mixId, ordered.front()), "the first track could be given a distinct attach point");

                        // [A B C] with A handing over 40 ms earlier: A at 0, B at D-60, C at 2D-80.
                        const auto beforeReorder = Duration_t{3 * fixtureDuration.count() - 80};
                        report.check(storedLengthOf(durationMix.mixId) == beforeReorder,
                            std::format("the distinct attach point shortened the mix ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                beforeReorder.count()));

                        // [B C A]: B at 0, C at D-20, A at 2D-40. A's early handover no longer counts,
                        // because nothing follows it.
                        const auto afterReorder = Duration_t{3 * fixtureDuration.count() - 40};
                        report.check(afterReorder != beforeReorder, "the two orders really do have different lengths, so the next check can fail");

                        report.check(mixes.reorderTrackInMix(durationMix.mixId, 0, 2), "a track could be reordered");
                        report.check(storedLengthOf(durationMix.mixId) == afterReorder,
                            std::format("reordering updates the stored length ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                afterReorder.count()));

                        // Back to uniform, so the checks after this one can keep using expectedLength.
                        auto restored = mixes.getMixTracks(durationMix.mixId);
                        restored.back().attachTo = fixtureDuration;
                        report.check(mixes.updateMixTrack(durationMix.mixId, restored.back()), "the attach point could be restored");
                        report.check(storedLengthOf(durationMix.mixId) == expectedLength(3),
                            std::format("restoring it returns the mix to its uniform length ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                expectedLength(3).count()));
                    }

                    // (f) A track deleted from the middle of the mix.
                    //
                    // Not a mix row whose track cannot be resolved: MixTracks.track_id is a foreign key
                    // that cascades, so deleting a track takes its mix row with it, and no row pointing
                    // at a deleted track can exist while that key stands. An unresolvable row is
                    // reached the other way, through a query that filters offline folders, and what a
                    // walk over a mix does with one is checked separately over the walk itself. What is
                    // reachable here, and what this checks, is that a cascade leaves the stored length
                    // describing what remains.
                    const auto beforeDeletion = mixes.getMixTracks(durationMix.mixId);
                    report.check(beforeDeletion.size() == 3, std::format("the mix has three tracks before the deletion (has {})", beforeDeletion.size()));
                    if (beforeDeletion.size() == 3)
                    {
                        report.check(db.removeTracks({beforeDeletion[1].trackId}).isOk(), "the middle track could be deleted from the library");

                        const auto survivors = mixes.getMixTracks(durationMix.mixId);
                        report.check(survivors.size() == 2, std::format("deleting it cascaded its row out of the mix (left {})", survivors.size()));

                        // Read before anything is recomputed. Repairing first and checking afterwards
                        // would pass whether or not the deletion refreshed anything, which is exactly
                        // what production does not do for itself.
                        report.check(storedLengthOf(durationMix.mixId) == expectedLength(2),
                            std::format("deleting a track updates the mixes it cascaded out of ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                expectedLength(2).count()));
                        report.check(mixes.getMix(durationMix.mixId).numberOfTracks == 2, "the cascade updated the stored count too");

                        // And the recomputation finds nothing to do, which is the same statement made
                        // from the other side: the deletion already left the row correct.
                        MixDurationCheck check;
                        report.check(mixes.recomputeMixDuration(durationMix.mixId, check).isOk(), "the mix can be rechecked after the cascade");
                        report.check(!check.changed, "the recheck finds nothing to correct, because the deletion already did it");
                    }

                    // (g) Rechecking a mix that is already right must not write. That is what makes a
                    // second run of the repair a verification of the first rather than a repeat.
                    MixDurationCheck recheck;
                    report.check(mixes.recomputeMixDuration(durationMix.mixId, recheck).isOk(), "a correct mix can be rechecked");
                    report.check(!recheck.changed, "rechecking a mix that is already correct does not write to it");
                    report.check(recheck.previous.totalLength == recheck.current.totalLength,
                        "an unchanged recheck reports the same length before and after");

                    // (h) The repair actually repairing something.
                    //
                    // Every check above starts from a correct row, so none of them would notice if
                    // recomputeMixDuration never wrote anything at all. This stages the state the whole
                    // one-off pass exists for - a stored summary that disagrees with the rows - and
                    // checks the correction, both reported figures, and what actually landed in the row.
                    //
                    // Staged over the test's own connection because nothing reachable through the
                    // interfaces can write those columns any more. That is the fix; it is also why this
                    // case has to be built from outside.
                    const auto correctLength = storedLengthOf(durationMix.mixId);
                    const auto correctCount = mixes.getMix(durationMix.mixId).numberOfTracks;

                    // Deliberately both wrong, and wrong in the direction the real library was: a length
                    // far longer than the mix, and a count that does not match its rows either.
                    constexpr int64_t kWrongLengthMs = 99'999'999;
                    const bool staged = setMixSummary(databasePath, durationMix.mixId, correctCount + 7, kWrongLengthMs);
                    report.check(staged, "a wrong summary could be staged onto the mix");

                    if (staged)
                    {
                        report.check(storedLengthOf(durationMix.mixId) == Duration_t{kWrongLengthMs}, "the staged summary really is in the row");

                        MixDurationCheck repair;
                        report.check(mixes.recomputeMixDuration(durationMix.mixId, repair).isOk(), "the mix with a wrong summary can be rechecked");
                        report.check(repair.changed, "a mix whose summary disagrees with its rows is corrected");

                        report.check(repair.previous.totalLength == Duration_t{kWrongLengthMs} && repair.previous.trackCount == correctCount + 7,
                            "the report of what was there beforehand matches what was staged");
                        report.check(repair.current.totalLength == correctLength && repair.current.trackCount == correctCount,
                            "the report of what it became matches the walk");

                        // The row itself, not just what the call said about it.
                        report.check(storedLengthOf(durationMix.mixId) == correctLength,
                            std::format("the corrected length reached the database ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                correctLength.count()));
                        report.check(mixes.getMix(durationMix.mixId).numberOfTracks == correctCount, "the corrected count reached the database too");

                        // And running it again finds nothing to do, which is what makes a second pass a
                        // verification of the first.
                        MixDurationCheck second;
                        report.check(mixes.recomputeMixDuration(durationMix.mixId, second).isOk(), "the repaired mix can be rechecked");
                        report.check(!second.changed, "a second pass over a repaired mix changes nothing");
                    }

                    // (i) A middle row that cannot be decoded.
                    //
                    // The failure this pins down is not the parse itself - it is what the readers do
                    // with it. Returning the rows that came before the bad one hands back a shorter
                    // mix, and the next save rewrites the row set to match: every track after the
                    // corrupt one, permanently gone. So the rule is all rows or none, and every caller
                    // that might write the mix back has to be able to tell those apart from an empty
                    // mix.
                    //
                    // Three rows first. The cascade above left two, and corrupting the last of two
                    // proves less: with a valid row on each side of the bad one, a reader that
                    // published what it had managed to read would hand back exactly one row - a number
                    // that is neither the empty answer nor the right one, so the checks below can tell
                    // all three outcomes apart.
                    auto survivors = mixes.getMixTracks(durationMix.mixId);
                    report.check(survivors.size() == 2, "two rows survived the cascade, to be built back up to three");
                    if (survivors.size() == 2)
                    {
                        // The same track twice is fine - a mix may list one more than once - and it
                        // avoids depending on a fourth track that earlier steps may have deleted.
                        auto three = survivors;
                        three.push_back(survivors.front());

                        // Renumbered from zero, rather than kept as they came back. A cascade does not
                        // renumber, so these two carry orders 0 and 2 - and MixTracks has only an
                        // index on (mix_id, order_in_mix), not a unique constraint, so appending a
                        // third at position 2 is accepted and leaves the mix with no row at position
                        // 1 at all. The corruption below then matches nothing.
                        for (size_t i = 0; i < three.size(); ++i)
                        {
                            three[i].orderInMix = static_cast<int>(i);
                        }
                        report.check(mixes.createOrUpdateMix(durationMix, three), "the mix could be built back up to three rows");

                        const auto rebuilt = mixes.getMixTracks(durationMix.mixId);
                        bool contiguous = rebuilt.size() == 3;
                        for (size_t i = 0; contiguous && i < rebuilt.size(); ++i)
                        {
                            contiguous = rebuilt[i].orderInMix == static_cast<int>(i);
                        }
                        report.check(contiguous, "the rebuilt rows sit at positions 0, 1 and 2, so there is a middle one to corrupt");
                    }

                    const auto beforeCorruption = mixes.getMix(durationMix.mixId);
                    report.check(beforeCorruption.numberOfTracks == 3, std::format("three rows are in place before the corruption (found {})", beforeCorruption.numberOfTracks));

                    // Order 1 of 0, 1, 2: a valid row before it and a valid row after it.
                    const bool corrupted = corruptMixData(databasePath, durationMix.mixId, 1);
                    report.check(corrupted, "the middle row's mix_data could be corrupted");

                    if (corrupted)
                    {
                        std::vector<MixTrack> readBack;
                        const auto readResult = mixes.readMixTracks(durationMix.mixId, readBack);
                        report.check(!readResult.isOk(), "reading a mix with an undecodable row fails rather than succeeding partly");
                        report.check(readBack.empty(),
                            std::format("a failed read hands back nothing, not the row before the bad one (got {})", readBack.size()));
                        report.check(readResult.errorMessage.find("mix_data") != std::string::npos,
                            std::format("the reason names what went wrong (said: '{}')", readResult.errorMessage));

                        // The statusless wrapper is what display-only callers use. It must return an
                        // empty mix, which is obviously wrong, rather than a prefix, which is not.
                        report.check(mixes.getMixTracks(durationMix.mixId).empty(), "the statusless reader returns nothing rather than a prefix");

                        // And the summary is not rewritten from what could be parsed.
                        MixDurationCheck afterCorruption;
                        report.check(!mixes.recomputeMixDuration(durationMix.mixId, afterCorruption).isOk(),
                            "recomputing refuses a mix it cannot fully read");
                        report.check(!afterCorruption.changed, "a refused recomputation writes nothing");

                        const auto afterMix = mixes.getMix(durationMix.mixId);
                        report.check(afterMix.totalDuration == beforeCorruption.totalDuration && afterMix.numberOfTracks == beforeCorruption.numberOfTracks,
                            "the stored summary is left exactly as it was");
                    }

                    mixes.removeMix(durationMix.mixId);
                }
            }

            // --- 8. A forced rescan refreshes rows instead of colliding with them. ---
            //
            // The forced path used to build a TrackInfo from the file, leave its track id unset, and
            // hand it to saveTrackInfo - which decides insert or update on that id. So every file it
            // was asked to refresh took the INSERT branch and hit UNIQUE(folder_id, filename). A forced
            // rescan did nothing at all, once per file, and reported success.
            //
            // Setting the id would have been the smaller fix and the wrong one: saveTrackInfo's UPDATE
            // writes every column, and a TrackInfo built from a file carries defaults for everything
            // the file cannot answer for. The checks below are as much about what a rescan must not
            // touch as about what it must.
            {
                // A baseline of its own, taken now rather than reusing step 1's.
                //
                // Section 7 deleted a track from the library whose file is still sitting in the tree, so
                // the next scan legitimately inserts it again under a new id. Comparing against the ids
                // step 1 recorded would fail on that alone and say nothing about the forced path. One
                // plain scan settles the library against the tree first; everything below is measured
                // from what that leaves.
                report.check(runScan({rootFolderId}, false, report, "settle before the forced rescan"), "the settling scan reports success");

                const auto baselineCount = static_cast<int>(trackRowsUnder(db, rootFolderId).size());
                const auto baselineIds = idsOf(tracksUnder(db, rootFolderId));
                report.check(baselineCount > 0 && static_cast<int>(baselineIds.size()) == baselineCount,
                    std::format("the library and the tree agree before the forced rescan ({} tracks)", baselineCount));

                const auto victimId = baselineIds.begin()->second;

                // Analysis and library history, staged so a rescan has something to flatten. Neither is
                // derivable from the file, which is what makes them the test.
                report.check(db.updateTrackBpm(victimId, AudioMetadata{128.0f}).isOk(), "a BPM could be staged onto a track");

                const auto before = db.getTrackById(victimId);
                report.check(before.has_value() && before->bpm.has_value() && before->bpm.value() > 0, "the staged BPM really is in the row");

                // Compared against itself afterwards rather than against a literal: the column is
                // normalised on the way in, and this is a test of what a rescan preserves, not of the
                // normalisation constant.
                const auto bpmBefore = (before.has_value() && before->bpm.has_value()) ? before->bpm.value() : 0;
                const auto dateAddedBefore = before.has_value() ? before->date_added : Timestamp_t{};

                // A title to lose, staged before the rescan. The fixtures carry no tags at all, so
                // without this every check about tags below would be comparing empty to empty.
                const auto stageTitle = [&db, victimId](const std::string &title)
                {
                    const auto row = db.getTrackById(victimId);
                    if (!row.has_value())
                    {
                        return false;
                    }

                    auto staged = row.value();
                    staged.title = title;
                    return db.updateScannedTrackData(staged, ScannedFields::Tags).isOk();
                };

                report.check(stageTitle("SelfTest Title Before Rescan"), "a title could be staged onto the track");

                report.check(runScan({rootFolderId}, false, report, "forced rescan", true), "a forced rescan reports success");

                const auto rescanRows = trackRowsUnder(db, rootFolderId);
                report.check(static_cast<int>(rescanRows.size()) == baselineCount,
                    std::format("a forced rescan inserted nothing - still {} rows (found {})", baselineCount, rescanRows.size()));
                report.check(idsOf(tracksUnder(db, rootFolderId)) == baselineIds, "and every track kept its id through the forced rescan");

                const auto after = db.getTrackById(victimId);
                report.check(after.has_value(), "the rescanned track can still be read back");
                if (after.has_value())
                {
                    report.check(after->bpm.has_value() && after->bpm.value() == bpmBefore,
                        "a forced rescan leaves the BPM alone - the file cannot answer for it");
                    report.check(after->date_added == dateAddedBefore,
                        "and leaves date_added alone - that is the library's history, not the file's");
                    report.check(after->duration > Duration_t{0}, "while the columns a scan does own are filled in");
                    report.check(!after->is_missing, "and the track is not missing");

                    // The other half of the rule. This file reads perfectly well and genuinely has no
                    // tags, so its empty tag is an answer and the staged title has to go. Without this,
                    // "never write an empty tag" would be indistinguishable from the correct rule, and
                    // clearing a title in a tag editor would stop working.
                    report.check(after->title.empty(),
                        std::format("a readable file with no tags clears the title - an empty tag is an answer (title is now '{}')", after->title));
                }

                // A file that cannot be read is not a file that says everything is blank.
                //
                // Id3TagScanner returns false for a file it cannot open, having filled in nothing - so
                // the TrackInfo carries a zero duration, an empty title and no audio properties. Writing
                // those over an existing row erases what the library knew because one read failed. The
                // file is truncated rather than deleted so that the scan still finds it: this is the
                // unreadable case, not the missing case, and the two take different paths.
                const auto durationBefore = after.has_value() ? after->duration : Duration_t{0};
                const auto victimPath = albumPath / (after.has_value() ? after->filename : std::string{});
                report.check(durationBefore > Duration_t{0}, "the track about to be truncated has a duration to lose");

                // And a title to lose as well. TagLib returns a non-null, empty tag object for a
                // malformed file exactly as it does for a real file with no tags, so without a
                // non-empty title staged here the erasure would look identical to a correct no-op.
                const std::string titleBefore{"SelfTest Title Before Truncation"};
                report.check(stageTitle(titleBefore), "a title could be staged before the file is truncated");

                {
                    std::ofstream truncate{victimPath, std::ios::binary | std::ios::trunc};
                    report.check(truncate.good(), std::format("{} could be truncated to nothing", pathToString(victimPath)));
                }

                report.check(runScan({rootFolderId}, false, report, "forced rescan over an unreadable file", true),
                    "a forced rescan over an unreadable file reports success");

                const auto afterTruncation = db.getTrackById(victimId);
                report.check(afterTruncation.has_value(), "the unreadable track still has its row");
                if (afterTruncation.has_value())
                {
                    report.check(afterTruncation->duration == durationBefore, "an unreadable file does not blank the duration the library already had");
                    report.check(afterTruncation->bpm.has_value() && afterTruncation->bpm.value() == bpmBefore, "nor the BPM");
                    report.check(afterTruncation->title == titleBefore,
                        std::format("nor the title - an empty tag object from a file that would not decode is not an answer (title is now '{}')",
                            afterTruncation->title));
                    report.check(afterTruncation->filesize_bytes == 0, "while the size the filesystem reports is written, because that much was read");
                }
            }

            // --- 9. A file that moved keeps its track id, and the mixes that use it. ---
            //
            // Moving a file used to be an insert and a flagging: a new row for the file in its new
            // folder, the old row marked missing. Every mix, working set and album entry stayed pointing
            // at the row that was now missing, so reorganising a folder outside the app broke every mix
            // that used anything in it - with the files sitting right there on disk.
            //
            // The decision cannot be made during the walk, because "the old file is gone" is not known
            // until every folder has been visited. What that buys is the second half of this section: a
            // file that was copied rather than moved must not take the original's identity.
            {
                const auto movedRoot = workRoot / "moved";
                const auto copyRoot = workRoot / "copied";
                if (makeDirectory(movedRoot) && makeDirectory(copyRoot))
                {
                    // Again its own baseline, and its own choice of victim: which of the fixture files
                    // still has a row depends on what the sections above deleted, so the file to move is
                    // picked from what is actually in the library now.
                    const auto beforeMove = tracksUnder(db, rootFolderId);
                    const auto baselineCount = static_cast<int>(trackRowsUnder(db, rootFolderId).size());
                    const auto baselineIds = idsOf(beforeMove);

                    // What the mix lists now, not what it was built with - section 7's cascade took a
                    // row out of it, and the point here is that the move takes none.
                    const auto mixTracksBeforeMove = theTrackLibrary.getMixManager().getMixTracks(mixInfo.mixId);
                    const auto mixBeforeMove = mixTracksBeforeMove.size();

                    // A track the mix actually uses, or the mix check below would hold however badly the
                    // move went. Not just any surviving row: the settling scan above re-inserted the file
                    // section 7 deleted, and that new row is in no mix at all.
                    std::string movedName;
                    TrackId movedTrackId{-1};
                    FolderId albumFolderId{-1};
                    for (const auto &entry : beforeMove)
                    {
                        const auto inTheMix = std::any_of(mixTracksBeforeMove.begin(),
                            mixTracksBeforeMove.end(),
                            [&entry](const MixTrack &mixTrack)
                            {
                                return mixTrack.trackId == entry.second.trackId;
                            });
                        if (inTheMix && entry.second.folderId == db.getFolderDatabase().findOrCreateFolderByPath(albumPath))
                        {
                            movedName = entry.second.filename;
                            movedTrackId = entry.second.trackId;
                            albumFolderId = entry.second.folderId;
                            break;
                        }
                    }
                    const auto movedTo = movedRoot / movedName;

                    report.check(movedTrackId > 0, std::format("the track about to be moved ('{}') is in the library and in the mix", movedName));

                    std::error_code moveEc;
                    if (movedTrackId > 0)
                    {
                        std::filesystem::rename(albumPath / movedName, movedTo, moveEc);
                        report.check(!moveEc, std::format("the file could be moved into {}", pathToString(movedRoot)));
                    }

                    if (!moveEc && movedTrackId > 0)
                    {
                        report.check(runScan({rootFolderId}, false, report, "after a file moved"), "the scan after the move reports success");

                        // Rows for the count, because a duplicate insert would put the same filename in
                        // a second folder - which is precisely what a map keyed by filename hides. The
                        // keyed view is still what the id comparison needs.
                        const auto moveRows = trackRowsUnder(db, rootFolderId);
                        report.check(static_cast<int>(moveRows.size()) == baselineCount,
                            std::format("no duplicate row was inserted for the moved file - still {} (found {})", baselineCount, moveRows.size()));
                        report.check(countMissing(moveRows) == 0, "and nothing is flagged missing, because nothing is");
                        report.check(idsOf(tracksUnder(db, rootFolderId)) == baselineIds, "every track id survived the move, the moved one included");

                        // The mix is the reason any of this matters. Matching ids are not enough on
                        // their own: MixTracks cascades on track deletion, so a delete-and-reinsert
                        // would leave the mix short while the id set still looked plausible.
                        const auto mixAfterMove = theTrackLibrary.getMixManager().getMixTracks(mixInfo.mixId).size();
                        report.check(mixAfterMove == mixBeforeMove,
                            std::format("the mix still lists the same {} tracks after the move (lists {})", mixBeforeMove, mixAfterMove));

                        const auto moved = db.getTrackById(movedTrackId);
                        report.check(moved.has_value() && moved->folderId != albumFolderId,
                            "the moved track's row now names the folder the file is actually in");
                    }

                    // A copy is not a move. Same name, same size, but the original never went anywhere,
                    // so the match must be refused and a new row inserted. Getting this wrong hands the
                    // original's mix references to the copy and leaves the original looking new.
                    std::error_code copyEc;
                    if (!moveEc && movedTrackId > 0)
                    {
                        std::filesystem::copy_file(movedTo, copyRoot / movedName, copyEc);
                        report.check(!copyEc, "a second file with the same name and size could be made");
                    }

                    if (!copyEc && !moveEc && movedTrackId > 0)
                    {
                        report.check(runScan({rootFolderId}, false, report, "after a file was copied"), "the scan after the copy reports success");

                        // Unkeyed, because the copy and the original share a filename and a keyed map
                        // would show one entry whether the insert happened or not.
                        const auto afterCopy = trackRowsUnder(db, rootFolderId);
                        report.check(static_cast<int>(afterCopy.size()) == baselineCount + 1,
                            std::format("the copy was inserted as its own track ({} rows, expected {})", afterCopy.size(), baselineCount + 1));

                        const auto stillThere = db.getTrackById(movedTrackId);
                        report.check(stillThere.has_value() && stillThere->folderId == db.getFolderDatabase().findOrCreateFolderByPath(movedRoot),
                            "and the original kept its own row rather than being re-identified as the copy");
                        report.check(countMissing(afterCopy) == 0, "with nothing flagged missing on either side of it");
                    }
                }
            }

            // --- 10. Two shapes that look like a move and are not. ---
            //
            // The rule is one file, one row, and that row's file is gone. Each half below breaks one of
            // those and must be refused: guessing here attaches a track's history and every mix that
            // uses it to an arbitrary file, and nothing afterwards says it happened.
            {
                const auto ambiguousRoot = workRoot / "ambiguous";
                const auto twinRootA = ambiguousRoot / "a";
                const auto twinRootB = ambiguousRoot / "b";
                const auto copyRoot = workRoot / "copied"; // section 9's, named again rather than shared

                if (makeDirectory(twinRootA) && makeDirectory(twinRootB))
                {
                    // (a) A row that vanished, and another row of the same name and size that did not.
                    //
                    // Section 9 left two rows sharing a name and size: the moved file and the copy. Move
                    // the copy on, and its old row is gone while the other is still on disk - so the
                    // name and size identify nothing, and the fact that one of them vanished does not
                    // make the survivor's twin this file.
                    const auto rowsBefore = static_cast<int>(trackRowsUnder(db, rootFolderId).size());
                    const auto copyFolderId = db.getFolderDatabase().findOrCreateFolderByPath(copyRoot);

                    TrackId copyTrackId{-1};
                    std::string twinName;
                    for (const auto &track : trackRowsUnder(db, rootFolderId))
                    {
                        if (track.folderId == copyFolderId)
                        {
                            copyTrackId = track.trackId;
                            twinName = track.filename;
                        }
                    }
                    report.check(copyTrackId > 0, "section 9's copy has a row of its own to move");

                    std::error_code twinEc;
                    if (copyTrackId > 0)
                    {
                        std::filesystem::rename(copyRoot / twinName, twinRootA / twinName, twinEc);
                        report.check(!twinEc, "the copy could be moved on again");
                    }

                    if (copyTrackId > 0 && !twinEc)
                    {
                        report.check(runScan({rootFolderId}, false, report, "a vanished row with a live twin"),
                            "the scan reports success with an ambiguous match on offer");

                        const auto rowsAfter = trackRowsUnder(db, rootFolderId);
                        report.check(static_cast<int>(rowsAfter.size()) == rowsBefore + 1,
                            std::format("the file was inserted as a new track rather than matched ({} rows, expected {})",
                                rowsAfter.size(),
                                rowsBefore + 1));

                        const auto orphan = db.getTrackById(copyTrackId);
                        report.check(orphan.has_value() && orphan->is_missing,
                            "and the row whose file went away is flagged missing, not quietly handed to the new file");
                        report.check(orphan.has_value() && orphan->folderId == copyFolderId,
                            "the flagged row still names the folder it was in");
                    }

                    // (b) One vanished row, two new files that both match it.
                    //
                    // Whichever the directory walk returned first would otherwise take the identity,
                    // which is an accident rather than a decision.
                    const auto rowsBeforeTwins = static_cast<int>(trackRowsUnder(db, rootFolderId).size());
                    const auto albumFolderId = db.getFolderDatabase().findOrCreateFolderByPath(albumPath);

                    TrackId twinSourceId{-1};
                    std::string twinSourceName;
                    for (const auto &track : trackRowsUnder(db, rootFolderId))
                    {
                        if (track.folderId == albumFolderId && !track.is_missing && track.filesize_bytes > 0)
                        {
                            twinSourceId = track.trackId;
                            twinSourceName = track.filename;
                        }
                    }
                    report.check(twinSourceId > 0, "a readable track is still in the album folder to make twins of");

                    if (twinSourceId > 0)
                    {
                        std::error_code copyEcA;
                        std::error_code copyEcB;
                        std::error_code removeEc;
                        std::filesystem::copy_file(albumPath / twinSourceName, twinRootA / twinSourceName, copyEcA);
                        std::filesystem::copy_file(albumPath / twinSourceName, twinRootB / twinSourceName, copyEcB);
                        std::filesystem::remove(albumPath / twinSourceName, removeEc);
                        report.check(!copyEcA && !copyEcB && !removeEc, "one file could be turned into two identical files elsewhere");

                        if (!copyEcA && !copyEcB && !removeEc)
                        {
                            report.check(runScan({rootFolderId}, false, report, "two new files matching one vanished row"),
                                "the scan reports success with two files competing for one row");

                            const auto rowsAfterTwins = trackRowsUnder(db, rootFolderId);
                            report.check(static_cast<int>(rowsAfterTwins.size()) == rowsBeforeTwins + 2,
                                std::format("both files were inserted as new tracks ({} rows, expected {})",
                                    rowsAfterTwins.size(),
                                    rowsBeforeTwins + 2));

                            const auto contested = db.getTrackById(twinSourceId);
                            report.check(contested.has_value() && contested->is_missing,
                                "and the row they were competing for is flagged missing rather than given to one of them");
                            report.check(contested.has_value() && contested->folderId == albumFolderId,
                                "it did not follow either file out of the album folder");
                        }
                    }
                }
            }

            // --- 11. The write mask, one field group at a time. ---
            //
            // A read can half succeed: TagLib hands back a tag object for plenty of files whose audio
            // properties it cannot work out, and the reverse happens too. A single "did the read work"
            // answer forces a choice between throwing away tags that were read and writing a zero
            // duration over a real one, and the second is what a coarse flag would have done.
            //
            // Tested here rather than through a crafted file, because a file that gives up its tags and
            // defeats the property reader is not something a test can reliably construct - while the
            // write mask is exactly where the damage would be done.
            {
                const auto subject = trackRowsUnder(db, rootFolderId);
                report.check(!subject.empty(), "there is a track to write masked updates over");

                if (!subject.empty())
                {
                    const auto original = subject.front();

                    // Everything a scan establishes, deliberately different from what is stored, so
                    // either half writing when it should not is visible.
                    TrackInfo scanned{};
                    scanned.trackId = original.trackId;
                    scanned.folderId = original.folderId;
                    scanned.filename = original.filename;
                    scanned.filesize_bytes = original.filesize_bytes;
                    scanned.last_scanned = std::chrono::system_clock::now();
                    scanned.title = "SelfTest Masked Title";
                    scanned.artist_name = "SelfTest Masked Artist";
                    scanned.duration = Duration_t{1234};
                    scanned.samplerate = 12345;

                    report.check(original.duration != scanned.duration && original.title != scanned.title,
                        "the stored row and the scanned one disagree about both halves, so either can be told apart");

                    // (a) Tags only: the titles move, the audio properties do not.
                    report.check(db.updateScannedTrackData(scanned, ScannedFields::Tags).isOk(), "a tags-only update is accepted");

                    const auto afterTags = db.getTrackById(original.trackId);
                    report.check(afterTags.has_value() && afterTags->title == scanned.title, "a tags-only update writes the title");
                    report.check(afterTags.has_value() && afterTags->duration == original.duration,
                        "and leaves the duration alone - the property read is what failed, and it says nothing");
                    report.check(afterTags.has_value() && afterTags->samplerate == original.samplerate, "nor does it touch the samplerate");

                    // (b) Audio properties only: the reverse.
                    TrackInfo propertiesOnly{scanned};
                    propertiesOnly.title.clear();
                    propertiesOnly.artist_name.clear();
                    report.check(db.updateScannedTrackData(propertiesOnly, ScannedFields::AudioProperties).isOk(),
                        "an audio-properties-only update is accepted");

                    const auto afterProperties = db.getTrackById(original.trackId);
                    report.check(afterProperties.has_value() && afterProperties->duration == scanned.duration,
                        "an audio-properties-only update writes the duration");
                    report.check(afterProperties.has_value() && afterProperties->samplerate == scanned.samplerate, "and the samplerate");
                    report.check(afterProperties.has_value() && afterProperties->title == scanned.title,
                        "and leaves the title that was read earlier alone, rather than blanking it");

                    // (c) Nothing at all still moves the row to where the file is.
                    TrackInfo locationOnly{propertiesOnly};
                    locationOnly.duration = Duration_t{0};
                    locationOnly.samplerate = 0;
                    locationOnly.filesize_bytes = original.filesize_bytes + 1;
                    report.check(db.updateScannedTrackData(locationOnly, ScannedFields::None).isOk(), "an update establishing nothing is still accepted");

                    const auto afterNothing = db.getTrackById(original.trackId);
                    report.check(afterNothing.has_value() && afterNothing->filesize_bytes == original.filesize_bytes + 1,
                        "it writes what the filesystem said, because that much was never in doubt");
                    report.check(afterNothing.has_value() && afterNothing->duration == scanned.duration && afterNothing->title == scanned.title,
                        "and writes none of the metadata it did not read");
                }
            }

            // --- 11b. A scan leaves the folder counts telling the truth. ---
            //
            // trackCount is an aggregate over a folder's descendants, worked out once while the cache is
            // built. The scan writes tracks straight past that cache, and a folder it discovers is added
            // surgically with the -1 that FolderInfo starts life with, so after a scan the cache no
            // longer agrees with the database about what is where.
            //
            // Nothing inside the library corrected that. The one thing that did was the library roots
            // dialog, which invalidated the cache itself after calling scanLibrary - so the tree a user
            // sees was right, and every other caller got stale counts. That is the defect this covers:
            // an invariant of the scan enforced by one of its callers rather than by the scan. It is
            // checked here, at the API, because that is the level it was missing from.
            //
            // Read without invalidating anything first, because that is the state a user is in when they
            // look at the tree after a scan. A check that invalidated first would pass against the defect.
            {
                const auto countedRoot = workRoot / "counted";
                const auto countedDeep = countedRoot / "deep";

                std::error_code countEc;
                const bool built = makeDirectory(countedRoot) && makeDirectory(countedDeep) &&
                                   writeSilentWav(countedDeep / "counted.wav", static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000));
                report.check(built, "a nested folder with one file could be added to the library on disk");

                if (built)
                {
                    const auto rootCountBefore = db.getFolderDatabase().getFolderById(rootFolderId);
                    report.check(rootCountBefore.has_value(), "the root folder's count could be read before the scan");

                    report.check(runScan({rootFolderId}, false, report, "a new nested folder appears"), "the scan that discovers it reports success");

                    // After the scan and before anything else touches the cache.
                    const auto deepId = db.getFolderDatabase().findOrCreateFolderByPath(countedDeep);
                    const auto countedId = db.getFolderDatabase().findOrCreateFolderByPath(countedRoot);
                    report.check(deepId > 0 && countedId > 0, "the scan created rows for both new folders");

                    const auto deep = db.getFolderDatabase().getFolderById(deepId);
                    report.check(deep.has_value() && deep->trackCount == 1,
                        std::format("the folder the file landed in says it holds 1 track (says {})", deep.has_value() ? deep->trackCount : -99));

                    // The count is recursive, so the folder above it sees the same track.
                    const auto counted = db.getFolderDatabase().getFolderById(countedId);
                    report.check(counted.has_value() && counted->trackCount == 1,
                        std::format("and its parent counts it too (says {})", counted.has_value() ? counted->trackCount : -99));

                    const auto rootCountAfter = db.getFolderDatabase().getFolderById(rootFolderId);
                    report.check(rootCountBefore.has_value() && rootCountAfter.has_value() && rootCountAfter->trackCount == rootCountBefore->trackCount + 1,
                        std::format("and the library root is one higher than it was ({} -> {})",
                            rootCountBefore.has_value() ? rootCountBefore->trackCount : -99,
                            rootCountAfter.has_value() ? rootCountAfter->trackCount : -99));
                }

                // --- and a scan that does not run to the end leaves them honest too ---
                //
                // scanLoop has a dozen ways out - a cancellation between two files, a failed write, a
                // root whose folders could not be determined - and every one of them can happen after
                // rows have already changed. So the refresh belongs to the attempt, not to the happy
                // path: it sits in TrackScanner::scan where scanLoop returns, and this is the check
                // that says so. Invalidating at the end of scanLoop instead passes every check above
                // and fails this one.
                //
                // Cancelled rather than failed because it is the exit that can be staged exactly: the
                // cancellation is raised from the progress callback, which fires every hundredth file,
                // so it lands well inside the walk with rows already written. The count below asserts
                // that - an early cancellation that inserted nothing would leave the cache and the
                // database trivially agreeing, and prove none of this.
                {
                    const auto cancelRoot = workRoot / "cancelme";

                    // Enough files to reach the progress callback, which fires every hundredth, and
                    // tiny ones: nothing reads their audio, they only have to be walked.
                    constexpr int kFilesToWalk = 150;
                    bool built = makeDirectory(cancelRoot);
                    for (int i = 0; built && i < kFilesToWalk; ++i)
                    {
                        built = writeSilentWav(cancelRoot / std::format("c{:03}.wav", i), 4410);
                    }
                    report.check(built, std::format("{} small files could be written for the cancelled scan", kFilesToWalk));

                    if (built)
                    {
                        std::atomic<bool> cancel{false};
                        bool scanSaidSuccess = true;
                        int progressReports = 0;
                        std::vector<FolderId> scope{rootFolderId};
                        theTrackLibrary.scanLibrary(
                            scope,
                            false,
                            false,
                            [&cancel, &progressReports](int, const std::string &)
                            {
                                // Not the first one. That is "Initializing scan...", raised before any
                                // root is walked, and cancelling there stops the scan before it creates
                                // anything - which would leave this checking a folder the test made
                                // itself rather than one the scan did. The second is the hundredth file,
                                // by which point the walk is well inside the directory above and its
                                // folder row exists. The next file sees the flag and stops.
                                if (++progressReports >= 2)
                                {
                                    cancel = true;
                                }
                            },
                            [&scanSaidSuccess](bool success, const std::string &)
                            {
                                scanSaidSuccess = success;
                            },
                            &cancel);

                        report.check(!scanSaidSuccess, "a cancelled scan reports failure");

                        const auto cancelFolderId = db.getFolderDatabase().findOrCreateFolderByPath(cancelRoot);
                        report.check(cancelFolderId > 0, "the cancelled scan still created the folder row it walked into");

                        // Whatever the cancellation left behind, the cache has to agree with the
                        // database about it. Counted in SQL rather than through the cache, because the
                        // cache is the thing under test.
                        int64_t rowsInDatabase = -1;
                        {
                            SqliteDatabase counter;
                            if (counter.open(pathToString(databasePath)))
                            {
                                SqliteStatement stmt{counter, "SELECT COUNT(*) FROM Tracks WHERE folder_id = ?;"};
                                if (stmt.isValid() && stmt.addParam(cancelFolderId) && stmt.getNextResult())
                                {
                                    rowsInDatabase = stmt.getInt64(0);
                                }
                            }
                        }
                        // Greater than zero, not merely countable. Tracks being inserted before the
                        // cancellation is what makes this a test rather than a tautology.
                        report.check(rowsInDatabase > 0,
                            std::format("the cancellation landed after rows had already been written ({} in the database)", rowsInDatabase));

                        const auto cancelled = db.getFolderDatabase().getFolderById(cancelFolderId);
                        report.check(cancelled.has_value() && cancelled->trackCount == rowsInDatabase,
                            std::format("and the cache agrees with the database about that folder ({} cached, {} in the database)",
                                cancelled.has_value() ? cancelled->trackCount : -99,
                                rowsInDatabase));
                    }
                }
            }

            // --- 12. A root nobody could look at is not evidence that its files are gone. ---
            //
            // A leftover only means "the walk did not find it", and the walk finds nothing under a root
            // it could not resolve or could not reach. An unplugged drive therefore produces a library
            // full of rows that look exactly like deleted files - and if a copy of one of them turns up
            // under a root that *was* walked, matching on that would hand the track, and every mix that
            // uses it, to the copy while the original sits there on a disk that is merely unplugged.
            //
            // A deleted *folder* under a healthy root is a different thing entirely, and is the ordinary
            // case section 9 covers. This is about the root.
            {
                const auto secondRoot = selfTestRoot / "second-root";
                const auto strandedName = std::string{"stranded.wav"};

                std::error_code secondEc;
                std::filesystem::remove_all(secondRoot, secondEc);
                if (makeDirectory(secondRoot) && writeSilentWav(secondRoot / strandedName, static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
                {
                    const auto addedRoot = db.getLibraryRootManager().addRoot(pathToString(secondRoot));
                    const auto secondRootFolderId = db.getFolderDatabase().findOrCreateFolderByPath(secondRoot);
                    report.check(addedRoot.has_value() && secondRootFolderId > 0, "a second library root could be added");

                    if (addedRoot.has_value() && secondRootFolderId > 0)
                    {
                        report.check(runScan({rootFolderId, secondRootFolderId}, false, report, "two roots, both present"),
                            "the scan across both roots reports success");

                        TrackId strandedId{-1};
                        for (const auto &track : trackRowsUnder(db, secondRootFolderId))
                        {
                            if (track.filename == strandedName)
                            {
                                strandedId = track.trackId;
                            }
                        }
                        report.check(strandedId > 0, "the file under the second root has a row of its own");

                        // The root goes away entirely - the disconnected-drive shape - and an identical
                        // file turns up under the root that is still there.
                        const auto arrivedRoot = workRoot / "arrived";
                        std::error_code setupEc;
                        if (makeDirectory(arrivedRoot))
                        {
                            std::filesystem::copy_file(secondRoot / strandedName, arrivedRoot / strandedName, setupEc);
                        }
                        std::error_code removeEc;
                        std::filesystem::remove_all(secondRoot, removeEc);
                        report.check(!setupEc && !removeEc, "the second root could be taken away with a copy of its file left elsewhere");

                        if (strandedId > 0 && !setupEc && !removeEc)
                        {
                            const auto rowsBefore = static_cast<int>(trackRowsUnder(db, rootFolderId).size());

                            report.check(runScan({rootFolderId, secondRootFolderId}, false, report, "one root gone, its file apparently elsewhere"),
                                "the scan reports success with a root it could not look at");

                            const auto rowsAfter = trackRowsUnder(db, rootFolderId);
                            report.check(static_cast<int>(rowsAfter.size()) == rowsBefore + 1,
                                std::format("the file under the healthy root was inserted as a new track ({} rows, expected {})",
                                    rowsAfter.size(),
                                    rowsBefore + 1));

                            const auto stranded = db.getTrackById(strandedId);
                            report.check(stranded.has_value(), "the row under the unreachable root still exists");
                            report.check(stranded.has_value() && stranded->folderId == secondRootFolderId,
                                "and still names the folder it was in - it was not handed to the copy");

                            // --- and a scan that is allowed to delete does not delete it either ---
                            //
                            // Everything above runs with removeMissingFiles off, so it proves the
                            // stranded row keeps its identity and nothing more. The row was still
                            // reaching the missing branch, which walked every leftover without asking
                            // whether anybody had looked in its folder - so a destructive scan handed it
                            // to removeTracks, and the delete cascaded into mix membership.
                            //
                            // Both directions in one scan, because a guard that simply stopped deleting
                            // would pass the first half on its own: a file that really was deleted, under
                            // the root that is still there, has to go.
                            if (stranded.has_value())
                            {
                                // Put the stranded track in the mix, so the cascade has something to take
                                // if the row is deleted.
                                {
                                    auto mixTracksNow = theTrackLibrary.getMixManager().getMixTracks(mixInfo.mixId);
                                    MixTrack strandedInMix{};
                                    strandedInMix.trackId = strandedId;
                                    strandedInMix.orderInMix = static_cast<int>(mixTracksNow.size());
                                    mixTracksNow.push_back(strandedInMix);
                                    report.check(theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracksNow),
                                        "the stranded track could be added to the mix");
                                }

                                const auto mixHolds = [](MixId mixId, TrackId trackId)
                                {
                                    const auto rows = theTrackLibrary.getMixManager().getMixTracks(mixId);
                                    return std::any_of(rows.begin(),
                                        rows.end(),
                                        [trackId](const MixTrack &row)
                                        {
                                            return row.trackId == trackId;
                                        });
                                };
                                report.check(mixHolds(mixInfo.mixId, strandedId), "and the mix holds it before the destructive scan");

                                // A file under the healthy root that really is deleted, so the same scan
                                // has real work to do.
                                // Whatever is still on disk under the healthy root - reconstructed from
                                // the row rather than assumed, because these fixtures live in a subfolder
                                // and earlier sections have moved and deleted some of them.
                                TrackId reallyGoneId{-1};
                                std::filesystem::path reallyGonePath;
                                for (const auto &row : trackRowsUnder(db, rootFolderId))
                                {
                                    const auto folder = db.reconstructFullPath(row.folderId);
                                    if (folder.empty())
                                    {
                                        continue;
                                    }
                                    const auto onDisk = folder / row.filename;
                                    if (std::filesystem::exists(onDisk))
                                    {
                                        reallyGoneId = row.trackId;
                                        reallyGonePath = onDisk;
                                        break;
                                    }
                                }
                                std::error_code deleteEc;
                                if (reallyGoneId > 0)
                                {
                                    std::filesystem::remove(reallyGonePath, deleteEc);
                                }
                                report.check(reallyGoneId > 0 && !deleteEc,
                                    std::format("a file under the healthy root ('{}') could be deleted for real", pathToString(reallyGonePath)));

                                if (reallyGoneId > 0 && !deleteEc)
                                {
                                    report.check(runScan({rootFolderId, secondRootFolderId}, true, report, "destructive, one root unreachable"),
                                        "a scan that may delete reports success with a root it could not look at");

                                    report.check(!db.getTrackById(reallyGoneId).has_value(),
                                        "the file that really was deleted, under the root that was walked, is gone from the database");

                                    const auto afterDestructive = db.getTrackById(strandedId);
                                    report.check(
                                        afterDestructive.has_value(), "the row under the unreachable root survived a scan that was allowed to delete it");
                                    report.check(afterDestructive.has_value() && !afterDestructive->is_missing,
                                        "and was not marked missing either - nobody looked in its folder");
                                    report.check(mixHolds(mixInfo.mixId, strandedId), "so the mix still holds it, rather than losing it to the delete cascade");
                                }
                            }
                        }
                    }

                    // --- the other shape of unreachable: present, and not listable ---
                    //
                    // Everything above takes the root away, so `is_directory` answers no. A root whose
                    // permissions deny listing answers yes and then enumerates to nothing, which is why
                    // eligibility had to come from opening the directory rather than from its type.
                    // Without that, this root looks walked, and every row under it looks deleted.
                    {
                        const auto lockedRoot = selfTestRoot / "locked-root";
                        const auto lockedName = std::string{"locked.wav"};

                        std::error_code lockedEc;
                        std::filesystem::remove_all(lockedRoot, lockedEc);
                        const bool built =
                            makeDirectory(lockedRoot) && writeSilentWav(lockedRoot / lockedName, static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000));

                        const auto lockedRootInfo = built ? db.getLibraryRootManager().addRoot(pathToString(lockedRoot)) : std::nullopt;
                        const auto lockedFolderId = built ? db.getFolderDatabase().findOrCreateFolderByPath(lockedRoot) : FolderId{-1};
                        report.check(built && lockedRootInfo.has_value() && lockedFolderId > 0, "a third root could be added with a file in it");

                        if (lockedRootInfo.has_value() && lockedFolderId > 0)
                        {
                            report.check(
                                runScan({lockedFolderId}, false, report, "the third root, still listable"), "the scan that discovers it reports success");

                            TrackId lockedId{-1};
                            for (const auto &row : trackRowsUnder(db, lockedFolderId))
                            {
                                if (row.filename == lockedName)
                                {
                                    lockedId = row.trackId;
                                }
                            }
                            report.check(lockedId > 0, "its file has a row");

                            const bool denied = lockedId > 0 && setDirectoryListable(lockedRoot, false);
                            report.check(denied, "the root could be made unlistable while still being a directory");

                            if (denied)
                            {
                                std::error_code stillEc;
                                report.check(std::filesystem::is_directory(lockedRoot, stillEc),
                                    "it still answers is_directory, which is what the old eligibility check asked");

                                report.check(runScan({lockedFolderId}, true, report, "destructive, the third root unlistable"),
                                    "a scan that may delete reports success with a root it could not list");

                                const auto afterLocked = db.getTrackById(lockedId);
                                report.check(afterLocked.has_value(), "the row under the unlistable root survived it");
                                report.check(afterLocked.has_value() && !afterLocked->is_missing, "and was not marked missing either");

                                // Put the permissions back before anything tries to clean up.
                                report.check(setDirectoryListable(lockedRoot, true), "the root could be made listable again");
                            }

                            db.getLibraryRootManager().removeRoot(lockedRootInfo->id);
                        }

                        std::filesystem::remove_all(lockedRoot, lockedEc);
                    }

                    if (addedRoot.has_value())
                    {
                        db.getLibraryRootManager().removeRoot(addedRoot->id);
                    }
                }
            }

            // --- 13. A scan that did not complete is not a completed scan ---
            //
            // "Last Scanned" is a column the user reads, and ScanRootsTask used to stamp it on every
            // selected root whatever the scan returned - so a scan that was cancelled or refused told
            // them the opposite of what happened. The two changes that landed this week made that
            // reachable rather than theoretical: a scan now refuses a scope it could not determine
            // (8b85c54) and skips a root it could not walk (a6d888e).
            //
            // Driven through the real ScanRootsTask::run rather than a copy of its logic, which is
            // why that class is in a header now. A check that made the decision itself would pass
            // whatever run() actually did with the result.
            //
            // Nothing is recorded on failure rather than something partial: scanLibrary reports one
            // result for the whole batch, so a root it finished before the failure cannot be told
            // from one it never reached. Leaving every timestamp alone keeps a date that was true.
            {
                const auto statsRoot = selfTestRoot / "scanstats-root";
                std::error_code statsEc;
                std::filesystem::remove_all(statsRoot, statsEc);

                // Enough to reach the progress callback, which fires every hundredth file, so the
                // cancellation lands inside the walk rather than before it starts.
                constexpr int kStatsFiles = 150;
                bool statsBuilt = makeDirectory(statsRoot);
                for (int i = 0; statsBuilt && i < kStatsFiles; ++i)
                {
                    statsBuilt = writeSilentWav(statsRoot / std::format("s{:03}.wav", i), 4410);
                }
                report.check(statsBuilt, std::format("{} files could be written for the scan-stats checks", kStatsFiles));

                const auto statsRootInfo = statsBuilt ? db.getLibraryRootManager().addRoot(pathToString(statsRoot)) : std::nullopt;
                const auto statsFolderId = statsBuilt ? db.getFolderDatabase().findOrCreateFolderByPath(statsRoot) : FolderId{-1};
                report.check(statsRootInfo.has_value() && statsFolderId > 0, "a library root could be added for the scan-stats checks");

                if (statsRootInfo.has_value() && statsFolderId > 0)
                {
                    auto &rootManager = db.getLibraryRootManager();
                    const auto rootId = statsRootInfo->id;

                    // Reads the stored value back, so every comparison below is between two values
                    // that have been through the database. last_scanned is stored as whole seconds,
                    // and comparing a round-tripped value against an in-memory one would fail on the
                    // rounding rather than on the behaviour.
                    const auto storedScanTime = [&rootManager, rootId]() -> std::optional<Timestamp_t>
                    {
                        for (const auto &root : rootManager.getAllRoots())
                        {
                            if (root.id == rootId)
                            {
                                return root.lastScanned;
                            }
                        }
                        return std::nullopt;
                    };

                    // A known time to watch, an hour old. updateScanStats takes one, so this needs no
                    // waiting and no assumption about how fast the clock ticks.
                    report.check(rootManager.updateScanStats(rootId, std::chrono::system_clock::now() - std::chrono::hours{1}),
                        "the root's last_scanned could be set to a known time");

                    const auto before = storedScanTime();
                    report.check(before.has_value(), "and reads back");

                    // A cancelled scan. The task is built and released the way the component builds
                    // and releases it.
                    {
                        std::atomic<bool> cancel{false};
                        int reports = 0;
                        bool taskSaidSuccess = true;

                        auto *task = new ui::ScanRootsTask{{statsFolderId}, {rootId}, false, false, nullptr};
                        task->run(
                            [&cancel, &reports](int, const std::string &)
                            {
                                // Not the first, which is raised before any root is walked. The
                                // second is the hundredth file, well inside the walk.
                                if (++reports >= 2)
                                {
                                    cancel = true;
                                }
                            },
                            [&taskSaidSuccess](bool success, const std::string &)
                            {
                                taskSaidSuccess = success;
                            },
                            cancel);
                        task->release(REFCOUNT_DEBUG_ARGS);

                        report.check(!taskSaidSuccess, "a cancelled scan reports failure to the task");
                        report.check(reports >= 2, std::format("and was cancelled inside the walk, not before it ({} progress reports)", reports));

                        const auto afterCancel = storedScanTime();
                        report.check(afterCancel == before, "and leaves last_scanned exactly as it was, rather than stamping the attempt");
                    }

                    // And the other half, so the check above cannot pass on a task that never records
                    // anything at all.
                    {
                        std::atomic<bool> noCancel{false};
                        bool taskSaidSuccess = false;

                        auto *task = new ui::ScanRootsTask{{statsFolderId}, {rootId}, false, false, nullptr};
                        task->run(
                            nullptr,
                            [&taskSaidSuccess](bool success, const std::string &)
                            {
                                taskSaidSuccess = success;
                            },
                            noCancel);
                        task->release(REFCOUNT_DEBUG_ARGS);

                        report.check(taskSaidSuccess, "a scan that runs to the end reports success");

                        const auto afterSuccess = storedScanTime();
                        report.check(afterSuccess.has_value() && afterSuccess != before, "and does move last_scanned");
                        report.check(
                            afterSuccess.has_value() && before.has_value() && afterSuccess.value() > before.value(), "forwards, to the time it finished");
                    }

                    // --- and a root the scan skipped is not a scanned root either ---
                    //
                    // The other half, and a different mechanism. A root that is gone, is not a
                    // directory, or cannot be listed is stepped over and the scan still reports
                    // success (TrackScanner.cpp, the two continues in the root loop) - deliberately,
                    // because the roots that were there really were scanned, and failing the whole
                    // run over one unplugged drive was the worse behaviour that a6d888e removed.
                    //
                    // So the check above cannot catch this one: the scan succeeds, and every root in
                    // the batch used to be stamped including the one nobody looked at.
                    {
                        const auto lockedStatsRoot = selfTestRoot / "scanstats-locked";
                        std::error_code lockedEc;
                        std::filesystem::remove_all(lockedStatsRoot, lockedEc);

                        const bool lockedBuilt = makeDirectory(lockedStatsRoot) &&
                                                 writeSilentWav(lockedStatsRoot / "locked.wav", static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000));
                        report.check(lockedBuilt, "a second root could be built for the skipped-root check");

                        const auto lockedRootInfo = lockedBuilt ? rootManager.addRoot(pathToString(lockedStatsRoot)) : std::nullopt;
                        const auto lockedFolderId = lockedBuilt ? db.getFolderDatabase().findOrCreateFolderByPath(lockedStatsRoot) : FolderId{-1};
                        report.check(lockedRootInfo.has_value() && lockedFolderId > 0, "and added as a library root");

                        if (lockedRootInfo.has_value() && lockedFolderId > 0)
                        {
                            const auto lockedId = lockedRootInfo->id;
                            const auto marker = std::chrono::system_clock::now() - std::chrono::hours{2};
                            report.check(rootManager.updateScanStats(rootId, marker) && rootManager.updateScanStats(lockedId, marker),
                                "both roots could be given the same known last_scanned");

                            const auto storedFor = [&rootManager](LibraryRootId id) -> std::optional<Timestamp_t>
                            {
                                for (const auto &root : rootManager.getAllRoots())
                                {
                                    if (root.id == id)
                                    {
                                        return root.lastScanned;
                                    }
                                }
                                return std::nullopt;
                            };

                            const auto healthyBefore = storedFor(rootId);
                            const auto skippedBefore = storedFor(lockedId);

                            const bool denied = setDirectoryListable(lockedStatsRoot, false);
                            report.check(denied, "the second root could be made unlistable");

                            if (denied)
                            {
                                std::atomic<bool> noCancel{false};
                                bool taskSaidSuccess = false;

                                auto *task = new ui::ScanRootsTask{{statsFolderId, lockedFolderId}, {rootId, lockedId}, false, false, nullptr};
                                task->run(
                                    nullptr,
                                    [&taskSaidSuccess](bool success, const std::string &)
                                    {
                                        taskSaidSuccess = success;
                                    },
                                    noCancel);
                                task->release(REFCOUNT_DEBUG_ARGS);

                                report.check(taskSaidSuccess, "a scan that skips an unlistable root still reports success");

                                const auto healthyAfter = storedFor(rootId);
                                report.check(healthyAfter.has_value() && healthyAfter != healthyBefore, "the root that was walked has its last_scanned moved");

                                const auto skippedAfter = storedFor(lockedId);
                                report.check(skippedAfter == skippedBefore, "and the root nobody could look at keeps the date it had");

                                // Before anything tries to clean up.
                                report.check(setDirectoryListable(lockedStatsRoot, true), "the second root could be made listable again");
                            }

                            rootManager.removeRoot(lockedId);
                        }

                        std::filesystem::remove_all(lockedStatsRoot, lockedEc);
                    }

                    rootManager.removeRoot(rootId);
                }

                // One of each shape of LongRunningTask:: record, for scripts/refcountd to read back.
                //
                // This is a producer, not a check - the assertion is on the other side of the format
                // boundary, in the tool that parses what this writes. A retain and a release are the
                // two records that carry a count, and the middle release is the non-final one, which
                // logs at a different level from the last. Costs nothing when the instrumentation is
                // off, which is how it ships: the logging is inside the ifdef and what remains is
                // three atomic operations.
                //
                // See the README in scripts/refcountd for how to run the two together.
                {
                    auto *probe = new ui::ScanRootsTask{{}, {}, false, false, nullptr};
                    probe->retain(REFCOUNT_DEBUG_ARGS);
                    probe->release(REFCOUNT_DEBUG_ARGS);
                    probe->release(REFCOUNT_DEBUG_ARGS);
                    report.note("wrote one retain and two releases from a long-running task, for scripts/refcountd");
                }

                std::filesystem::remove_all(statsRoot, statsEc);
            }

            // --- Where a mix's tracks sit does not depend on whether they can be resolved ---
            //
            // The one property that makes every walk over a mix agree. The timeline, the playback
            // engine, both exporters and the stored length all position tracks from the mix's own
            // attach points, and they now do it through calculateMixTrackStarts - so a track on a
            // disconnected drive leaves a silent gap where it belongs rather than pulling everything
            // after it earlier in some of those places and not others.
            //
            // Over the walk directly, with no database: it is a pure function of a track list, and the
            // five callers agree by sharing it rather than by each being checked to behave alike.
            {
                const auto ms = [](int64_t value)
                {
                    return Duration_t{value};
                };

                // Three tracks, each handing over 1000 ms before it ends, with distinct attach points
                // so an off-by-one in the chain cannot look correct by symmetry.
                std::vector<MixTrack> tracks(3);
                tracks[0].trackId = 1;
                tracks[0].attachTo = ms(9000);
                tracks[1].trackId = 2;
                tracks[1].attachFrom = ms(1000);
                tracks[1].attachTo = ms(19000);
                tracks[2].trackId = 3;
                tracks[2].attachFrom = ms(2000);
                tracks[2].attachTo = ms(29000);

                // 0, then 0 + 9000 - 1000, then 8000 + 19000 - 2000.
                const std::vector<Duration_t> expectedStarts{ms(0), ms(8000), ms(25000)};
                report.check(database::calculateMixTrackStarts(tracks) == expectedStarts,
                    std::format("the ATTACH chain places three tracks at 0, 8000, 25000 ms (got {}, {}, {})",
                        database::calculateMixTrackStarts(tracks)[0].count(),
                        database::calculateMixTrackStarts(tracks)[1].count(),
                        database::calculateMixTrackStarts(tracks)[2].count()));

                // Durations of 10, 20 and 30 seconds, so the ends are 10000, 28000 and 55000.
                const auto lengthOf = [](TrackId trackId) -> std::optional<Duration_t>
                {
                    return Duration_t{trackId * 10000};
                };
                report.check(database::calculateMixDuration(tracks, lengthOf) == ms(55000),
                    std::format("the mix is as long as its last track's end ({} ms, expected 55000)",
                        database::calculateMixDuration(tracks, lengthOf).count()));

                // The middle track offline. Its position is unchanged and so is the third's - which is
                // the whole point, and what used to differ between the walk, the engine and the
                // exporter. Only the end it would have contributed goes.
                const auto lengthWithoutTheMiddle = [](TrackId trackId) -> std::optional<Duration_t>
                {
                    if (trackId == 2)
                    {
                        return std::nullopt;
                    }
                    return Duration_t{trackId * 10000};
                };

                report.check(database::calculateMixTrackStarts(tracks) == expectedStarts,
                    "an unresolvable track moves nothing: the starts come from the mix, not from the audio");
                report.check(database::calculateMixDuration(tracks, lengthWithoutTheMiddle) == ms(55000),
                    std::format("and the mix is still as long, because the last track has not moved ({} ms, expected 55000)",
                        database::calculateMixDuration(tracks, lengthWithoutTheMiddle).count()));

                // The last track offline instead, which is the case where the total really does change:
                // 55000 was its end, and the longest remaining end is the middle track's 28000.
                const auto lengthWithoutTheLast = [](TrackId trackId) -> std::optional<Duration_t>
                {
                    if (trackId == 3)
                    {
                        return std::nullopt;
                    }
                    return Duration_t{trackId * 10000};
                };
                report.check(database::calculateMixDuration(tracks, lengthWithoutTheLast) == ms(28000),
                    std::format("losing the last track shortens the mix to what is left ({} ms, expected 28000)",
                        database::calculateMixDuration(tracks, lengthWithoutTheLast).count()));

                // A track whose length was never determined is not the same as one that cannot be
                // resolved: it resolves, to zero, and still ends where it starts.
                const auto zeroLengthMiddle = [](TrackId trackId) -> std::optional<Duration_t>
                {
                    if (trackId == 2)
                    {
                        return Duration_t{0};
                    }
                    return Duration_t{trackId * 10000};
                };
                report.check(database::calculateMixDuration(tracks, zeroLengthMiddle) == ms(55000),
                    "a track that resolves to zero length is not treated as unresolvable");

                report.check(database::calculateMixTrackStarts({}).empty(), "an empty mix has no positions");
                report.check(database::calculateMixDuration(std::vector<MixTrack>{}, lengthOf) == ms(0), "and no length");
            }

            // --- The BPM picker reads a whole track row, and reads it by position ---
            //
            // getNextTrackForBpmAnalysis is the one such read nothing else here exercises, and it is
            // the one where a mistake is worst: BpmAnalysis writes the result back keyed by the
            // track_id this decodes, so a column list out of step with trackInfoFromStatement would
            // file one track's BPM against another track's id. Both of its queries are covered, because
            // they are written differently - the first joins MixTracks and so has to qualify every
            // column, the second does not.
            //
            // What each field is compared against is read back out of the database by name, in a
            // separate query, which is the comparison that matters: not "did it return a track" but
            // "did this value land in the field SQL says it belongs to".
            //
            // Four fields rather than all twenty-eight, chosen for where they sit in the row - folder_id
            // and filename near the front, title in the middle, status last. A list that is wrong by one
            // shifts everything after the mistake, so a sample spread across the row catches it wherever
            // it is, while comparing every field would only say the same thing at more length.
            {
                auto &tracks = theTrackLibrary.getTrackDatabase();

                SqliteDatabase probe;
                if (!probe.open(pathToString(databasePath)))
                {
                    report.check(false, "a probe connection could be opened for the BPM picker check");
                }
                else
                {
                    const auto scalar = [&probe](const std::string &sql)
                    {
                        SqliteStatement stmt{probe, sql};
                        return (stmt.isValid() && stmt.getNextResult()) ? stmt.getText(0) : std::string{"<query failed>"};
                    };

                    // Nothing left to analyse, so the picker has to come back empty. Without this the
                    // checks below would pass on a picker that simply returned the first track it saw.
                    report.check(probe.execute("UPDATE Tracks SET bpm = 120;"), "every scratch track could be marked as analysed");
                    report.check(!tracks.getNextTrackForBpmAnalysis().has_value(), "with every track analysed the BPM picker offers nothing");

                    // Priority 1: a track that is in a mix. This is the joined query.
                    const auto inMixId = scalar("SELECT t.track_id FROM Tracks t JOIN MixTracks m ON t.track_id = m.track_id "
                                                "ORDER BY t.track_id LIMIT 1");
                    report.check(inMixId != "<query failed>" && inMixId != "",
                        std::format("the scratch library has a track in a mix to offer the picker (id '{}')", inMixId));

                    if (inMixId != "<query failed>" && !inMixId.empty())
                    {
                        report.check(probe.execute(std::format("UPDATE Tracks SET bpm = NULL WHERE track_id = {};", inMixId)),
                            "that track could be marked as un-analysed");

                        const auto offered = tracks.getNextTrackForBpmAnalysis();
                        report.check(offered.has_value() && std::to_string(offered->trackId) == inMixId,
                            std::format("the picker offers the un-analysed track that is in a mix (offered '{}', expected '{}')",
                                offered.has_value() ? std::to_string(offered->trackId) : std::string{"nothing"},
                                inMixId));

                        if (offered.has_value())
                        {
                            // Three fields from three different places in the row - near the front, the
                            // middle and the end - because a list that is wrong by one shifts some
                            // fields and not others.
                            report.check(offered->filename == scalar(std::format("SELECT filename FROM Tracks WHERE track_id = {};", inMixId)),
                                "the filename it decoded is the one that row holds");
                            report.check(offered->title == scalar(std::format("SELECT title FROM Tracks WHERE track_id = {};", inMixId)),
                                "and so is the title");
                            report.check(std::to_string(offered->folderId) ==
                                    scalar(std::format("SELECT folder_id FROM Tracks WHERE track_id = {};", inMixId)),
                                "and the folder it points at");
                            // The last column, and the one that would catch a list too long or too
                            // short. Compared against what the row holds through the same mapping the
                            // decoder uses, rather than against a pair of acceptable values - "either
                            // of two" would have passed on a status that decoded from the wrong column.
                            const auto statusText = scalar(std::format("SELECT status FROM Tracks WHERE track_id = {};", inMixId));
                            const auto expectedStatus = (statusText == "ok") ? TrackStatus::Ok
                                : (statusText == "bad_format")               ? TrackStatus::BadFormat
                                                                             : TrackStatus::Unknown;
                            report.check(offered->status == expectedStatus,
                                std::format("the status column decoded into the status field (got {}, row says '{}')",
                                    static_cast<int>(offered->status),
                                    statusText));
                        }

                        // Priority 2: nothing in a mix needs analysing, but something outside one does.
                        report.check(probe.execute(std::format("UPDATE Tracks SET bpm = 120 WHERE track_id = {};", inMixId)),
                            "the mixed track could be marked as analysed again");

                        const auto outsideId = scalar("SELECT track_id FROM Tracks WHERE track_id NOT IN (SELECT track_id FROM MixTracks) "
                                                      "ORDER BY track_id LIMIT 1");
                        report.check(outsideId != "<query failed>" && !outsideId.empty(),
                            std::format("the scratch library has a track outside every mix (id '{}')", outsideId));

                        if (outsideId != "<query failed>" && !outsideId.empty())
                        {
                            report.check(probe.execute(std::format("UPDATE Tracks SET bpm = NULL WHERE track_id = {};", outsideId)),
                                "that track could be marked as un-analysed");

                            const auto second = tracks.getNextTrackForBpmAnalysis();
                            report.check(second.has_value() && std::to_string(second->trackId) == outsideId,
                                std::format("the picker falls through to a track in no mix (offered '{}', expected '{}')",
                                    second.has_value() ? std::to_string(second->trackId) : std::string{"nothing"},
                                    outsideId));
                            if (second.has_value())
                            {
                                report.check(second->filename == scalar(std::format("SELECT filename FROM Tracks WHERE track_id = {};", outsideId)),
                                    "with the filename that row holds");
                                report.check(second->title == scalar(std::format("SELECT title FROM Tracks WHERE track_id = {};", outsideId)),
                                    "and its title");
                            }
                        }
                    }
                }
            }

            writeResultsFile(resultsPath, "jucyaudio scan self test", report);
            spdlog::info("[SelfTest] Finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runMixRecoverySelfTest(const std::filesystem::path &selfTestRoot, const std::filesystem::path &databasePath)
        {
            Report report;
            // Its own library, separate from the scan suite's: this one deletes tracks and a mix as part
            // of what it asserts, and the two must not be able to disturb each other.
            const auto workRoot = selfTestRoot / "recovery-library";
            const auto exportPath = selfTestRoot / "recovery-export.wav";
            const auto resultsPath = selfTestRoot / "mixrecovery-results.txt";

            spdlog::info("[SelfTest] Starting mix recovery self test. Root: {}", pathToString(selfTestRoot));

            std::error_code ec;
            std::filesystem::remove_all(workRoot, ec);
            if (ec)
            {
                report.abort(std::format("Could not clear {}: {}", pathToString(workRoot), ec.message()));
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }
            std::filesystem::remove(exportPath, ec);

            const auto albumPath = workRoot / kAlbumFolder;
            std::filesystem::create_directories(albumPath, ec);
            if (ec)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(albumPath), ec.message()));
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }

            for (int i = 1; i <= kTrackCount; ++i)
            {
                const auto file = albumPath / std::format("rec{:02}.wav", i);
                if (!writeSilentWav(file, static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
                {
                    report.abort(std::format("Could not write the fixture {}", pathToString(file)));
                    writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                    return 1;
                }
            }

            auto &db = theTrackLibrary.getTrackDatabase();
            auto &mixManager = theTrackLibrary.getMixManager();

            if (!db.getLibraryRootManager().addRoot(pathToString(workRoot)).has_value())
            {
                report.abort("Could not add the recovery scratch library as a library root.");
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }

            const auto rootFolderId = db.getFolderDatabase().findOrCreateFolderByPath(workRoot);
            if (rootFolderId <= 0 || !runScan({rootFolderId}, false, report, "recovery fixture discovery"))
            {
                report.abort("Could not discover the recovery scratch library.");
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }

            const auto tracks = tracksUnder(db, rootFolderId);
            if (static_cast<int>(tracks.size()) != kTrackCount)
            {
                report.abort(std::format("Expected {} fixture tracks, found {}.", kTrackCount, tracks.size()));
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }

            // --- 1. A mix with settings worth losing ---

            // Non-default cue points and gain, so mix_data is something specific rather than an empty
            // shell. A verbatim-copy assertion against defaults would pass even if the copy were wrong.
            std::vector<MixTrack> mixTracks;
            std::vector<TrackId> orderedTrackIds;
            for (const auto &entry : tracks)
            {
                MixTrack mixTrack{};
                mixTrack.trackId = entry.second.trackId;
                mixTrack.orderInMix = static_cast<int>(mixTracks.size());
                mixTrack.cueStart = Duration_t{10 * (mixTrack.orderInMix + 1)};
                mixTrack.cueEnd = Duration_t{kFixtureDurationMs};
                mixTrack.gainAdjustment = 0.5f + (0.1f * static_cast<float>(mixTrack.orderInMix));
                orderedTrackIds.push_back(mixTrack.trackId);
                mixTracks.push_back(mixTrack);
            }

            MixInfo mixInfo{};
            mixInfo.name = "SelfTest Recovery Mix";
            mixInfo.totalDuration = Duration_t{kTrackCount * kFixtureDurationMs};
            if (!mixManager.createOrUpdateMix(mixInfo, mixTracks) || mixInfo.mixId <= 0)
            {
                report.abort("Could not create the recovery test mix.");
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }
            report.note(std::format("created mix {} with {} tracks", mixInfo.mixId, mixTracks.size()));

            const auto liveMixTracks = mixManager.getMixTracks(mixInfo.mixId);

            // --- 2. Export it for real, and check nothing complained ---

            audio::ActiveExportSettings settings{};
            settings.outputPath = exportPath;

            const audio::MixExporter exporter{};
            const auto exportResult = exporter.exportMixToFile(mixInfo.mixId, settings, nullptr);

            report.check(exportResult.success, "exporting the mix to WAV succeeds");
            report.check(exportResult.recoveryWarning.empty(),
                std::format("the export reports no recovery warning (got: '{}')", exportResult.recoveryWarning));
            report.check(std::filesystem::exists(exportPath, ec), "the WAV file was written");

            // --- 3. What was recorded matches what was exported ---

            std::vector<MixRecoveryEntry> recorded;
            report.check(mixManager.getRecoveryData(mixInfo.mixId, recorded).isOk(), "recovery data reads back without error");
            report.check(static_cast<int>(recorded.size()) == kTrackCount,
                std::format("{} recovery rows were written (found {})", kTrackCount, recorded.size()));

            // Asserted in its own right rather than used as a silent gate. It is a separate prerequisite,
            // and letting it skip the position and id checks would turn one failure into no failures.
            report.check(static_cast<int>(liveMixTracks.size()) == kTrackCount,
                std::format("the live mix still lists {} tracks (found {})", kTrackCount, liveMixTracks.size()));

            if (static_cast<int>(recorded.size()) == kTrackCount)
            {
                bool positionsOk = true;
                bool idsOk = true;
                for (size_t i = 0; i < recorded.size(); ++i)
                {
                    positionsOk = positionsOk && recorded[i].orderInMix == static_cast<int>(i) && recorded[i].mixId == mixInfo.mixId;
                    idsOk = idsOk && recorded[i].trackId == orderedTrackIds[i];
                }
                report.check(positionsOk, "recovery rows carry contiguous positions and the right mix id");
                report.check(idsOk, "recovery rows carry the same track ids, in the same order, as the mix");

                // Stored with the record rather than fetched from the live mix when needed. A playlist
                // whose tracks come from the record and whose length comes from the current mix would be
                // describing two different mixes at once.
                const bool durationOk = std::all_of(recorded.begin(),
                    recorded.end(),
                    [&mixInfo](const MixRecoveryEntry &entry)
                    {
                        return entry.mixTotalDuration == mixInfo.totalDuration;
                    });
                report.check(durationOk, "every recovery row remembers how long the whole mix was");
            }

            if (recorded.size() == liveMixTracks.size())
            {
                bool settingsOk = true;
                for (size_t i = 0; i < recorded.size(); ++i)
                {
                    MixTrack fromRecord{};
                    if (!recorded[i].mixData.empty())
                    {
                        try
                        {
                            json::parse(recorded[i].mixData).get_to(fromRecord);
                        }
                        catch (const std::exception &e)
                        {
                            report.note(std::format("recovery row {} has unparseable mix_data: {}", i, e.what()));
                            settingsOk = false;
                            continue;
                        }
                    }

                    const auto &live = liveMixTracks[i];
                    settingsOk = settingsOk && fromRecord.cueStart == live.cueStart && fromRecord.cueEnd == live.cueEnd &&
                                 fromRecord.attachFrom == live.attachFrom && fromRecord.attachTo == live.attachTo &&
                                 fromRecord.gainAdjustment == live.gainAdjustment && fromRecord.envelopePoints == live.envelopePoints;
                }
                report.check(settingsOk, "recorded mix_data round-trips to the same cue points, attach points, gain and envelope");
            }

            // --- 3b. mix_data is stored verbatim, not re-serialised ---

            {
                // An unknown field is staged directly into MixTracks, then the mix is exported again
                // through the public API. A capture that parsed and re-serialised on the way in would
                // drop the field, because to_json does not know about it; a verbatim copy keeps it.
                const auto injected = injectUnknownFieldIntoMixData(databasePath, mixInfo.mixId, 0);
                report.check(!injected.empty(), "an unknown JSON field could be staged into the live mix_data");

                if (!injected.empty())
                {
                    const auto reExport = exporter.exportMixToFile(mixInfo.mixId, settings, nullptr);
                    report.check(reExport.success && reExport.recoveryWarning.empty(),
                        std::format("re-exporting after the injection succeeds and records cleanly (warning: '{}')", reExport.recoveryWarning));

                    std::vector<MixRecoveryEntry> afterInjection;
                    std::ignore = mixManager.getRecoveryData(mixInfo.mixId, afterInjection);
                    report.check(!afterInjection.empty() && afterInjection.front().mixData == injected,
                        "mix_data was stored byte for byte, keeping a field the parser does not know");
                }
            }

            // Re-read: the record now describes the injected state, and later checks compare against it.
            recorded.clear();
            std::ignore = mixManager.getRecoveryData(mixInfo.mixId, recorded);

            // --- 3c. The companion m3u sits beside the audio and says what it should ---

            {
                const auto companionPath = audio::companionM3UPathFor(exportPath);

                report.check(std::filesystem::exists(companionPath, ec), "a companion m3u was written beside the audio file");
                // A stray temporary next to the real file looks like a half-written recovery artefact,
                // which is worse than none because someone would trust it.
                report.check(strayTempCount(companionPath.parent_path()) == 0, "no temporary was left behind");

                // Scoped so the handle is closed before the re-export below. Windows ReplaceFile refuses
                // to replace a file that anyone still has open, so a reader left dangling here makes the
                // production code look broken when it is behaving correctly.
                std::string text;
                {
                    std::ifstream in{companionPath, std::ios::binary};
                    text.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
                }

                report.check(text.starts_with("#EXTM3U\n"), "the companion starts with the standard header");
                for (const auto *tag : {"#EXTMIX:", "#EXTMIXDURATION:", "#EXTINF:", "#JAALBUM:", "#JASTART:", "#JADURATION:", "#JASIZE:", "#JATRACKID:"})
                {
                    report.check(text.find(tag) != std::string::npos, std::format("the companion carries {}", tag));
                }

                // Presence is not enough. The application sets a global locale with thousands
                // separators, so a value that goes out through operator<< instead of std::format comes
                // out as "20,757" - present, well-formed to the eye, and parsed as 20 by anything
                // reading it as a number. Every field below is a bare integer by definition, so the
                // whole value up to the newline must be digits.
                for (const auto *tag : {"#EXTMIXDURATION:", "#JASTART:", "#JADURATION:", "#JASIZE:", "#JATRACKID:"})
                {
                    const std::string tagText{tag};
                    bool allNumeric = true;
                    std::string offender;
                    for (size_t at = text.find(tagText); at != std::string::npos; at = text.find(tagText, at + 1))
                    {
                        const auto valueAt = at + tagText.size();
                        const auto lineEnd = text.find('\n', valueAt);
                        auto value = text.substr(valueAt, lineEnd - valueAt);

                        // #JASTART alone may be negative, and one mix in the library is: its first
                        // track cues 482 ms before the file begins, so its audible content starts
                        // before the mix does. A sign is part of the number here; a comma is not.
                        if (tagText == "#JASTART:" && value.starts_with('-'))
                        {
                            value.erase(0, 1);
                        }

                        if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
                        {
                            allNumeric = false;
                            offender = value;
                            break;
                        }
                    }
                    report.check(allNumeric, std::format("every {} value is digits only (found \"{}\")", tag, offender));
                }

                // One #EXTINF per track, so nothing was dropped or duplicated.
                size_t extinfCount = 0;
                for (size_t at = text.find("#EXTINF:"); at != std::string::npos; at = text.find("#EXTINF:", at + 1))
                {
                    ++extinfCount;
                }
                report.check(extinfCount == static_cast<size_t>(kTrackCount),
                    std::format("the companion lists {} tracks (found {})", kTrackCount, extinfCount));

                // Written binary, so the bytes are the bytes: no CRLF translation on the way out.
                report.check(text.find("\r\n") == std::string::npos, "the companion has no CRLF - it was written as raw bytes");

                // Re-export replaces it rather than appending to or corrupting it. The result is checked
                // rather than discarded: a failed export would leave the previous file untouched, so the
                // size comparison below would pass while proving nothing.
                const auto sizeBefore = std::filesystem::file_size(companionPath, ec);
                const auto replaceExport = exporter.exportMixToFile(mixInfo.mixId, settings, nullptr);
                report.check(replaceExport.success && replaceExport.recoveryWarning.empty(),
                    std::format("the replacement re-export succeeds and records cleanly (warning: '{}')", replaceExport.recoveryWarning));

                // Only meaningful because the export above was asserted to have succeeded: a failed
                // replacement leaves the previous file untouched, so this comparison would pass while
                // proving the opposite of what it claims.
                const auto sizeAfter = std::filesystem::file_size(companionPath, ec);
                report.check(sizeBefore == sizeAfter, "re-exporting replaces the companion rather than growing it");
                report.check(strayTempCount(companionPath.parent_path()) == 0, "re-exporting leaves no temporary behind either");
            }

            // That export captured again, so the rows carry a fresh capturedAt. Everything below compares
            // against `recorded` to prove nothing changed it, so it has to describe the state as of now -
            // otherwise those checks would be comparing against a record this test itself superseded.
            recorded.clear();
            report.check(mixManager.getRecoveryData(mixInfo.mixId, recorded).isOk(), "the record re-reads after the replacement export");
            report.check(static_cast<int>(recorded.size()) == kTrackCount,
                std::format("the record still has {} rows after re-export (found {})", kTrackCount, recorded.size()));

            // --- 3d. A record from before the mix length was stored ---

            {
                // Rows written under v27 have no total_duration, and the migration correctly leaves them
                // NULL. Staged here the same way the unknown JSON field was, because nothing reachable
                // through the public interfaces can produce a NULL any more.
                //
                // The failure this guards against is a confident nought: reading NULL as zero and then
                // printing #EXTMIXDURATION:0 would tell a reader that a two-hour mix is empty.
                const bool nulled = setRecoveryDurationToNull(databasePath, mixInfo.mixId);
                report.check(nulled, "a v27-style row with no recorded mix length could be staged");

                if (nulled)
                {
                    std::vector<MixRecoveryEntry> legacy;
                    report.check(mixManager.getRecoveryData(mixInfo.mixId, legacy).isOk(), "a record with no mix length still reads");
                    report.check(!legacy.empty() && !legacy.front().mixTotalDuration.has_value(),
                        "an unrecorded mix length reads back as unknown, not as zero");

                    if (!legacy.empty())
                    {
                        const auto legacyPath = selfTestRoot / "legacy-duration.m3u";
                        report.check(audio::writeMixRecoveryM3U(legacyPath, legacy, legacy.front().mixTotalDuration).empty(),
                            "a playlist can still be written from a record with no mix length");

                        std::string legacyText;
                        {
                            std::ifstream in{legacyPath, std::ios::binary};
                            legacyText.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
                        }
                        report.check(legacyText.find("#EXTMIXDURATION") == std::string::npos,
                            "the playlist omits the duration line rather than claiming zero");
                        report.check(legacyText.find("#EXTINF:") != std::string::npos, "the playlist is otherwise complete");
                    }
                }
            }

            // Put the mix back the way it was, so the checks below compare against a full record.
            {
                const auto reExport = exporter.exportMixToFile(mixInfo.mixId, settings, nullptr);
                report.check(reExport.success && reExport.recoveryWarning.empty(), "re-exporting restores a record with a known mix length");
                recorded.clear();
                std::ignore = mixManager.getRecoveryData(mixInfo.mixId, recorded);
            }

            // --- 4. A capture that does not match what was rendered is refused ---

            {
                auto doctored = liveMixTracks;
                if (!doctored.empty())
                {
                    doctored.front().cueStart += Duration_t{500};
                }

                MixRecoveryCapture capture;
                const auto captureResult = mixManager.captureRecoveryData(mixInfo.mixId, capture, &doctored);
                report.check(captureResult.isOk(), "a mismatched capture completes without error");
                report.check(!capture.captured, "a capture is refused when the mix no longer matches what was rendered");
                report.note(std::format("refusal said: {}", capture.skipReason));

                std::vector<MixRecoveryEntry> afterRefusal;
                std::ignore = mixManager.getRecoveryData(mixInfo.mixId, afterRefusal);
                // Every field, not just the count: a rewrite producing the same number of rows would slip
                // past a size check. capturedAt alone would catch it, since it is stamped afresh on every
                // capture, but comparing the lot costs nothing and says what is meant.
                report.check(sameRecord(recorded, afterRefusal), "a refused capture leaves the previous record identical, field for field");
            }

            // --- 5. Losing a track: the record survives, and cannot be overwritten by the damage ---

            // Deleting the track cascades its MixTracks row away, which is the exact damage that started
            // all of this. It leaves a gap in order_in_mix, because nothing renumbers on this path.
            const auto victimId = orderedTrackIds.front();
            if (!db.removeTracks({victimId}).isOk())
            {
                report.abort(std::format("Could not delete track {} to simulate the damage.", victimId));
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }
            report.note(std::format("deleted track {} from the library", victimId));

            report.check(mixManager.getMixTracks(mixInfo.mixId).size() == static_cast<size_t>(kTrackCount - 1),
                "deleting a track cascades its row out of the mix");

            std::vector<MixRecoveryEntry> afterTrackDelete;
            report.check(mixManager.getRecoveryData(mixInfo.mixId, afterTrackDelete).isOk(), "recovery data still reads after a track was deleted");
            report.check(sameRecord(recorded, afterTrackDelete),
                std::format("the recovery record survives the deletion of its tracks unchanged (expected {} rows, found {})",
                    recorded.size(),
                    afterTrackDelete.size()));

            {
                MixRecoveryCapture capture;
                const auto captureResult = mixManager.captureRecoveryData(mixInfo.mixId, capture);
                report.check(captureResult.isOk(), "capturing a damaged mix completes without error");
                report.check(!capture.captured, "a damaged mix is refused rather than captured");
                report.note(std::format("refusal said: {}", capture.skipReason));

                std::vector<MixRecoveryEntry> afterRefusal;
                std::ignore = mixManager.getRecoveryData(mixInfo.mixId, afterRefusal);
                report.check(sameRecord(recorded, afterRefusal),
                    "the complete record survives the damaged mix unchanged, field for field - this is the whole point");
            }

            // --- 5b. A damaged mix may be recorded as partial, but never over a good record ---
            //
            // The mixes this exists for lost rows before recovery data existed, and their missing
            // tracks are in no surviving backup. Refusing them forever left the mixes most at risk
            // as the only ones with nothing written down at all.
            //
            // The order of these two checks is the point. Allowing a partial capture must not weaken
            // the rule it sits next to: a mix that has already lost tracks must not be able to
            // overwrite a complete description of itself with a shorter one.
            {
                MixRecoveryCapture overwrite;
                const auto blocked = mixManager.captureRecoveryData(mixInfo.mixId, overwrite, nullptr, RecoveryCaptureMode::AllowIncomplete);
                report.check(blocked.isOk(), "a partial capture over an existing record completes without error");
                report.check(!overwrite.captured, "a partial capture is refused when a complete record already exists");
                report.note(std::format("refusal said: {}", overwrite.skipReason));

                std::vector<MixRecoveryEntry> stillThere;
                std::ignore = mixManager.getRecoveryData(mixInfo.mixId, stillThere);
                report.check(sameRecord(recorded, stillThere), "the complete record is untouched by the refused partial capture");

                // With no record in the way, the same mix records what survives.
                report.check(clearRecoveryData(databasePath, mixInfo.mixId), "the record could be cleared to leave the mix unprotected");

                // A second deletion, from the middle of what is left. The first victim was the track
                // at position 0, so the survivors run 1, 2, 3 - a gap at the front only, which never
                // falls between two recorded tracks. Removing the one at position 2 leaves 1 and 3,
                // and a hole between two rows that are both in the record is what the playlist note
                // below has to describe.
                report.check(db.removeTracks({orderedTrackIds[2]}).isOk(), "a second track could be deleted, from the middle");

                MixRecoveryCapture partial;
                const auto partialResult = mixManager.captureRecoveryData(mixInfo.mixId, partial, nullptr, RecoveryCaptureMode::AllowIncomplete);
                report.check(partialResult.isOk() && partial.captured, "a damaged mix with no record is captured when partial records are allowed");
                report.check(partial.incomplete, "the capture reports itself as partial");
                report.check(partial.entries.size() == static_cast<size_t>(kTrackCount - 2),
                    std::format("it records the {} rows that survived (recorded {})", kTrackCount - 2, partial.entries.size()));

                std::vector<MixRecoveryEntry> partialRead;
                report.check(mixManager.getRecoveryData(mixInfo.mixId, partialRead).isOk(), "the partial record reads back");
                report.check(!partialRead.empty() && std::none_of(partialRead.begin(),
                                                        partialRead.end(),
                                                        [](const MixRecoveryEntry &entry) { return entry.isComplete; }),
                    "every row of it is marked incomplete, so nothing reading it can mistake it for the whole mix");

                // Both positions, because they are different things and only one of them can be
                // reconstructed later. The record position is where the row sits here; the source
                // position is where the mix said the track was, and the jumps in it are the only
                // surviving evidence of what went missing and whereabouts.
                bool recordPositionsRun = !partialRead.empty();
                for (size_t i = 0; recordPositionsRun && i < partialRead.size(); ++i)
                {
                    recordPositionsRun = partialRead[i].orderInMix == static_cast<int>(i);
                }
                report.check(recordPositionsRun, "the record positions run 0..N-1");

                // Positions 0 and 2 were deleted, so the survivors sat at 1 and 3 in the mix. Both
                // gaps have to still be visible in the record: one before the first survivor, one
                // between the two.
                std::vector<int> sourcePositions;
                for (const auto &entry : partialRead)
                {
                    sourcePositions.push_back(entry.sourceOrderInMix.value_or(-1));
                }
                report.check(sourcePositions == std::vector<int>{1, 3},
                    std::format("the mix's own positions are kept, gaps and all (found {})", idsToText(sourcePositions)));

                // And the artefact meant to be read by a person says so too.
                //
                // Guarded, because front() on an empty vector is undefined behaviour and a test that
                // crashes is worse than one that fails: the run dies here and every check after this
                // point goes unreported, including the export refusal below.
                if (!partialRead.empty())
                {
                    const auto partialPath = selfTestRoot / "partial-record.m3u";
                    report.check(audio::writeMixRecoveryM3U(partialPath, partialRead, partialRead.front().mixTotalDuration).empty(),
                        "a playlist can be written from a partial record");

                    std::ifstream in{partialPath, std::ios::binary};
                    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
                    report.check(text.find("#EXTMIXINCOMPLETE:1") != std::string::npos, "the playlist declares itself incomplete");
                    report.check(text.find("WARNING") != std::string::npos, "and says so in words, for whoever opens it in an editor");

                    // The point of keeping the source positions: not "a track is missing" but where.
                    // Both holes, and by exact range - a substring that only proves some annotation
                    // exists would pass with one of the two missing, and the leading one is precisely
                    // the one a neighbour comparison cannot see.
                    report.check(text.find("1 track(s) missing here, at position(s) 0..0") != std::string::npos,
                        "the playlist names the hole before the first surviving track");
                    report.check(text.find("1 track(s) missing here, at position(s) 2..2") != std::string::npos,
                        "and the hole between the two surviving tracks");
                }

                // An export must never carry a partial record, whatever mode is asked for: the audio
                // file is finished, and a record beside it claims to list what is in it.
                const auto rendered = mixManager.getMixTracks(mixInfo.mixId);
                MixRecoveryCapture exportAttempt;
                const auto exportResult =
                    mixManager.captureRecoveryData(mixInfo.mixId, exportAttempt, &rendered, RecoveryCaptureMode::AllowIncomplete);
                report.check(exportResult.isOk(), "an export-time partial capture completes without error");
                report.check(!exportAttempt.captured, "a partial record is refused against a rendered export even when partials are allowed");
            }

            // --- 5c. A damaged mix can also hold two rows at the same position ---
            //
            // MixRecovery keys on (mix_id, order_in_mix); MixTracks does not, so a mix that lost rows
            // may also have repeats. Copying those positions across fails on the primary key, which
            // is exactly what happened to two mixes on the first real run of this. The record
            // renumbers instead: order_in_mix is the position within the record, which for an intact
            // mix it always was.
            {
                report.check(clearRecoveryData(databasePath, mixInfo.mixId), "the record could be cleared again");

                const auto before = mixManager.getMixTracks(mixInfo.mixId);
                report.check(before.size() >= 2, "the damaged mix still has rows to duplicate a position with");
                if (before.size() >= 2)
                {
                    report.check(duplicateOrderInMix(databasePath, mixInfo.mixId, before.back().orderInMix, before.front().orderInMix),
                        "two rows could be put at the same position");

                    MixRecoveryCapture repeated;
                    const auto repeatedResult =
                        mixManager.captureRecoveryData(mixInfo.mixId, repeated, nullptr, RecoveryCaptureMode::AllowIncomplete);
                    report.check(repeatedResult.isOk(), std::format("capturing a mix with a repeated position succeeds (said: '{}')", repeatedResult.errorMessage));
                    report.check(repeated.captured, "and it is captured rather than refused");
                    report.check(repeated.entries.size() == before.size(),
                        std::format("every surviving row is recorded ({} of {})", repeated.entries.size(), before.size()));

                    std::vector<MixRecoveryEntry> repeatedRead;
                    report.check(mixManager.getRecoveryData(mixInfo.mixId, repeatedRead).isOk(), "the record reads back");
                    bool numbered = repeatedRead.size() == before.size();
                    for (size_t i = 0; numbered && i < repeatedRead.size(); ++i)
                    {
                        numbered = repeatedRead[i].orderInMix == static_cast<int>(i);
                    }
                    report.check(numbered, "the recorded positions run 0..N-1, whatever the mix had stored");

                    // And the positions the mix held - including the repeat - are still there.
                    std::vector<int> heldPositions;
                    for (const auto &entry : repeatedRead)
                    {
                        heldPositions.push_back(entry.sourceOrderInMix.value_or(-1));
                    }
                    std::vector<int> expectedHeld;
                    for (const auto &row : before)
                    {
                        expectedHeld.push_back(row.orderInMix);
                    }
                    expectedHeld.back() = before.front().orderInMix; // the row that was moved on top
                    std::sort(expectedHeld.begin(), expectedHeld.end());
                    std::sort(heldPositions.begin(), heldPositions.end());
                    report.check(heldPositions == expectedHeld,
                        std::format("the duplicated source positions survive the renumbering (found {})", idsToText(heldPositions)));
                }
            }

            // --- 6. Deleting the mix does take its record with it ---

            report.check(mixManager.removeMix(mixInfo.mixId), "the test mix can be deleted");

            std::vector<MixRecoveryEntry> afterMixDelete;
            report.check(mixManager.getRecoveryData(mixInfo.mixId, afterMixDelete).isOk(), "recovery data reads without error after the mix was deleted");
            report.check(afterMixDelete.empty(), "deleting the mix removes its recovery rows");

            // --- 7. What the writer is allowed to overwrite ---
            //
            // Export-time writing replaces: the audio has just been rendered and the playlist beside
            // it has to describe that render. The maintenance pass must not, because the file it would
            // replace may be one somebody has been writing notes on while rebuilding a mix.
            {
                const auto occupied = selfTestRoot / "occupied.m3u";

                const std::string staged{"notes I made while putting this mix back together\nsecond line\n"};
                {
                    std::ofstream existing{occupied, std::ios::binary | std::ios::trunc};
                    existing << staged;
                }

                std::vector<MixRecoveryEntry> rows;
                std::ignore = mixManager.getRecoveryData(mixInfo.mixId, rows);
                if (rows.empty())
                {
                    // The mix was deleted just above, so build the one row this needs by hand.
                    MixRecoveryEntry entry;
                    entry.mixName = "a mix";
                    entry.filename = "track.mp3";
                    entry.folderPath = "D:\\music";
                    entry.duration = Duration_t{1000};
                    rows.push_back(entry);
                }

                bool existed = false;
                const auto refused = audio::writeMixRecoveryM3U(occupied, rows, std::nullopt, audio::M3UWriteMode::NeverReplace, &existed);
                // Every check below carries this, rather than relying on the ones above it. report.check
                // records a result; it does not stop the run, so a later line saying "and ..." is not
                // conditional on the earlier ones having held. Without it, each of them is separately true
                // of a writer that did nothing at all.
                const bool refusedCorrectly{refused.empty() && existed};
                report.check(refused.empty(), std::format("NeverReplace over an existing file is not an error (said: '{}')", refused));
                report.check(existed, "and it reports that the name was taken");

                {
                    std::ifstream in{occupied, std::ios::binary};
                    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
                    // The whole file, byte for byte. A prefix check passes for a writer that appended
                    // to it, or replaced everything after the first line - which is not "untouched".
                    report.check(refusedCorrectly && text == staged, "the file that was there is untouched, byte for byte - this is the whole point");
                }

                // No leftovers: a temporary beside it is a second name for whatever was published,
                // and the next thing to open that name truncates the file it points at.
                report.check(refusedCorrectly && strayTempCount(selfTestRoot) == 0, "and no temporary was left beside it");

                // A free name is claimed normally.
                const auto freeName = selfTestRoot / "unoccupied.m3u";
                existed = true;
                const auto written = audio::writeMixRecoveryM3U(freeName, rows, std::nullopt, audio::M3UWriteMode::NeverReplace, &existed);
                const bool published{written.empty() && !existed && std::filesystem::exists(freeName, ec)};
                report.check(written.empty() && !existed, "NeverReplace writes when the name is free");
                report.check(std::filesystem::exists(freeName, ec), "and the file is there afterwards");

                // This is the branch that publishes by hard link, so the temporary is a second name for
                // the file that was just published and has to be gone.
                //
                // !existed belongs in the gate as much as the rest: the already-there fast path also
                // returns no error, leaves the file in place and creates no temporary, so without it this
                // is green for a writer that took the branch this check is not about.
                report.check(published && strayTempCount(selfTestRoot) == 0, "and the temporary that became its second name is unlinked");

                // A name that is taken by something other than a usable file is a different answer from
                // "there is already a playlist here": no playlist can be written for that mix at all, so
                // it must be a failure rather than a quiet nothing-to-do.
                const auto blocked = selfTestRoot / "blocked.m3u";
                std::filesystem::create_directories(blocked, ec);
                report.check(std::filesystem::is_directory(blocked, ec), "a directory could be put where a playlist would go");
                report.check(audio::mixRecoveryM3UTargetState(blocked) == audio::M3UTargetState::Blocked,
                    "a directory under a playlist name reads as blocked, not as a playlist");

                existed = true;
                const auto refusedBlocked = audio::writeMixRecoveryM3U(blocked, rows, std::nullopt, audio::M3UWriteMode::NeverReplace, &existed);
                report.check(!refusedBlocked.empty(), "writing to a blocked name is an error, not a silent success");
                report.check(!existed, "and it is not reported as an ordinary already-there");

                report.check(audio::mixRecoveryM3UTargetState(freeName) == audio::M3UTargetState::HoldsFile,
                    "the playlist just written reads as a usable file");
                report.check(audio::mixRecoveryM3UTargetState(selfTestRoot / "never-written.m3u") == audio::M3UTargetState::Free,
                    "and a name nothing occupies reads as free");

                // Replace mode still replaces, which is what an export needs.
                const auto replaced = audio::writeMixRecoveryM3U(occupied, rows, std::nullopt, audio::M3UWriteMode::ReplaceExisting);
                report.check(replaced.empty(), "ReplaceExisting still replaces");
                {
                    std::ifstream in{occupied, std::ios::binary};
                    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
                    report.check(text.starts_with("#EXTM3U"), "and the replacement really is the playlist");
                }
            }

            writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
            spdlog::info("[SelfTest] Mix recovery finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runMigrationSelfTest(const std::filesystem::path &selfTestRoot)
        {
            Report report;
            const auto workRoot = selfTestRoot / "migration";
            const auto resultsPath = selfTestRoot / "migration-results.txt";
            const auto dbPath = workRoot / "v29.db";

            spdlog::info("[SelfTest] Starting migration self test. Root: {}", pathToString(selfTestRoot));

            std::error_code ec;
            std::filesystem::remove_all(workRoot, ec);
            std::filesystem::create_directories(workRoot, ec);
            if (ec)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(workRoot), ec.message()));
                writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                return 1;
            }

            // A database shaped the way v29 left one.
            //
            // Built by letting the application create a complete current schema and then putting one
            // table back the way it was, rather than hand-writing the two tables the migration reads.
            // A database containing only those two is not a database this code can open: connect()
            // initialises the folder cache afterwards, which queries Folders and fails - quietly, since
            // connect() does not look at the result. The test would then have reported a successful
            // migration of a database that could not really be opened at all.
            //
            // This is the one migration in the project that rewrites primary keys, and it does so to
            // records that cannot be regenerated - the mixes they describe have already lost the rows
            // in question. It gets a test of its own for that reason.
            {
                SqliteTrackDatabase fresh;
                const auto created = fresh.connect(dbPath);
                report.check(created.isOk(), std::format("a complete scratch schema could be created (said: '{}')", created.errorMessage));
                if (!created.isOk())
                {
                    writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                    return 1;
                }
            }

            // What a brand-new database is stamped at, read rather than written down: this suite asserts
            // that an aged database climbs all the way back, and hard-coding the number means editing a
            // test every time a rung is added, which is how a check ends up being edited to agree with
            // whatever happened.
            std::string latestVersion;

            {
                SqliteDatabase seed;
                if (!seed.open(pathToString(dbPath)))
                {
                    report.abort("Could not reopen the scratch database to age it.");
                    writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                    return 1;
                }

                {
                    SqliteStatement stmt{seed, "SELECT value FROM SchemaInfo WHERE key = 'schema_version';"};
                    if (stmt.isValid() && stmt.getNextResult())
                    {
                        latestVersion = stmt.getText(0);
                    }
                }
                report.check(!latestVersion.empty(), std::format("a new database says what version it is stamped at (said: '{}')", latestVersion));

                // Only MixRecovery goes back: it is the only table the v30 rung touches. Dropping it
                // takes its indexes with it, which the migration neither reads nor recreates.
                //
                // The foreign key on mix_id is kept, so the fixture is the shape v29 really had, and
                // the two mixes it points at are created first. Enforcement is switched on
                // explicitly: SqliteDatabase::open does not set PRAGMA foreign_keys, so without this
                // the key would be recorded and ignored, and a seeding mistake that invented mixes
                // would go through unnoticed.
                //
                // Three statements rather than one, because that is how the shape came about: the v27
                // rung created the table, the v28 rung appended total_duration and the v29 rung appended
                // is_complete. Writing the finished column list out in a single CREATE is what made this
                // fixture wrong - it had total_duration fifth, where initialSqlStatements declares it,
                // rather than second to last, where ALTER TABLE put it in every database that really
                // migrated. Adding the columns the way the rungs added them makes the order a
                // consequence of the same operations instead of a transcription that can drift again.
                const bool built =
                    seed.execute("PRAGMA foreign_keys = ON;") &&
                    seed.execute("INSERT INTO Mixes (mix_id, name) VALUES (1, 'Seed One'), (2, 'Seed Two');") &&
                    seed.execute("DROP TABLE MixRecovery;") &&
                    seed.execute("CREATE TABLE MixRecovery(mix_id INTEGER NOT NULL, order_in_mix INTEGER NOT NULL, "
                                 "captured_at INTEGER NOT NULL, mix_name TEXT NOT NULL, track_id INTEGER, "
                                 "artist_name TEXT, album_title TEXT, title TEXT, filename TEXT, folder_path TEXT, duration INTEGER, "
                                 "filesize_bytes INTEGER, bpm INTEGER, mix_data TEXT, "
                                 "PRIMARY KEY (mix_id, order_in_mix), "
                                 "FOREIGN KEY (mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE);") &&
                    seed.execute("ALTER TABLE MixRecovery ADD COLUMN total_duration INTEGER;") &&
                    seed.execute("ALTER TABLE MixRecovery ADD COLUMN is_complete INTEGER NOT NULL DEFAULT 1;") &&
                    // The indexes go back too. Dropping the table took them with it, and a fixture
                    // without them cannot show that the migration leaves them standing - which is
                    // worth showing, because renumbering a primary key is exactly the kind of work
                    // that gets done by rebuilding a table and losing whatever hung off it.
                    seed.execute("CREATE INDEX idx_mixrecovery_track ON MixRecovery(track_id);") &&
                    seed.execute("CREATE INDEX idx_mixrecovery_fileident ON MixRecovery(filename, filesize_bytes);") &&
                    seed.execute("UPDATE SchemaInfo SET value = '29' WHERE key = 'schema_version';");
                report.check(built, "the scratch database could be put back into its v29 shape");
                if (!built)
                {
                    writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                    return 1;
                }

                // The fixture's own column order, checked rather than assumed. Every other assertion in
                // this suite names the field it reads, so a fixture in the wrong order passes all of
                // them and nothing says a word - which is exactly how it sat wrong. The list is the v27
                // rung's CREATE TABLE followed by what the v28 and v29 rungs appended, and can be read
                // straight off runMigrations.
                {
                    constexpr const char *v29Columns[]{"mix_id",
                        "order_in_mix",
                        "captured_at",
                        "mix_name",
                        "track_id",
                        "artist_name",
                        "album_title",
                        "title",
                        "filename",
                        "folder_path",
                        "duration",
                        "filesize_bytes",
                        "bpm",
                        "mix_data",
                        "total_duration",
                        "is_complete"};

                    std::vector<std::string> columns;
                    {
                        SqliteStatement stmt{seed, "PRAGMA table_info(MixRecovery);"};
                        while (stmt.getNextResult())
                        {
                            columns.push_back(stmt.getText(1));
                        }
                    }

                    std::string found;
                    for (const auto &column : columns)
                    {
                        if (!found.empty())
                        {
                            found.append(", ");
                        }
                        found.append(column);
                    }

                    report.check(std::ranges::equal(columns, v29Columns),
                        std::format("the v29 fixture holds the columns v29 really had, in that order (found: {})", found));
                }

                // Mix 1 is intact: positions 0, 1, 2, and must come through completely unchanged.
                // Mix 2 is one of the damaged ones: positions 0, 3, 7 with is_complete = 0, exactly as
                // v29 wrote them. Its record positions have to become 0, 1, 2 while 0, 3, 7 survive as
                // the source positions - that is the whole point of the migration.
                const char *insert = "INSERT INTO MixRecovery (mix_id, order_in_mix, captured_at, mix_name, total_duration, track_id, "
                                     "artist_name, album_title, title, filename, folder_path, duration, filesize_bytes, bpm, mix_data, "
                                     "is_complete) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
                // Every value is distinct per row and derived from the mix and position it belongs to.
                // Identical payloads would let a field move from one row to another unnoticed, which is
                // exactly the mistake a renumbering migration is capable of making.
                const auto seedRow = [&seed, insert](int64_t mixId, int order, int complete)
                {
                    const auto tag = [mixId, order](std::string_view field)
                    {
                        return std::format("{}-{}-{}", field, mixId, order);
                    };
                    const auto number = [mixId, order](int64_t base)
                    {
                        return base + mixId * 1000 + order;
                    };

                    SqliteStatement stmt{seed, insert};
                    return stmt.isValid() && stmt.addParam(mixId) && stmt.addParam(order) && stmt.addParam(number(700000)) &&
                           stmt.addParam(tag("mix")) && stmt.addParam(number(400000)) && stmt.addParam(number(100)) &&
                           stmt.addParam(tag("artist")) && stmt.addParam(tag("album")) && stmt.addParam(tag("title")) &&
                           stmt.addParam(tag("file")) && stmt.addParam(tag("folder")) && stmt.addParam(number(1000)) &&
                           stmt.addParam(number(2000)) && stmt.addParam(number(120)) && stmt.addParam(tag("data")) &&
                           stmt.addParam(int64_t{complete}) && stmt.execute();
                };

                bool seeded = true;
                for (const auto order : {0, 1, 2})
                {
                    seeded = seeded && seedRow(1, order, 1);
                }
                for (const auto order : {0, 3, 7})
                {
                    seeded = seeded && seedRow(2, order, 0);
                }
                report.check(seeded, "an intact record and a gapped partial one could be seeded");
            }

            // Opening it runs the ladder. Nothing else in this test asks the database for anything, so
            // whatever comes back afterwards is the migration's doing.
            {
                SqliteTrackDatabase migrated;
                const auto connected = migrated.connect(dbPath);
                report.check(connected.isOk(), std::format("the v29 database migrates on open (said: '{}')", connected.errorMessage));
            }

            SqliteDatabase check;
            if (!check.open(pathToString(dbPath)))
            {
                report.abort("Could not reopen the migrated database.");
                writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                return 1;
            }

            {
                SqliteStatement stmt{check, "SELECT value FROM SchemaInfo WHERE key = 'schema_version';"};
                // The latest version, not 30: opening a v29 database runs every rung above it, and this
                // fixture is only shaped for the v30 one. The checks below are what say v30 did its job.
                report.check(stmt.getNextResult() && stmt.getText(0) == latestVersion,
                    std::format("the schema is stamped at the latest version ({})", latestVersion));
            }

            // Read back whole rows, compared as text with NULL spelled out.
            //
            // Not a SELECT COUNT(*) ... WHERE field <> 'expected': in SQL, NULL <> anything is unknown
            // rather than true, so the WHERE discards it and a migration that had blanked every title
            // in the table would have been reported as leaving them all alone.
            const auto rowsOf = [&check](MixId mixId)
            {
                std::vector<std::string> rows;
                SqliteStatement stmt{check,
                    "SELECT order_in_mix, source_order_in_mix, captured_at, mix_name, total_duration, track_id, artist_name, "
                    "album_title, title, filename, folder_path, duration, filesize_bytes, bpm, mix_data, is_complete "
                    "FROM MixRecovery WHERE mix_id = ? ORDER BY order_in_mix"};
                if (!stmt.isValid() || !stmt.addParam(mixId))
                {
                    return rows;
                }

                while (stmt.getNextResult())
                {
                    std::string row;
                    for (int col = 0; col < 16; ++col)
                    {
                        // "<null>" rather than an empty string, so a field that was blanked cannot
                        // read as a field that was always empty.
                        row += (col == 0 ? "" : "|") + (stmt.isNull(col) ? std::string{"<null>"} : stmt.getText(col));
                    }
                    rows.push_back(std::move(row));
                }
                return rows;
            };

            const auto expectedRow = [](int64_t mixId, int recordOrder, int sourceOrder, int complete)
            {
                const auto tag = [mixId, sourceOrder](std::string_view field)
                {
                    return std::format("{}-{}-{}", field, mixId, sourceOrder);
                };
                const auto number = [mixId, sourceOrder](int64_t base)
                {
                    return base + mixId * 1000 + sourceOrder;
                };

                return std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
                    recordOrder,
                    sourceOrder,
                    number(700000),
                    tag("mix"),
                    number(400000),
                    number(100),
                    tag("artist"),
                    tag("album"),
                    tag("title"),
                    tag("file"),
                    tag("folder"),
                    number(1000),
                    number(2000),
                    number(120),
                    tag("data"),
                    complete);
            };

            // The intact record: positions unchanged, because rank and stored position already agreed,
            // and every field still attached to the row it was seeded on.
            const std::vector<std::string> expectedIntact{expectedRow(1, 0, 0, 1), expectedRow(1, 1, 1, 1), expectedRow(1, 2, 2, 1)};
            report.check(rowsOf(1) == expectedIntact, "an intact record comes through the migration unchanged, field for field");

            // The damaged one: renumbered to 0, 1, 2 while 0, 3, 7 survive beside it - and each row's
            // payload still names the position it originally held, so nothing was shuffled.
            const std::vector<std::string> expectedGapped{expectedRow(2, 0, 0, 0), expectedRow(2, 1, 3, 0), expectedRow(2, 2, 7, 0)};
            report.check(rowsOf(2) == expectedGapped, "a gapped record is renumbered 0..N-1 with its source positions and payload intact");

            if (rowsOf(2) != expectedGapped)
            {
                for (const auto &row : rowsOf(2))
                {
                    report.note("gapped row: " + row);
                }
            }

            {
                SqliteStatement stmt{check, "SELECT COUNT(*) FROM MixRecovery"};
                report.check(stmt.getNextResult() && stmt.getInt64(0) == 6, "no row was lost or duplicated");
            }

            {
                SqliteStatement stmt{check,
                    "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND tbl_name = 'MixRecovery' "
                    "AND name IN ('idx_mixrecovery_track', 'idx_mixrecovery_fileident')"};
                report.check(stmt.getNextResult() && stmt.getInt64(0) == 2, "both MixRecovery indexes are still there afterwards");
            }

            // --- v30 to v31: one Folders row per path ---
            //
            // Its own database, because the fixture is a library that already carries the damage the
            // index exists to prevent: two Folders rows for one path, tracks under both, one filename
            // present in both, a child folder hanging off the row that is about to go, and a mix
            // pointing at a track in it.
            //
            // The merge is what needs the test, not the index. Folding two folders onto one moves
            // tracks into a folder that may already hold a row for the same filename, and the obvious
            // way to get there - delete the duplicate and let ON DELETE CASCADE tidy up - takes those
            // tracks and the MixTracks rows referencing them with it, silently.
            {
                const auto folderDbPath = workRoot / "v30-folders.db";

                {
                    SqliteTrackDatabase fresh;
                    const auto created = fresh.connect(folderDbPath);
                    report.check(created.isOk(), std::format("a scratch schema for the folder migration could be created (said: '{}')", created.errorMessage));
                    if (!created.isOk())
                    {
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }
                }

                {
                    SqliteDatabase seed;
                    if (!seed.open(pathToString(folderDbPath)))
                    {
                        report.abort("Could not reopen the folder scratch database to age it.");
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }

                    // Dropping the index is what puts the database back into its v30 shape - it is the
                    // only thing v31 adds, and the duplicate rows below cannot be inserted while it
                    // stands. Foreign keys stay off here on purpose: the fixture is written by hand and
                    // is consistent, and the migration is what has to keep it that way.
                    const bool aged =
                        seed.execute("DROP INDEX idx_folders_root_path;") &&
                        // Rows 20 and 21 are the other kind of duplicate: neither has a computed path,
                        // so a unique index on root_path cannot see them (NULLs are distinct) and the
                        // migration has to reconstruct their paths before it looks for duplicates.
                        // Their names differ only in case, which is a duplicate to normalizeForCache
                        // and therefore to the cache - the reconstruction has to use the same rule.
                        // Three rows for c:\dup, not two: with only two, a title collision always has
                        // one album on the keeper, and the case where neither is - two losers holding
                        // the same title and the keeper holding none - never arises. That is the case
                        // where "whichever the UPDATE reached first" would be the answer if the merge
                        // did not pick one itself.
                        seed.execute("INSERT INTO Folders (folder_id, parent_id, name, root_path, actual_path) VALUES "
                                     "(10, NULL, 'dup', 'c:\\dup', 'C:\\Dup'), "
                                     "(11, NULL, 'dup', 'c:\\dup', 'C:\\Dup'), "
                                     "(12, 11, 'sub', 'c:\\dup\\sub', 'C:\\Dup\\Sub'), "
                                     "(13, NULL, 'dup', 'c:\\dup', 'C:\\Dup'), "
                                     "(20, NULL, 'Orphan', NULL, NULL), "
                                     "(21, NULL, 'ORPHAN', '', NULL), "
                                     "(22, 20, 'Deep', NULL, NULL), "
                                     "(23, 21, 'DEEP', NULL, NULL);") &&
                        seed.execute("INSERT INTO Tracks (track_id, folder_id, filename, title) VALUES "
                                     "(100, 10, 'both.mp3', 'kept'), "
                                     "(101, 11, 'both.mp3', 'collapsed'), "
                                     "(102, 11, 'only-here.mp3', 'moved'), "
                                     "(103, 12, 'child.mp3', 'reparented'), "
                                     "(104, 20, 'a.mp3', 'under the surviving pathless row'), "
                                     "(105, 21, 'b.mp3', 'under the other pathless row'), "
                                     "(106, 13, 'c.mp3', 'under the third row for one path'), "
                                     "(107, 11, 'd.mp3', 'on the album that wins a two-loser collision'), "
                                     "(108, 22, 'e.mp3', 'under the surviving pathless child'), "
                                     "(109, 23, 'f.mp3', 'under the other pathless child'), "
                                     "(110, 11, 'g.mp3', 'on the album that has no counterpart');") &&
                        // Albums, covering the three shapes the merge has to tell apart. genres, moods
                        // and tags are JSON arrays - that is what vectorToJsonArray writes and what
                        // jsonArrayToVector expects - so the fixture stores them that way.
                        //
                        // 200 and 201 collide by title with one of them on the keeper: 201 gives way to
                        // 200 (lower id) rather than cascading away with folder 11. 200 carries no
                        // genres, tags, bandcamp link or year; 201 carries all four, and an
                        // album_artist that 200 also has, so the survivor keeps its own there.
                        //
                        // 204 and 205 collide by title with neither on the keeper - one on each of the
                        // two loser rows. 204 wins by id, and has to be moved to folder 10 afterwards.
                        //
                        // 202 has no counterpart at all and simply moves, landing beside 200 in folder
                        // 10. A folder holding two albums of different titles is a layout the schema
                        // allows and the fixture is deliberately built to produce.
                        seed.execute("INSERT INTO Albums (album_id, album_artist, title, year, folder_id, genres, moods, tags, bandcamp_url) VALUES "
                                     "(200, 'Keeper Artist', 'Shared', NULL, 10, '[]', NULL, NULL, NULL), "
                                     "(201, 'Loser Artist', 'Shared', 1999, 11, '[\"downtempo\"]', '[\"calm\"]', '[\"mine\"]', 'https://example.test/album'), "
                                     "(202, 'Solo Artist', 'Only There', 2001, 11, '[\"ambient\"]', NULL, NULL, NULL), "
                                     "(204, 'Loser One', 'Two Losers', NULL, 11, '[]', NULL, '[\"from-204\"]', NULL), "
                                     "(205, 'Loser Two', 'Two Losers', 1990, 13, '[\"acid\"]', NULL, '[\"from-205\"]', NULL);") &&
                        seed.execute("UPDATE Tracks SET album_id = 200 WHERE track_id = 100;") &&
                        seed.execute("UPDATE Tracks SET album_id = 201 WHERE track_id IN (101, 102);") &&
                        seed.execute("UPDATE Tracks SET album_id = 202 WHERE track_id = 110;") &&
                        seed.execute("UPDATE Tracks SET album_id = 204 WHERE track_id = 107;") &&
                        seed.execute("UPDATE Tracks SET album_id = 205 WHERE track_id = 106;") &&
                        // Every track that ends up in folder 10, describing album 200 - and every one
                        // of them, because the album pass gives up on a folder the moment it reads a
                        // row with no artist or no album title, and the lookup it is here to exercise
                        // sits behind that. The album named is deliberately not the one a lookup
                        // keeping a single album per folder would have kept: 204 is read last.
                        seed.execute("UPDATE Tracks SET artist_name = 'Keeper Artist', album_title = 'Shared' "
                                     "WHERE track_id IN (100, 101, 102, 106, 107, 110);") &&
                        seed.execute("INSERT INTO Mixes (mix_id, name) VALUES (7, 'Folder Seed Mix');") &&
                        seed.execute("INSERT INTO MixTracks (mix_id, track_id, order_in_mix, mix_data) VALUES "
                                     "(7, 101, 0, '{}'), (7, 102, 1, '{}');") &&
                        // A marker on the row that gets collapsed, to cover the step that remaps them.
                        // TrackMarkers is part of the schema a new database is built from since v32, so
                        // the fixture no longer has to put the table back by hand.
                        seed.execute("INSERT INTO TrackMarkers (marker_id, track_id, position_ms, comment, created_at, updated_at) VALUES "
                                     "(1, 101, 5000, 'on the collapsed row', 1, 1);") &&
                        // The search tables go, which is what a v30 database created from scratch really
                        // looked like - and keeps both sides of the "skip a step whose table is not
                        // there" check in the v31 rung covered, now that TrackMarkers is present.
                        seed.execute("DROP TRIGGER IF EXISTS tracksdata_fts_ai;") &&
                        seed.execute("DROP TRIGGER IF EXISTS tracksdata_fts_ad;") &&
                        seed.execute("DROP TRIGGER IF EXISTS tracksdata_fts_au;") &&
                        seed.execute("DROP TRIGGER IF EXISTS tracks_search_insert;") &&
                        seed.execute("DROP TRIGGER IF EXISTS tracks_search_update;") &&
                        seed.execute("DROP TRIGGER IF EXISTS tracks_search_delete;") &&
                        seed.execute("DROP TRIGGER IF EXISTS tracktags_search_insert;") &&
                        seed.execute("DROP TRIGGER IF EXISTS tracktags_search_delete;") &&
                        seed.execute("DROP TABLE IF EXISTS TracksSearchFTS;") &&
                        seed.execute("DROP TABLE IF EXISTS TracksSearchData;") &&
                        seed.execute("UPDATE SchemaInfo SET value = '30' WHERE key = 'schema_version';");
                    report.check(aged, "a v30-shaped database holding duplicate folder rows could be seeded");
                    if (!aged)
                    {
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }
                }

                {
                    SqliteTrackDatabase migrated;
                    const auto connected = migrated.connect(folderDbPath);
                    report.check(connected.isOk(), std::format("the v30 database migrates on open (said: '{}')", connected.errorMessage));

                    // Reading a folder back through the cache, which is what makes the album pass run
                    // over folder 10 - three albums, every track in it describing one of them, which is
                    // the layout that used to abort here in Debug.
                    //
                    // Deliberately not called a check that the cache *built*: no accessor can say. Each
                    // one calls buildCacheIfNeeded and then reads m_folderInfoFromId whatever it
                    // returned, and that map is filled before every path that returns false - so a
                    // folder comes back from a build that failed exactly as it does from one that
                    // worked. Tracked as issue #32.
                    report.check(migrated.getFolderDatabase().getFolderById(10).has_value(),
                        "the migrated folder rows read back through the cache");
                }

                SqliteDatabase check;
                if (!check.open(pathToString(folderDbPath)))
                {
                    report.abort("Could not reopen the migrated folder database.");
                    writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                    return 1;
                }

                // One value per query, as text with NULL spelled out, so a column that was blanked
                // cannot read as one that was always empty.
                const auto scalar = [&check](const char *sql)
                {
                    SqliteStatement stmt{check, sql};
                    if (!stmt.isValid() || !stmt.getNextResult())
                    {
                        return std::string{"<query failed>"};
                    }
                    return stmt.isNull(0) ? std::string{"<null>"} : stmt.getText(0);
                };

                report.check(scalar("SELECT value FROM SchemaInfo WHERE key = 'schema_version'") == latestVersion,
                    std::format("the schema is stamped at the latest version ({})", latestVersion));
                report.check(scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_folders_root_path'") == "1",
                    "the unique folder path index is there afterwards");
                report.check(scalar("SELECT COUNT(*) FROM Folders WHERE root_path = 'c:\\dup'") == "1", "one row is left for the duplicated path");
                report.check(scalar("SELECT folder_id FROM Folders WHERE root_path = 'c:\\dup'") == "10", "and it is the lowest of the two ids");
                report.check(scalar("SELECT parent_id FROM Folders WHERE folder_id = 12") == "10", "the child of the row that went is re-parented onto the survivor");

                // The tracks that must still be there, and where. 101 is the collapsed one: same folder
                // path and same filename as 100, so it is the same file described twice.
                report.check(scalar("SELECT COUNT(*) FROM Tracks") == "10", "the merge lost no track it could not prove was a duplicate");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 100") == "10", "the surviving copy of the shared filename stayed put");
                report.check(scalar("SELECT COUNT(*) FROM Tracks WHERE track_id = 101") == "0", "its duplicate was collapsed away");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 102") == "10", "a track with no counterpart moved to the survivor");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 103") == "12", "a track in the re-parented child did not move");

                // The point of merging rather than cascading. MixTracks.track_id cascades on delete, so
                // a migration that removed 101 without remapping would have silently shortened this mix.
                report.check(scalar("SELECT COUNT(*) FROM MixTracks WHERE mix_id = 7") == "2", "the mix still has both of its rows");
                report.check(scalar("SELECT track_id FROM MixTracks WHERE mix_id = 7 AND order_in_mix = 0") == "100",
                    "the row that pointed at the collapsed track now points at the one it kept");
                report.check(scalar("SELECT track_id FROM MixTracks WHERE mix_id = 7 AND order_in_mix = 1") == "102", "the other row is untouched");
                report.check(scalar("SELECT track_id FROM TrackMarkers WHERE marker_id = 1") == "100", "a marker on the collapsed track followed it to the one it kept");

                // Two rows that never had a path. Nothing in the schema can see them as duplicates, so
                // the migration has to reconstruct what they name before it looks - and reconstruct it
                // with normalizeForCache, or 'Orphan' and 'ORPHAN' stay two folders forever.
                report.check(scalar("SELECT COUNT(*) FROM Folders WHERE root_path = 'orphan'") == "1",
                    "two rows that never had a computed path are reconstructed and merged into one");
                report.check(scalar("SELECT folder_id FROM Folders WHERE root_path = 'orphan'") == "20", "and the survivor is the lowest of the two ids");
                report.check(scalar("SELECT COUNT(*) FROM Folders WHERE root_path IS NULL OR root_path = ''") == "0",
                    "no row is left with a path the index cannot see");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 104") == "20", "the track under the survivor stayed there");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 105") == "20", "the track under the other one came across");

                // The same thing one level down, which is the branch that reconstructs a path from a
                // parent rather than from a name alone: two pathless children under the two pathless
                // roots, whose names also differ only in case.
                report.check(scalar("SELECT COUNT(*) FROM Folders WHERE root_path = 'orphan\\deep'") == "1",
                    "two pathless children under two pathless parents reconstruct to one path and merge");
                report.check(scalar("SELECT folder_id FROM Folders WHERE root_path = 'orphan\\deep'") == "22", "the survivor is the lowest of the two ids");
                report.check(scalar("SELECT parent_id FROM Folders WHERE folder_id = 22") == "20", "and hangs off the folder its parent was merged into");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 108") == "22", "the track under the surviving child stayed there");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 109") == "22", "the track under the other child came across");
                report.check(scalar("SELECT COUNT(*) FROM Folders") == "4", "four folders are left, one per distinct path");

                // The albums. 201 could not move - 200 holds that title on the keeper folder - and had
                // to be merged rather than left to cascade away with folder 11.
                report.check(scalar("SELECT COUNT(*) FROM Albums") == "3", "the two colliding albums were merged away and three survive");
                report.check(scalar("SELECT folder_id FROM Albums WHERE album_id = 200") == "10", "the survivor of the collision with the keeper is the keeper's");
                report.check(scalar("SELECT COUNT(*) FROM Albums WHERE album_id = 201") == "0", "the album that could not move is gone");
                report.check(scalar("SELECT genres FROM Albums WHERE album_id = 200") == "[\"downtempo\"]", "the survivor took the genres it had none of");
                report.check(scalar("SELECT moods FROM Albums WHERE album_id = 200") == "[\"calm\"]", "and the moods");
                report.check(scalar("SELECT tags FROM Albums WHERE album_id = 200") == "[\"mine\"]", "and the tags");
                report.check(scalar("SELECT bandcamp_url FROM Albums WHERE album_id = 200") == "https://example.test/album", "and the bandcamp link");
                report.check(scalar("SELECT year FROM Albums WHERE album_id = 200") == "1999", "and the year");
                report.check(scalar("SELECT album_artist FROM Albums WHERE album_id = 200") == "Keeper Artist",
                    "but kept its own value where it had one");
                report.check(scalar("SELECT album_id FROM Tracks WHERE track_id = 100") == "200", "the track already on the survivor still points at it");
                report.check(scalar("SELECT album_id FROM Tracks WHERE track_id = 102") == "200",
                    "and the track on the merged-away album was moved onto it, not orphaned");

                // Two losers, no album of that title on the keeper. Which one survives is decided by
                // id, not by the order the statements happened to reach them.
                report.check(scalar("SELECT COUNT(*) FROM Albums WHERE title = 'Two Losers'") == "1", "a collision between two losers leaves one album");
                report.check(scalar("SELECT album_id FROM Albums WHERE title = 'Two Losers'") == "204", "and it is the lower of the two ids, whichever was reached first");
                report.check(scalar("SELECT folder_id FROM Albums WHERE album_id = 204") == "10", "the survivor was moved onto the keeper folder");
                report.check(scalar("SELECT genres FROM Albums WHERE album_id = 204") == "[\"acid\"]", "it took the genres it had none of from the other loser");
                report.check(scalar("SELECT year FROM Albums WHERE album_id = 204") == "1990", "and the year");
                report.check(scalar("SELECT tags FROM Albums WHERE album_id = 204") == "[\"from-204\"]", "and kept its own tags");
                report.check(scalar("SELECT album_id FROM Tracks WHERE track_id = 107") == "204", "its own track still points at it");
                report.check(scalar("SELECT album_id FROM Tracks WHERE track_id = 106") == "204", "and the other loser's track was moved onto it");

                // The album with no counterpart moves with its folder, id, metadata and tracks intact -
                // landing beside 200 in folder 10, which is a folder holding two albums of different
                // titles. The schema allows that, so the migration must not treat it as a collision.
                report.check(scalar("SELECT folder_id FROM Albums WHERE album_id = 202") == "10", "an album with no counterpart moved to the survivor");
                report.check(scalar("SELECT genres FROM Albums WHERE album_id = 202") == "[\"ambient\"]", "keeping what was on it");
                report.check(scalar("SELECT album_id FROM Tracks WHERE track_id = 110") == "202", "and the track that pointed at it still does");
                report.check(scalar("SELECT COUNT(*) FROM Albums WHERE folder_id = 10") == "3",
                    "one folder holds three albums of different titles afterwards, which is a layout the schema allows");
                report.check(scalar("SELECT COUNT(*) FROM Albums") == "3",
                    "and the album pass added none of its own for a folder whose tracks describe one it already has");

                // And the index actually refuses, rather than merely existing.
                {
                    SqliteStatement stmt{check, "INSERT INTO Folders (parent_id, name, root_path) VALUES (NULL, 'dup', 'c:\\dup');"};
                    report.check(stmt.isValid() && !stmt.execute(), "a second row for a path the table already has is refused");
                }
            }

            // --- The v32 rung leaves a database that already has everything alone ---
            //
            // Scope, stated because the first version of this check claimed more than it does: both
            // databases are built from the current schema, so sending one back through the ladder can
            // only exercise rungs that are idempotent. v32 is - it is all IF NOT EXISTS. An ordinary
            // ALTER TABLE ADD COLUMN rung is not, and would fail with a duplicate column against a
            // database that already has it.
            //
            // So this is not a whole-ladder convergence test and must not be read as one. It cannot
            // see structural differences from v4-v31: the MixTracks primary key the v6 rung creates
            // and the current schema does not was sitting right under it and it said nothing. What it
            // does check is that the repair is safe against the databases most people have - the ones
            // that are already complete - and that is worth having on its own.
            //
            // A real convergence check needs a frozen old-schema fixture run up the whole ladder.
            // There is one, at the end of this suite.
            {
                const auto freshPath = workRoot / "convergence-fresh.db";
                const auto laddered = workRoot / "convergence-laddered.db";

                const auto createFresh = [&report](const std::filesystem::path &path, const char *what)
                {
                    SqliteTrackDatabase db;
                    const auto created = db.connect(path);
                    report.check(created.isOk(), std::format("{} could be created (said: '{}')", what, created.errorMessage));
                    return created.isOk();
                };

                // Reads the schema as text, one line per object, ignoring the ANALYZE tables: those
                // depend on what each database happened to be asked, not on how it was built.
                const auto schemaOf = [&report](const std::filesystem::path &path)
                {
                    std::vector<std::string> objects;
                    SqliteDatabase db;
                    if (!db.open(pathToString(path)))
                    {
                        report.abort(std::format("Could not open {} to read its schema.", pathToString(path)));
                        return objects;
                    }

                    SqliteStatement stmt{db,
                        "SELECT type, name, COALESCE(sql, '') FROM sqlite_master "
                        "WHERE name NOT LIKE 'sqlite_stat%' ORDER BY type, name;"};
                    if (!stmt.isValid())
                    {
                        report.abort("Could not read sqlite_master.");
                        return objects;
                    }

                    while (stmt.getNextResult())
                    {
                        objects.push_back(stmt.getText(0) + " " + stmt.getText(1) + " " + stmt.getText(2));
                    }
                    return objects;
                };

                if (createFresh(freshPath, "a fresh database for the convergence check") &&
                    createFresh(laddered, "a second one to send back through the ladder"))
                {
                    {
                        SqliteDatabase age;
                        const bool stamped = age.open(pathToString(laddered)) &&
                            age.execute("UPDATE SchemaInfo SET value = '31' WHERE key = 'schema_version';");
                        report.check(stamped, "the second database could be stamped back to version 31");
                    }

                    {
                        SqliteTrackDatabase reopened;
                        const auto connected = reopened.connect(laddered);
                        report.check(connected.isOk(), std::format("it migrates back up to the latest version (said: '{}')", connected.errorMessage));
                    }

                    const auto freshSchema = schemaOf(freshPath);
                    const auto ladderedSchema = schemaOf(laddered);

                    report.check(!freshSchema.empty(), std::format("the fresh database has a schema to compare ({} objects)", freshSchema.size()));
                    report.check(freshSchema == ladderedSchema,
                        std::format("the v32 rung changes nothing in a database that already has everything ({} vs {} objects)",
                            freshSchema.size(),
                            ladderedSchema.size()));

                    // Name what differs, because "two lists are not equal" is not a bug report.
                    if (freshSchema != ladderedSchema)
                    {
                        for (const auto &object : freshSchema)
                        {
                            if (std::ranges::find(ladderedSchema, object) == ladderedSchema.end())
                            {
                                report.note("only in the fresh database: " + object);
                            }
                        }
                        for (const auto &object : ladderedSchema)
                        {
                            if (std::ranges::find(freshSchema, object) == freshSchema.end())
                            {
                                report.note("only after the ladder: " + object);
                            }
                        }
                    }
                }
            }

            // --- And the repair itself, against a database that really is missing all of it ---
            //
            // The convergence check above compares two databases that both start complete. This one
            // strips a v31 database back to the state every library created from scratch was actually
            // in - no search tables, no markers, no presets - and requires the v32 rung to put them
            // back and to fill the search content from the tracks that are already there.
            {
                const auto strippedPath = workRoot / "v31-stripped.db";

                {
                    SqliteTrackDatabase fresh;
                    const auto created = fresh.connect(strippedPath);
                    report.check(created.isOk(), std::format("a database to strip could be created (said: '{}')", created.errorMessage));
                    if (!created.isOk())
                    {
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }
                }

                {
                    SqliteDatabase strip;
                    if (!strip.open(pathToString(strippedPath)))
                    {
                        report.abort("Could not reopen the database to strip it.");
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }

                    // A folder and two tracks first, so the search backfill has something to find - a
                    // library created without the search tables was scanned without them too, and the
                    // triggers only maintain the content table from the moment they exist.
                    const bool stripped =
                        strip.execute("INSERT INTO Folders (folder_id, parent_id, name, root_path, actual_path) VALUES "
                                      "(30, NULL, 'stripped', 'c:\\stripped', 'C:\\Stripped');") &&
                        strip.execute("INSERT INTO Tracks (track_id, folder_id, filename, title, artist_name, album_title) VALUES "
                                      "(300, 30, 'findme.mp3', 'Findable Title', 'Findable Artist', 'Findable Album'), "
                                      "(301, 30, 'other.mp3', 'Other Title', 'Other Artist', 'Other Album');") &&
                        strip.execute("DROP TRIGGER IF EXISTS tracksdata_fts_ai;") &&
                        strip.execute("DROP TRIGGER IF EXISTS tracksdata_fts_ad;") &&
                        strip.execute("DROP TRIGGER IF EXISTS tracksdata_fts_au;") &&
                        strip.execute("DROP TRIGGER IF EXISTS tracks_search_insert;") &&
                        strip.execute("DROP TRIGGER IF EXISTS tracks_search_update;") &&
                        strip.execute("DROP TRIGGER IF EXISTS tracks_search_delete;") &&
                        strip.execute("DROP TRIGGER IF EXISTS tracktags_search_insert;") &&
                        strip.execute("DROP TRIGGER IF EXISTS tracktags_search_delete;") &&
                        strip.execute("DROP TABLE IF EXISTS TracksSearchFTS;") &&
                        strip.execute("DROP TABLE IF EXISTS TracksSearchData;") &&
                        strip.execute("DROP TABLE IF EXISTS TrackMarkers;") &&
                        strip.execute("DROP TABLE IF EXISTS MixMarkers;") &&
                        strip.execute("DROP TABLE IF EXISTS EQPresets;") &&
                        strip.execute("DROP TABLE IF EXISTS ReverbPresets;") &&
                        strip.execute("DROP INDEX IF EXISTS idx_tracks_status;") &&
                        // The name a migrated library carries instead of idx_mixtracks_mix_order.
                        strip.execute("DROP INDEX IF EXISTS idx_mixtracks_mix_order;") &&
                        strip.execute("DROP INDEX IF EXISTS idx_mixtracks_track;") &&
                        strip.execute("CREATE INDEX idx_mixtracks_order ON MixTracks(mix_id, order_in_mix);") &&
                        strip.execute("UPDATE SchemaInfo SET value = '31' WHERE key = 'schema_version';");
                    report.check(stripped, "a v31 database could be stripped back to what a fresh one used to hold");
                    if (!stripped)
                    {
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }
                }

                {
                    SqliteTrackDatabase repaired;
                    const auto connected = repaired.connect(strippedPath);
                    report.check(connected.isOk(), std::format("the stripped database migrates on open (said: '{}')", connected.errorMessage));
                }

                SqliteDatabase check;
                if (!check.open(pathToString(strippedPath)))
                {
                    report.abort("Could not reopen the repaired database.");
                    writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                    return 1;
                }

                const auto scalar = [&check](const char *sql)
                {
                    SqliteStatement stmt{check, sql};
                    if (!stmt.isValid() || !stmt.getNextResult())
                    {
                        return std::string{"<query failed>"};
                    }
                    return stmt.isNull(0) ? std::string{"<null>"} : stmt.getText(0);
                };

                report.check(scalar("SELECT value FROM SchemaInfo WHERE key = 'schema_version'") == latestVersion,
                    std::format("the repaired schema is stamped at the latest version ({})", latestVersion));

                for (const auto *table : {"TrackMarkers", "MixMarkers", "EQPresets", "ReverbPresets", "TracksSearchData", "TracksSearchFTS"})
                {
                    const auto sql{std::format("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = '{}'", table)};
                    report.check(scalar(sql.c_str()) == "1", std::format("{} is back", table));
                }

                for (const auto *trigger : {"tracks_search_insert",
                         "tracks_search_update",
                         "tracks_search_delete",
                         "tracktags_search_insert",
                         "tracktags_search_delete",
                         "tracksdata_fts_ai",
                         "tracksdata_fts_ad",
                         "tracksdata_fts_au"})
                {
                    const auto sql{std::format("SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger' AND name = '{}'", trigger)};
                    report.check(scalar(sql.c_str()) == "1", std::format("the {} trigger is back", trigger));
                }

                report.check(scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_tracks_status'") == "1",
                    "the status index is back");
                report.check(scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_mixtracks_mix_order'") == "1",
                    "the MixTracks order index is there under the name the schema uses");
                report.check(scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_mixtracks_track'") == "1",
                    "and the one a migrated library never had");
                report.check(scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_mixtracks_order'") == "0",
                    "the old name it carried instead is gone");

                report.check(scalar("SELECT COUNT(*) FROM EQPresets") == "6", "the six factory EQ presets are there");
                report.check(scalar("SELECT COUNT(*) FROM ReverbPresets") == "7", "and the seven factory reverb presets");
                report.check(scalar("SELECT is_deletable FROM EQPresets WHERE name = 'Flat'") == "0", "a factory preset is not deletable");

                // The tracks that were already in the library have to be searchable, not just the ones
                // added from here on - which is the difference between creating the tables and
                // repairing the database.
                report.check(scalar("SELECT COUNT(*) FROM TracksSearchData") == "2", "the search content table was filled from the tracks already there");
                report.check(scalar("SELECT COUNT(*) FROM TracksSearchFTS WHERE TracksSearchFTS MATCH 'Findable'") == "1",
                    "and a track that was in the library before the repair can be found through the index");
            }

            // --- MixTracks, shaped the way the v6 rung really leaves it ---
            //
            // A version-shaped fixture, because the check above cannot see this: the v6 rung gives
            // MixTracks PRIMARY KEY(mix_id, track_id) and the schema a new database is built from has
            // no uniqueness on those columns at all. A mix may legitimately hold the same track twice
            // - MissingFileScanTask returns indices rather than track ids for exactly that reason -
            // and saveMix inserts each row with a plain INSERT. So on a library that migrated up the
            // ladder, saving such a mix fails its second row and rolls the whole save back, while on
            // a library created from scratch it works.
            //
            // The table is put back into its v6 shape by hand, the way the v29 fixture above does for
            // MixRecovery: nothing produces that shape any more.
            {
                const auto v6Path = workRoot / "v6-mixtracks.db";

                {
                    SqliteTrackDatabase fresh;
                    const auto created = fresh.connect(v6Path);
                    report.check(created.isOk(), std::format("a database for the MixTracks fixture could be created (said: '{}')", created.errorMessage));
                    if (!created.isOk())
                    {
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }
                }

                {
                    SqliteDatabase seed;
                    if (!seed.open(pathToString(v6Path)))
                    {
                        report.abort("Could not reopen the MixTracks fixture to age it.");
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }

                    // Foreign keys on, so the rows the fixture seeds are ones the real schema would
                    // have accepted - a mix and two tracks that exist, not invented ids.
                    const bool aged =
                        seed.execute("PRAGMA foreign_keys = ON;") &&
                        seed.execute("INSERT INTO Folders (folder_id, parent_id, name, root_path, actual_path) VALUES "
                                     "(40, NULL, 'mixtracks', 'c:\\mixtracks', 'C:\\MixTracks');") &&
                        seed.execute("INSERT INTO Tracks (track_id, folder_id, filename, title) VALUES "
                                     "(400, 40, 'one.mp3', 'One'), (401, 40, 'two.mp3', 'Two');") &&
                        seed.execute("INSERT INTO Mixes (mix_id, name) VALUES (9, 'MixTracks Fixture');") &&
                        seed.execute("DROP TABLE MixTracks;") &&
                        seed.execute("CREATE TABLE MixTracks(mix_id INTEGER NOT NULL, track_id INTEGER NOT NULL, "
                                     "order_in_mix INTEGER NOT NULL, mix_data TEXT NOT NULL, "
                                     "PRIMARY KEY(mix_id, track_id), "
                                     "FOREIGN KEY(mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE, "
                                     "FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE);") &&
                        seed.execute("CREATE INDEX idx_mixtracks_order ON MixTracks(mix_id, order_in_mix);") &&
                        seed.execute("INSERT INTO MixTracks (mix_id, track_id, order_in_mix, mix_data) VALUES "
                                     "(9, 400, 0, '{\"first\":true}'), (9, 401, 1, '{\"second\":true}');") &&
                        seed.execute("UPDATE SchemaInfo SET value = '31' WHERE key = 'schema_version';");
                    report.check(aged, "MixTracks could be put back into its v6 shape with rows in it");
                    if (!aged)
                    {
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }

                    // The fixture is only worth anything if the old shape really does refuse the second
                    // row, so that is established here rather than assumed.
                    SqliteStatement duplicate{seed, "INSERT INTO MixTracks (mix_id, track_id, order_in_mix, mix_data) VALUES (9, 400, 2, '{}');"};
                    report.check(duplicate.isValid() && !duplicate.execute(), "the v6 shape refuses a mix that holds the same track twice");
                }

                {
                    SqliteTrackDatabase migrated;
                    const auto connected = migrated.connect(v6Path);
                    report.check(connected.isOk(), std::format("the v6-shaped database migrates on open (said: '{}')", connected.errorMessage));
                }

                SqliteDatabase check;
                if (!check.open(pathToString(v6Path)))
                {
                    report.abort("Could not reopen the rebuilt MixTracks database.");
                    writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                    return 1;
                }

                const auto scalar = [&check](const char *sql)
                {
                    SqliteStatement stmt{check, sql};
                    if (!stmt.isValid() || !stmt.getNextResult())
                    {
                        return std::string{"<query failed>"};
                    }
                    return stmt.isNull(0) ? std::string{"<null>"} : stmt.getText(0);
                };

                report.check(scalar("SELECT COUNT(*) FROM pragma_index_list('MixTracks') WHERE origin = 'pk'") == "0",
                    "the rebuilt MixTracks has no primary key on (mix_id, track_id)");

                // The rows, whole and in order - a rebuild that loses or reorders them would be worse
                // than the constraint it removes.
                report.check(scalar("SELECT COUNT(*) FROM MixTracks") == "2", "both rows survived the rebuild");
                report.check(scalar("SELECT track_id FROM MixTracks WHERE mix_id = 9 AND order_in_mix = 0") == "400", "the first row is intact");
                report.check(scalar("SELECT mix_data FROM MixTracks WHERE mix_id = 9 AND order_in_mix = 0") == "{\"first\":true}",
                    "and carries the data it was seeded with");
                report.check(scalar("SELECT track_id FROM MixTracks WHERE mix_id = 9 AND order_in_mix = 1") == "401", "so is the second");
                report.check(scalar("SELECT mix_data FROM MixTracks WHERE mix_id = 9 AND order_in_mix = 1") == "{\"second\":true}",
                    "with its own data, not the other row's");

                report.check(scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_mixtracks_mix_order'") == "1",
                    "the indexes the drop took with it are back");
                report.check(scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_mixtracks_track'") == "1",
                    "including the one only a fresh schema used to have");

                {
                    // The point of the whole exercise: the second copy of a track now goes in.
                    SqliteStatement duplicate{check, "INSERT INTO MixTracks (mix_id, track_id, order_in_mix, mix_data) VALUES (9, 400, 2, '{}');"};
                    report.check(duplicate.isValid() && duplicate.execute(), "a mix can now hold the same track twice");
                }

                {
                    // And the foreign keys came through the rebuild. Enforcement is switched on
                    // explicitly: SqliteDatabase::open does not, so without this the key would be
                    // recorded and ignored and this check would pass on a table that had lost it.
                    report.check(check.execute("PRAGMA foreign_keys = ON;"), "foreign key enforcement could be switched on");
                    SqliteStatement orphan{check, "INSERT INTO MixTracks (mix_id, track_id, order_in_mix, mix_data) VALUES (9, 99999, 3, '{}');"};
                    report.check(orphan.isValid() && !orphan.execute(), "the rebuilt table still refuses a row pointing at a track that does not exist");
                }
            }

            // --- The databases that really were version 12, run all the way up the ladder ---
            //
            // This is the check the block above cannot be. The fixture is not built from the current
            // schema and then aged by a stamp: it is the initialSqlStatements array copied out of commit
            // d34e3bc, whose latestSchemaVersion is 12, so every rung from 13 to 32 runs against the
            // shape a library created at that version really had. See Tests/SchemaV12Fixture.h.
            //
            // Two shapes are climbed, because they are missing different things and take different
            // branches. They are not every shape a version-12 database could have - one migrated from
            // below v11 carries other rung-only objects, and rungs 2 to 12 are not exercised by either
            // of these; that is the same limit as the fixture's own. Not tracked anywhere: the task
            // that asked for a frozen old-schema fixture was closed by the work this comment sits in,
            // and the residual limit was accepted with it rather than carried forward:
            //  - created from scratch, which never got the search tables (the v12 rung made those, and
            //    initialSqlStatements did not) - the divergence v32 exists to repair, frozen as it was;
            //  - migrated up from an earlier version, which ran that rung and does have them. It is the
            //    only one of the two that reaches the v25 rung with something to sync.
            //
            // The comparison is structural rather than textual, because text cannot work here: SQLite
            // keeps a CREATE statement as written and ALTER TABLE ADD COLUMN appends to it, so a table
            // twenty rungs old never spells itself the way today's initialSqlStatements does even when
            // the two are the same table. readSchemaStructure says what the difference is.
            {
                const auto freshPath = workRoot / "v32-fresh.db";

                SqliteTrackDatabase freshDb;
                const auto createdFresh = freshDb.connect(freshPath);
                report.check(createdFresh.isOk(),
                    std::format("a new database to compare the aged ones against could be created (said: '{}')", createdFresh.errorMessage));

                const auto freshStructure = readSchemaStructure(freshPath, report);
                report.check(!freshStructure.empty(), std::format("the new database has a structure to compare ({} objects)", freshStructure.size()));

                // @param withSearch Whether the fixture also runs the search objects the v12 rung
                //        created, which is what tells the two real v12 shapes apart.
                const auto climbAndCompare = [&](const std::string &label, const std::filesystem::path &agedPath, bool withSearch)
                {
                    bool built = false;
                    {
                        SqliteDatabase seed;
                        if (!seed.open(pathToString(agedPath)))
                        {
                            report.check(false, std::format("the {} fixture database could be created", label));
                            return;
                        }

                        built = true;
                        for (const auto *sql : schemaV12Statements)
                        {
                            if (!seed.execute(sql))
                            {
                                report.check(false, std::format("the {} fixture schema could be built (failed on: {})", label, seed.getLastError()));
                                built = false;
                                break;
                            }
                        }

                        if (built && withSearch)
                        {
                            for (const auto *sql : schemaV12SearchStatements)
                            {
                                if (!seed.execute(sql))
                                {
                                    report.check(false,
                                        std::format("the {} fixture's search objects could be built (failed on: {})", label, seed.getLastError()));
                                    built = false;
                                    break;
                                }
                            }
                        }

                        if (built)
                        {
                            // Rows, not just tables. A rung that rebuilds a table can lose what was in
                            // it, and an empty database is the one case where that never shows. The mix
                            // holds the same track twice on purpose: at 12 nothing stopped it, and it is
                            // what the v6 primary key - which v32 removes - made impossible to save.
                            const bool seeded =
                                seed.execute("INSERT INTO SchemaInfo (key, value) VALUES ('schema_version', '12');") &&
                                seed.execute("INSERT INTO Folders (folder_id, parent_id, name, root_path) VALUES (1, NULL, 'aged', 'c:\\aged');") &&
                                seed.execute("INSERT INTO Tracks (track_id, folder_id, filename, title, artist_name, album_title) "
                                             "VALUES (1, 1, 'one.mp3', 'One', 'Aged Artist', 'Aged Album'), "
                                             "(2, 1, 'two.mp3', 'Two', 'Aged Artist', 'Aged Album');") &&
                                seed.execute("INSERT INTO Tags (tag_id, name) VALUES (1, 'aged-tag');") &&
                                seed.execute("INSERT INTO TrackTags (track_id, tag_id) VALUES (1, 1);") &&
                                seed.execute("INSERT INTO WorkingSets (ws_id, name) VALUES (1, 'Aged Working Set');") &&
                                seed.execute("INSERT INTO WorkingSetTracks (ws_id, track_id) VALUES (1, 1), (1, 2);") &&
                                seed.execute("INSERT INTO Mixes (mix_id, name) VALUES (1, 'Aged Mix');") &&
                                seed.execute("INSERT INTO MixTracks (mix_id, track_id, order_in_mix, mix_data) "
                                             "VALUES (1, 1, 0, '{}'), (1, 2, 1, '{}'), (1, 1, 2, '{}');") &&
                                seed.execute("INSERT INTO LibraryRoots (root_id, path) VALUES (1, 'c:\\aged');");
                            report.check(seeded,
                                std::format("the {} fixture could be given something to migrate (said: '{}')", label, seed.getLastError()));
                            built = seeded;
                        }
                    }

                    if (!built)
                    {
                        return;
                    }

                    {
                        SqliteTrackDatabase aged;
                        const auto climbed = aged.connect(agedPath);
                        report.check(climbed.isOk(), std::format("the {} database climbs the whole ladder (said: '{}')", label, climbed.errorMessage));
                    }

                    {
                        SqliteDatabase reopened;
                        std::string reached;
                        if (reopened.open(pathToString(agedPath)))
                        {
                            SqliteStatement stmt{reopened, "SELECT value FROM SchemaInfo WHERE key = 'schema_version';"};
                            if (stmt.isValid() && stmt.getNextResult())
                            {
                                reached = stmt.getText(0);
                            }
                        }
                        report.check(!reached.empty() && reached == latestVersion,
                            std::format("the {} database ends up stamped at the version a new one is (said '{}', expected '{}')",
                                label,
                                reached,
                                latestVersion));

                        // The rows are still there. Twenty rungs, two of which rebuild a table.
                        const auto scalar = [&reopened](const char *sql)
                        {
                            SqliteStatement stmt{reopened, sql};
                            return (stmt.isValid() && stmt.getNextResult()) ? stmt.getText(0) : std::string{"<query failed>"};
                        };
                        report.check(scalar("SELECT COUNT(*) FROM Tracks") == "2", std::format("{}: both tracks came up the ladder", label));
                        report.check(scalar("SELECT COUNT(*) FROM MixTracks WHERE mix_id = 1") == "3",
                            std::format("{}: so did all three mix rows, including the second copy of the same track", label));
                        report.check(scalar("SELECT COUNT(*) FROM WorkingSetTracks WHERE ws_id = 1") == "2",
                            std::format("{}: and the working set kept its tracks", label));
                        report.check(scalar("SELECT COUNT(*) FROM TracksSearchData") == "2",
                            std::format("{}: the search content is there for both tracks", label));
                    }

                    const auto agedStructure = readSchemaStructure(agedPath, report);
                    report.check(agedStructure == freshStructure,
                        std::format("the {} database ends up structurally identical to a new one ({} vs {} objects)",
                            label,
                            agedStructure.size(),
                            freshStructure.size()));

                    // Name what differs. "Two maps are not equal" is not a bug report, and the whole
                    // value of this check is in what it says when it fails.
                    if (agedStructure != freshStructure)
                    {
                        for (const auto &[name, facts] : freshStructure)
                        {
                            const auto it = agedStructure.find(name);
                            if (it == agedStructure.end())
                            {
                                report.note(std::format("{}: only in a new database: {}", label, name));
                                continue;
                            }
                            for (const auto &fact : facts)
                            {
                                if (std::ranges::find(it->second, fact) == it->second.end())
                                {
                                    report.note(std::format("{}: {}: only a new database has [{}]", label, name, fact));
                                }
                            }
                        }
                        for (const auto &[name, facts] : agedStructure)
                        {
                            const auto it = freshStructure.find(name);
                            if (it == freshStructure.end())
                            {
                                report.note(std::format("{}: only after the ladder: {}", label, name));
                                continue;
                            }
                            for (const auto &fact : facts)
                            {
                                if (std::ranges::find(it->second, fact) == it->second.end())
                                {
                                    report.note(std::format("{}: {}: only the ladder produces [{}]", label, name, fact));
                                }
                            }
                        }
                    }
                };

                if (!freshStructure.empty())
                {
                    climbAndCompare("v12-created-fresh", workRoot / "v12-aged.db", false);
                    climbAndCompare("v12-migrated-up", workRoot / "v12-with-search.db", true);
                }
            }

            writeResultsFile(resultsPath, "jucyaudio migration self test", report);
            spdlog::info("[SelfTest] Migration finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runBackupSelfTest(const std::filesystem::path &selfTestRoot)
        {
            Report report;
            const auto workRoot = selfTestRoot / "backup";
            const auto resultsPath = selfTestRoot / "backup-results.txt";
            const auto dbPath = workRoot / "walcheck.db";

            spdlog::info("[SelfTest] Starting backup self test. Root: {}", pathToString(selfTestRoot));

            std::error_code ec;
            std::filesystem::remove_all(workRoot, ec);
            std::filesystem::create_directories(workRoot, ec);
            if (ec)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(workRoot), ec.message()));
                writeResultsFile(resultsPath, "jucyaudio backup self test", report);
                return 1;
            }

            // A row that exists only in the -wal file. The connection stays open across the backup, so
            // nothing checkpoints it into the main database - which is precisely the state a copy of that
            // main file would fail to capture, and the state a real library is in most of the time.
            constexpr const char *kMarker = "only-in-the-wal";
            {
                SqliteDatabase db;
                if (!db.open(pathToString(dbPath)))
                {
                    report.abort(std::format("Could not create the scratch database {}", pathToString(dbPath)));
                    writeResultsFile(resultsPath, "jucyaudio backup self test", report);
                    return 1;
                }

                const bool prepared = db.execute("PRAGMA journal_mode=WAL;") && db.execute("CREATE TABLE WalCheck (marker TEXT NOT NULL);") &&
                                      db.execute("INSERT INTO WalCheck (marker) VALUES ('only-in-the-wal');");
                report.check(prepared, "a WAL-mode scratch database was created with a committed row");
                if (!prepared)
                {
                    writeResultsFile(resultsPath, "jucyaudio backup self test", report);
                    return 1;
                }

                // Appended to the path rather than rebuilt from a string: pathToString hands back UTF-8,
                // and feeding that to the narrow path constructor puts it straight back through the
                // active code page - the very conversion this codebase uses pathToString to avoid.
                auto walPath = dbPath;
                walPath += "-wal";
                const auto walSize = std::filesystem::exists(walPath, ec) ? std::filesystem::file_size(walPath, ec) : 0;
                report.check(walSize > 0, std::format("the committed row is still in the -wal file ({} bytes), not the database", walSize));

                // Backed up while that connection is still open, exactly as it would be with the app
                // running. Forced, because there are no existing backups to age out and this test is
                // about the mechanism rather than the schedule.
                config::RootSettings settings;
                DatabaseBackupManager manager;
                const auto outcome = manager.performBackupCheck(settings, dbPath, false, true, true);

                report.check(outcome.attempted, "the backup manager attempted a backup when forced");
                report.check(outcome.succeeded, std::format("the backup reports success (error: '{}')", outcome.errorMessage));
                report.check(!outcome.backupFile.empty() && std::filesystem::exists(outcome.backupFile, ec), "the backup file exists");

                // Nothing half-finished left lying around under a name that would later be counted,
                // pruned against, and one day restored from.
                // Any .partial at all, not one predicted name: temporaries carry a unique per-attempt
                // suffix now, so checking a computed path would be checking one that never existed.
                size_t partials = 0;
                for (const auto &entry : std::filesystem::directory_iterator{workRoot, ec})
                {
                    partials += (entry.path().extension() == ".partial") ? 1 : 0;
                }
                report.check(partials == 0, std::format("no .partial file was left behind (found {})", partials));
                // Distinct from succeeded: a backup can be published correctly and still leave a
                // temporary behind if something held a handle on it. That is a warning, not a failure,
                // and the two should not be conflated here either.
                report.check(outcome.warningMessage.empty(), std::format("the backup reports no housekeeping warning (got: '{}')", outcome.warningMessage));

                if (outcome.succeeded)
                {
                    // The check the whole item exists for. A copy_file backup opens, has a WalCheck
                    // table, and has no rows in it.
                    SqliteDatabase restored;
                    if (restored.open(pathToString(outcome.backupFile)))
                    {
                        std::string found;
                        SqliteStatement stmt{restored};
                        const bool queried = stmt.query(
                            [&found, &stmt]() -> bool
                            {
                                found = stmt.getText(0);
                                return true;
                            },
                            "SELECT marker FROM WalCheck;");
                        report.check(queried && found == kMarker,
                            std::format("the backup contains the row that was only in the WAL (found '{}')", found));
                    }
                    else
                    {
                        report.check(false, "the backup file could be opened as a database");
                    }
                }
            }

            writeResultsFile(resultsPath, "jucyaudio backup self test", report);
            spdlog::info("[SelfTest] Backup test finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runTimelineSelfTest(const std::filesystem::path &selfTestRoot, const std::filesystem::path &databasePath)
        {
            Report report;
            // Its own library again: this suite writes to the mix it builds, and the checks are about
            // what a second writer does to a timeline that is already showing it.
            const auto workRoot = selfTestRoot / "timeline-library";
            const auto resultsPath = selfTestRoot / "timeline-results.txt";

            spdlog::info("[SelfTest] Starting timeline self test. Root: {}", pathToString(selfTestRoot));

            const auto stop = [&report, &resultsPath](const std::string &why)
            {
                report.abort(why);
                writeResultsFile(resultsPath, "jucyaudio timeline self test", report);
                return 1;
            };

            std::error_code ec;
            std::filesystem::remove_all(workRoot, ec);
            if (ec)
            {
                return stop(std::format("Could not clear {}: {}", pathToString(workRoot), ec.message()));
            }

            const auto albumPath = workRoot / kAlbumFolder;
            std::filesystem::create_directories(albumPath, ec);
            if (ec)
            {
                return stop(std::format("Could not create {}: {}", pathToString(albumPath), ec.message()));
            }

            for (int i = 1; i <= kTrackCount; ++i)
            {
                const auto file = albumPath / std::format("tl{:02}.wav", i);
                if (!writeSilentWav(file, static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
                {
                    return stop(std::format("Could not write the fixture {}", pathToString(file)));
                }
            }

            auto &db = theTrackLibrary.getTrackDatabase();
            auto &mixManager = theTrackLibrary.getMixManager();

            if (!db.getLibraryRootManager().addRoot(pathToString(workRoot)).has_value())
            {
                return stop("Could not add the timeline scratch library as a library root.");
            }

            const auto rootFolderId = db.getFolderDatabase().findOrCreateFolderByPath(workRoot);
            if (rootFolderId <= 0 || !runScan({rootFolderId}, false, report, "timeline fixture discovery"))
            {
                return stop("Could not discover the timeline scratch library.");
            }

            const auto tracks = tracksUnder(db, rootFolderId);
            if (static_cast<int>(tracks.size()) != kTrackCount)
            {
                return stop(std::format("Expected {} fixture tracks, found {}.", kTrackCount, tracks.size()));
            }

            std::vector<MixTrack> mixTracks;
            for (const auto &entry : tracks)
            {
                MixTrack mixTrack{};
                mixTrack.trackId = entry.second.trackId;
                mixTrack.orderInMix = static_cast<int>(mixTracks.size());
                mixTrack.cueStart = Duration_t{10 * (mixTrack.orderInMix + 1)};
                mixTrack.cueEnd = Duration_t{kFixtureDurationMs};
                mixTracks.push_back(mixTrack);
            }

            MixInfo mixInfo{};
            mixInfo.name = "SelfTest Timeline Mix";
            mixInfo.totalDuration = Duration_t{kTrackCount * kFixtureDurationMs};
            if (!mixManager.createOrUpdateMix(mixInfo, mixTracks) || mixInfo.mixId <= 0)
            {
                return stop("Could not create the timeline fixture mix.");
            }
            const auto mixId = mixInfo.mixId;
            report.note(std::format("built a {}-track mix (id {}) over its own scratch library", kTrackCount, mixId));

            // --- 1. The loader says when its rows changed ---
            //
            // This is what the timeline's protection is built on, so it is checked on its own first: a
            // guard comparing a number that never moves is not a guard.
            audio::MixProjectLoader loader;
            if (!loader.loadMix(mixId))
            {
                return stop("Could not load the fixture mix into a MixProjectLoader.");
            }

            const auto afterFirstLoad = loader.getContentsGeneration();
            if (!loader.reloadFromDatabase())
            {
                return stop("Could not reload the fixture mix.");
            }
            const auto afterReload = loader.getContentsGeneration();
            report.check(afterReload != afterFirstLoad,
                std::format("a reload changes the contents generation ({} -> {})", afterFirstLoad, afterReload));

            // Through reorderTracks, the public way in: a single move is handed straight to
            // reorderSingleTrack, which is where the rows are moved and the generation is bumped.
            const auto firstTrackId = loader.getMixTracks().front().trackId;
            report.check(loader.reorderTracks({{firstTrackId, 0}}), "a reorder to the position a track already holds succeeds");
            report.check(loader.getContentsGeneration() == afterReload, "that no-op reorder leaves the generation alone");

            report.check(loader.reorderTracks({{firstTrackId, 2}}), "a reorder that actually moves a track succeeds");
            const auto afterReorder = loader.getContentsGeneration();
            report.check(afterReorder != afterReload,
                std::format("an in-place reorder changes the generation ({} -> {})", afterReload, afterReorder));

            // The third way the rows change, and the third bump: removeTrackAtOrder drops a row and
            // renumbers the rest in memory, without a load and without touching the database.
            const auto rowsBeforeRemoval = loader.getMixTracks().size();
            report.check(loader.removeTrackAtOrder(0), "removing a row from the loaded mix in place succeeds");
            report.check(loader.getMixTracks().size() + 1 == rowsBeforeRemoval,
                std::format("that removal took one row out ({} -> {})", rowsBeforeRemoval, loader.getMixTracks().size()));
            const auto afterRemoval = loader.getContentsGeneration();
            report.check(afterRemoval != afterReorder,
                std::format("an in-place removal changes the generation ({} -> {})", afterReorder, afterRemoval));

            // Back to what the database holds, so the checks below start from a loader that agrees
            // with it - the removal above was in memory only, and the reorder was never saved.
            if (!loader.reloadFromDatabase())
            {
                return stop("Could not reload the fixture mix after the reorder checks.");
            }

            // --- 2. The views survive a reload nobody told the timeline about ---
            //
            // The layout pass reads both halves of every TrackView. While those were pointers into the
            // loader's vectors, the reload below freed everything they addressed and this walk read it.
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::AudioThumbnailCache thumbnails{16};
            ui::TimelineComponent timeline{formats, thumbnails};
            timeline.setSize(4000, 600);

            if (!timeline.populateFrom(&loader))
            {
                return stop("The timeline could not be populated from the fixture mix.");
            }
            report.check(timeline.getNumChildComponents() == kTrackCount,
                std::format("the timeline built {} track components (found {})", kTrackCount, timeline.getNumChildComponents()));

            const auto generationBeforeTheReload = loader.getContentsGeneration();
            if (!loader.reloadFromDatabase())
            {
                return stop("Could not reload the mix behind the back of the timeline.");
            }
            report.check(loader.getContentsGeneration() != generationBeforeTheReload,
                "that reload is visible in the generation, which is what the timeline compares against");

            // No repopulation in between, deliberately. Nothing here can assert the absence of a
            // use-after-free - a released allocation often still reads back fine - so what is asserted
            // is that the walk completes and the views are still there afterwards.
            // Through setSize, because resized() is private: a new width runs the same layout pass,
            // which is the walk over every view that used to read the freed vectors.
            timeline.setSize(4200, 600);
            report.check(timeline.getNumChildComponents() == kTrackCount, "the layout pass after that reload leaves the track components intact");

            // --- 3. An edit whose positions came from stale views is refused ---

            // Repopulated first, so the clipboard copy is taken from a timeline that agrees with the
            // loader: what is being tested below is the paste, not the copy.
            if (!timeline.populateFrom(&loader))
            {
                return stop("The timeline could not be repopulated before the clipboard check.");
            }

            auto *const firstComponent = dynamic_cast<ui::MixTrackComponent *>(timeline.getChildComponent(0));
            if (firstComponent == nullptr)
            {
                return stop("The first child of the timeline is not a track component.");
            }
            timeline.setSelectedTrack(firstComponent);
            timeline.copySelectedTrackToClipboard();

            const auto rowsBefore = mixManager.getMixTracks(mixId).size();
            if (!loader.reloadFromDatabase())
            {
                return stop("Could not reload the mix before the stale-paste check.");
            }

            timeline.pasteFromClipboard(false);
            const auto rowsAfterStalePaste = mixManager.getMixTracks(mixId).size();
            report.check(rowsAfterStalePaste == rowsBefore,
                std::format("a paste from views the loader has replaced is refused (rows {} -> {})", rowsBefore, rowsAfterStalePaste));

            // The other half of the same guard: it has to let a current timeline through, or it would
            // pass the check above by refusing everything.
            if (!timeline.populateFrom(&loader))
            {
                return stop("The timeline could not be repopulated after the refused paste.");
            }
            timeline.pasteFromClipboard(false);
            const auto rowsAfterFreshPaste = mixManager.getMixTracks(mixId).size();
            report.check(rowsAfterFreshPaste == rowsBefore + 1,
                std::format("the same paste goes through once the views are current again (rows {} -> {})", rowsBefore, rowsAfterFreshPaste));

            // --- 3. A row the timeline cannot resolve still holds its place ---
            //
            // The disagreement this suite exists to pin down. The timeline builds a view only for a
            // mix row whose TrackInfo resolves, so its views are a subsequence of the rows - and it
            // used to chain each view's position off the previous *view*, which meant an offline track
            // pulled everything after it earlier. Playback and the M3U export advanced through every
            // row and so kept the gap; the length the editor reported skipped the row but still took
            // the skipped row's attachTo, which was a third answer again. The picture the user edited
            // on was none of them.
            //
            // The row still has no component - there is nothing to draw - but it keeps its place.
            //
            // Staged over a second connection with foreign keys off, because nothing reachable
            // through the public interfaces can produce it: MixTracks.track_id cascades, so deleting
            // a track the ordinary way takes its mix row with it. Last in this suite, since it leaves
            // the scratch mix pointing at a track that is gone.
            {
                // The fixture mix is built with cue points but no attach points, so every track starts
                // at zero and the components sit on top of each other - a layout in which no rule can
                // be told from another. Give it a real ATTACH chain first: each track hands over at
                // the end of its fixture audio, so the tracks are one duration apart.
                {
                    auto spread = mixManager.getMixTracks(mixId);
                    bool given{true};
                    for (auto &row : spread)
                    {
                        row.attachTo = Duration_t{kFixtureDurationMs};
                        row.attachFrom = Duration_t{0};
                        given = given && mixManager.updateMixTrack(mixId, row);
                    }
                    report.check(given, "the fixture mix could be given a real attach chain to lay out");
                    if (!given)
                    {
                        writeResultsFile(resultsPath, "jucyaudio timeline self test", report);
                        return 1;
                    }
                }

                if (!loader.reloadFromDatabase() || !timeline.populateFrom(&loader))
                {
                    return stop("Could not return the timeline to the loaded mix before the offline check.");
                }

                const auto rows = loader.getMixTracks();
                if (rows.size() < 3 || timeline.getNumChildComponents() != static_cast<int>(rows.size()))
                {
                    return stop("The offline check needs at least three resolved rows to work with.");
                }

                // Where every component sits while everything resolves. Read from the components
                // themselves rather than from the loader, so this is the layout and not the arithmetic.
                const auto viewsBefore = timeline.getNumChildComponents();
                std::vector<int> xBefore;
                for (int i = 0; i < viewsBefore; ++i)
                {
                    xBefore.push_back(timeline.getChildComponent(i)->getX());
                }

                // Which also says the layout is worth reading at all: components that all sat at the
                // same x would agree under any rule, which is how the first version of this check
                // passed while proving nothing.
                report.check(std::ranges::is_sorted(xBefore, std::less_equal<>{}) && xBefore.front() < xBefore.back(),
                    std::format("the attach chain spreads the components out (x {} .. {})", xBefore.front(), xBefore.back()));

                const auto vanishingTrackId = rows[1].trackId;
                {
                    SqliteDatabase saboteur;
                    const bool staged = saboteur.open(pathToString(databasePath)) &&
                        saboteur.execute("PRAGMA foreign_keys = OFF;") &&
                        saboteur.execute(std::format("DELETE FROM Tracks WHERE track_id = {};", vanishingTrackId).c_str());
                    report.check(staged, std::format("the second track ({}) could be made unresolvable", vanishingTrackId));
                    if (!staged)
                    {
                        writeResultsFile(resultsPath, "jucyaudio timeline self test", report);
                        return 1;
                    }
                }

                if (!loader.reloadFromDatabase() || !timeline.populateFrom(&loader))
                {
                    return stop("Could not repopulate the timeline after the second track went missing.");
                }

                // Stated on its own, because the other half of telling a failed query from an offline
                // track is that an offline track must not be treated as a failure. The query answered
                // with fewer rows than the mix has, and that is a mix the user can still open and edit.
                report.check(loader.isLoaded(), "a mix with a track that cannot be resolved still loads");

                report.check(timeline.getNumChildComponents() == viewsBefore - 1,
                    std::format("the unresolvable row gets no component ({} -> {})", viewsBefore, timeline.getNumChildComponents()));
                report.check(loader.getMixTracks().size() == rows.size(),
                    std::format("but its row is still in the mix ({} rows)", loader.getMixTracks().size()));

                // The check the whole change is about: every surviving track is drawn exactly where it
                // was, so the missing one leaves a gap instead of pulling the rest earlier. Row 1 is
                // the one that went, so the components now correspond to rows 0, 2, 3, ...
                std::vector<int> xExpected{xBefore};
                xExpected.erase(xExpected.begin() + 1);

                std::vector<int> xAfter;
                for (int i = 0; i < timeline.getNumChildComponents(); ++i)
                {
                    xAfter.push_back(timeline.getChildComponent(i)->getX());
                }

                const auto joined = [](const std::vector<int> &values)
                {
                    std::string text;
                    for (const auto value : values)
                    {
                        text += (text.empty() ? "" : ",") + std::to_string(value);
                    }
                    return text;
                };

                report.check(xAfter == xExpected,
                    std::format("every track after the missing one is drawn where it always was, gap and all (got {}, expected {})",
                        joined(xAfter),
                        joined(xExpected)));
            }

            // --- 4. The exported file, against real PCM ---
            //
            // Two separate statements, and the second is not the one you would guess.
            //
            // With every track resolved, the samples that come out have to sit where the ATTACH chain
            // says - checked against the file rather than against the positions the exporter computed,
            // so the shared walk is verified end to end.
            //
            // With a track unresolvable, the export does not render a gap: prepareActiveTrackSources
            // refuses the whole export the moment a row has no TrackInfo. So there is no output to
            // compare, and the contract worth pinning is the refusal itself. The positions the
            // exporter now shares with everything else are therefore not observable in a file for
            // that case - they matter to the length the editor reports, the M3U export, playback and
            // the timeline.
            //
            // Its own library, with audible fixtures: everything else here is silent, and an onset
            // cannot be found in silence.
            {
                const auto exportRoot = selfTestRoot / "timeline-export";
                std::filesystem::remove_all(exportRoot, ec);
                std::filesystem::create_directories(exportRoot, ec);
                if (ec)
                {
                    return stop(std::format("Could not create {}: {}", pathToString(exportRoot), ec.message()));
                }

                constexpr uint32_t kToneSamples = 44100; // one second each
                for (int i = 1; i <= 3; ++i)
                {
                    if (!writeSilentWav(exportRoot / std::format("tone{:02}.wav", i), kToneSamples, 12000))
                    {
                        return stop("Could not write the audible export fixtures.");
                    }
                }

                if (!db.getLibraryRootManager().addRoot(pathToString(exportRoot)).has_value())
                {
                    return stop("Could not add the export fixture library as a root.");
                }
                const auto exportFolderId = db.getFolderDatabase().findOrCreateFolderByPath(exportRoot);
                if (exportFolderId <= 0 || !runScan({exportFolderId}, false, report, "export fixture discovery"))
                {
                    return stop("Could not discover the export fixture library.");
                }

                const auto toneTracks = tracksUnder(db, exportFolderId);
                if (toneTracks.size() != 3)
                {
                    return stop(std::format("Expected 3 audible fixtures, found {}.", toneTracks.size()));
                }

                // One second of audio each, handing over at two seconds, so the three sit at 0, 2 and 4
                // seconds with a second of silence between them. The silence is what makes the onsets
                // findable: three tracks butted together are one continuous loud run.
                std::vector<MixTrack> toneMix;
                for (const auto &entry : toneTracks)
                {
                    MixTrack row{};
                    row.trackId = entry.second.trackId;
                    row.orderInMix = static_cast<int>(toneMix.size());
                    row.attachTo = Duration_t{2000};
                    row.attachFrom = Duration_t{0};
                    toneMix.push_back(row);
                }

                MixInfo toneMixInfo{};
                toneMixInfo.name = "SelfTest Export Gap Mix";
                if (!mixManager.createOrUpdateMix(toneMixInfo, toneMix) || toneMixInfo.mixId <= 0)
                {
                    return stop("Could not create the export fixture mix.");
                }

                // Where each track's audio begins in an exported file, and how long that file is. A run
                // is a stretch of loud samples with silence before it; the fixtures are square waves,
                // so a track is uniformly loud from its first sample and the runs are its tracks.
                struct RenderedAudio
                {
                    std::vector<int64_t> onsets;
                    int64_t totalSamples{-1};
                };

                const auto renderedAudioOf = [&report](const std::filesystem::path &file)
                {
                    RenderedAudio rendered;

                    juce::AudioFormatManager formatsForReading;
                    formatsForReading.registerBasicFormats();
                    std::unique_ptr<juce::AudioFormatReader> reader{
                        formatsForReading.createReaderFor(juce::File{juce::String{pathToString(file)}})};
                    if (!reader)
                    {
                        report.abort(std::format("Could not read back {}", pathToString(file)));
                        return rendered;
                    }

                    rendered.totalSamples = reader->lengthInSamples;
                    juce::AudioBuffer<float> audio{static_cast<int>(reader->numChannels), static_cast<int>(rendered.totalSamples)};
                    reader->read(&audio, 0, static_cast<int>(rendered.totalSamples), 0, true, true);

                    bool inRun{false};
                    for (int64_t i = 0; i < rendered.totalSamples; ++i)
                    {
                        const bool loud = std::abs(audio.getSample(0, static_cast<int>(i))) > 0.05f;
                        if (loud && !inRun)
                        {
                            rendered.onsets.push_back(i);
                        }
                        inRun = loud;
                    }
                    return rendered;
                };

                audio::ActiveExportSettings toneSettings{};
                toneSettings.outputPath = exportRoot / "whole.wav";
                const audio::MixExporter toneExporter{};

                const auto wholeExport = toneExporter.exportMixToFile(toneMixInfo.mixId, toneSettings, nullptr);
                report.check(wholeExport.success, std::format("the three-track mix exports (said: '{}')", wholeExport.message));
                if (!wholeExport.success)
                {
                    writeResultsFile(resultsPath, "jucyaudio timeline self test", report);
                    return 1;
                }

                // What the ATTACH chain says, checked against the samples that came out. A tolerance,
                // because the exporter may fade a track in and the threshold then trips a little late;
                // 50 ms is far tighter than the 2000 ms a misplaced track would be out by.
                const auto rendered = renderedAudioOf(toneSettings.outputPath);
                const std::vector<int64_t> expectedOnsets{0, 88200, 176400};
                report.check(rendered.onsets.size() == expectedOnsets.size(),
                    std::format("the exported file holds three separate tracks (found {})", rendered.onsets.size()));

                if (rendered.onsets.size() == expectedOnsets.size())
                {
                    bool placed{true};
                    for (size_t i = 0; i < expectedOnsets.size(); ++i)
                    {
                        placed = placed && std::llabs(rendered.onsets[i] - expectedOnsets[i]) < 2205;
                    }
                    report.check(placed,
                        std::format("and each one starts where the attach chain puts it (samples {}, {}, {}; expected 0, 88200, 176400)",
                            rendered.onsets[0],
                            rendered.onsets[1],
                            rendered.onsets[2]));
                }

                // Now the middle one goes, the same way as above: over its own connection with foreign
                // keys off, because the cascade would take the mix row with it.
                const auto middleTrackId = toneMix[1].trackId;
                {
                    SqliteDatabase saboteur;
                    const bool staged = saboteur.open(pathToString(databasePath)) &&
                        saboteur.execute("PRAGMA foreign_keys = OFF;") &&
                        saboteur.execute(std::format("DELETE FROM Tracks WHERE track_id = {};", middleTrackId).c_str());
                    report.check(staged, std::format("the middle exported track ({}) could be made unresolvable", middleTrackId));
                    if (!staged)
                    {
                        writeResultsFile(resultsPath, "jucyaudio timeline self test", report);
                        return 1;
                    }
                }

                // And the contract when one cannot be resolved, which is not "render a gap":
                // prepareActiveTrackSources refuses the whole export rather than producing a mix that
                // is quietly missing a track. Pinned down because it is the reason the exporter's
                // positions are not observable in a file for this case - and because a change that
                // made the export tolerant would need the index coupling in contributeFromActiveSource
                // dealt with first, which the comment there now says.
                //
                // Re-exported over the file the first export produced, which is the ordinary way to
                // update one, and the case that used to destroy it: the target was deleted to make the
                // writer before either of the steps that can fail had run.
                const auto reExportedPath = toneSettings.outputPath;
                const auto gappedExport = toneExporter.exportMixToFile(toneMixInfo.mixId, toneSettings, nullptr);
                report.check(!gappedExport.success, "an export whose track cannot be resolved is refused, not rendered short");

                // The file that was already there is still there, and is still the mix that was
                // exported into it - not truncated, not emptied, not replaced by a partial render.
                report.check(std::filesystem::exists(reExportedPath, ec), "the export it was written over still exists");
                if (std::filesystem::exists(reExportedPath, ec))
                {
                    const auto survivor = renderedAudioOf(reExportedPath);
                    report.check(survivor.totalSamples == rendered.totalSamples,
                        std::format("and is the same length as before the failed re-export ({} vs {} samples)",
                            survivor.totalSamples,
                            rendered.totalSamples));
                    report.check(survivor.onsets == rendered.onsets,
                        std::format("with its tracks still where they were ({} runs)", survivor.onsets.size()));
                }

                // And nothing is left lying beside it. The partial file is what the render actually
                // wrote to; a failed export has to take it away again.
                auto partialPath{reExportedPath};
                partialPath += ".jucyaudio-part";
                report.check(!std::filesystem::exists(partialPath, ec),
                    std::format("and the partial render was cleaned up ({})", pathToString(partialPath)));

                // --- A re-export that succeeds does replace the file ---
                //
                // The other half, and the one a preservation change can quietly break: every check so
                // far would pass if the commit never happened at all. The mix is shortened first, so
                // the replacement is provable from the bytes rather than from a timestamp - the file
                // that comes out has to be the new render and not the old one left in place.
                {
                    auto shortened = mixManager.getMixTracks(toneMixInfo.mixId);
                    report.check(shortened.size() == 3, std::format("the export fixture still has three rows (has {})", shortened.size()));

                    // The row whose track went missing is put back, so the mix can render again, and
                    // the last one is dropped so the result is visibly shorter than what is on disk.
                    bool restored{true};
                    for (auto &row : shortened)
                    {
                        row.attachTo = Duration_t{1000};
                        restored = restored && mixManager.updateMixTrack(toneMixInfo.mixId, row);
                    }
                    report.check(restored, "the export fixture could be given a shorter attach chain");

                    // The unresolvable row is still unresolvable, so the render would still be refused.
                    // Put the track back the same way it was taken away.
                    {
                        SqliteDatabase restorer;
                        const bool putBack = restorer.open(pathToString(databasePath)) &&
                            restorer.execute("PRAGMA foreign_keys = OFF;") &&
                            restorer.execute(std::format("INSERT INTO Tracks (track_id, folder_id, filename, title, duration) "
                                                         "SELECT {}, folder_id, 'tone02.wav', 'restored', duration FROM Tracks WHERE track_id = {};",
                                middleTrackId,
                                toneMix[0].trackId)
                                                 .c_str());
                        report.check(putBack, "the missing track could be put back for the replacement check");
                    }

                    const auto replacement = toneExporter.exportMixToFile(toneMixInfo.mixId, toneSettings, nullptr);
                    report.check(replacement.success, std::format("the shortened mix re-exports over the old file (said: '{}')", replacement.message));

                    if (replacement.success)
                    {
                        const auto replaced = renderedAudioOf(reExportedPath);
                        report.check(replaced.totalSamples > 0 && replaced.totalSamples != rendered.totalSamples,
                            std::format("and the file on disk is the new render, not the old one ({} vs {} samples)",
                                replaced.totalSamples,
                                rendered.totalSamples));
                        report.check(!std::filesystem::exists(partialPath, ec), "with no partial left beside it");
                    }
                }

                // --- Cancelling an export stops it, and keeps what was there ---
                //
                // The progress callback is the cancel button: it returns false to say stop, and being a
                // parameter of exportMixToFile it needs nothing of the export to be injectable. A failed
                // write is checked further down and takes more arranging - see the block that subclasses
                // the WAV export - but neither of them needs a hook that exists only for the test.
                //
                // The callback used to return void, and the two callers that wanted to cancel already
                // handed back `!shouldCancel` for std::function to discard. So the render ran to
                // completion and wrote the file anyway, over whatever was there.
                {
                    const auto beforeCancelling = renderedAudioOf(reExportedPath);
                    report.check(beforeCancelling.totalSamples > 0, "there is a finished export to protect from a cancelled one");

                    // The mix is lengthened again first, so that "the file did not change" means
                    // something. Cancelling a render of the same mix that is already on disk would
                    // produce an identical file if the cancel were ignored, and the check below could
                    // not tell the two apart.
                    {
                        auto lengthened = mixManager.getMixTracks(toneMixInfo.mixId);
                        bool given{true};
                        for (auto &row : lengthened)
                        {
                            row.attachTo = Duration_t{2000};
                            given = given && mixManager.updateMixTrack(toneMixInfo.mixId, row);
                        }
                        report.check(given, "the mix could be lengthened, so a completed render would differ from the file on disk");
                    }

                    // Everything before the render is allowed through, and the first report from the
                    // mixing loop is refused. So this is a cancel in the middle of rendering and not a
                    // refusal at the "Starting export..." report, which is its own case: nothing has
                    // been written then, and run() refuses before the first block rather than after it.
                    //
                    // Only the render's own reports are counted. The export also reports starting and
                    // reports the error afterwards, and those say nothing about whether it stopped; the
                    // mixing loop's messages are the ones that mean "another block went to disk".
                    int renderReports{0};
                    const auto cancelled = toneExporter.exportMixToFile(toneMixInfo.mixId,
                        toneSettings,
                        [&renderReports](float, const std::string &message)
                        {
                            if (!message.starts_with("Exporting WAV..."))
                            {
                                return true;
                            }
                            ++renderReports;
                            return false;
                        });

                    report.check(!cancelled.success, "a cancelled export reports failure rather than success");

                    // The count is what proves it stopped. A render that ignored the answer would report
                    // once per block - about thirty times for this mix - and then finish.
                    report.check(renderReports == 1,
                        std::format("and it stopped at the first refusal rather than running on ({} render reports)", renderReports));

                    // Which is the point of stopping: the file the user already had is still theirs.
                    const auto afterCancelling = renderedAudioOf(reExportedPath);
                    report.check(afterCancelling.totalSamples == beforeCancelling.totalSamples,
                        std::format("the export that was already there is untouched ({} vs {} samples)",
                            afterCancelling.totalSamples,
                            beforeCancelling.totalSamples));
                    report.check(afterCancelling.onsets == beforeCancelling.onsets, "with its tracks still where they were");
                    report.check(!std::filesystem::exists(partialPath, ec), "and the cancelled render left no partial behind");

                    // Refusing the very first report, before any work is done. The contract is that
                    // false means stop, so this has to stop too - not set up, write a block, and then
                    // notice at the next report.
                    int reportsBeforeRefusing{0};
                    const auto refusedAtOnce = toneExporter.exportMixToFile(toneMixInfo.mixId,
                        toneSettings,
                        [&reportsBeforeRefusing](float, const std::string &)
                        {
                            ++reportsBeforeRefusing;
                            return false;
                        });

                    report.check(!refusedAtOnce.success, "refusing the very first report cancels the export too");
                    report.check(reportsBeforeRefusing == 2,
                        std::format("and nothing is rendered after it - only the start and the error are reported ({} reports)",
                            reportsBeforeRefusing));
                    report.check(renderedAudioOf(reExportedPath).totalSamples == beforeCancelling.totalSamples,
                        "the file that was already there survives that too");

                    // And the last moment there is: after the render has finished and the file has been
                    // closed, patched and validated, with only the commit left. Everything else is
                    // allowed through, so this cancels at exactly one point and nowhere earlier - the
                    // whole mix really is rendered before it is thrown away.
                    //
                    // The gate there used to read what the last report had left behind rather than
                    // asking again, and closing a WAV means patching its header and reading it back, so
                    // a cancel during that work would have been answered with a stale yes.
                    int lateReports{0};
                    const auto cancelledAtTheEnd = toneExporter.exportMixToFile(toneMixInfo.mixId,
                        toneSettings,
                        [&lateReports](float, const std::string &message)
                        {
                            if (!message.starts_with("Saving the exported file"))
                            {
                                return true;
                            }
                            ++lateReports;
                            return false;
                        });

                    report.check(!cancelledAtTheEnd.success, "cancelling while the finished file is being saved still cancels");
                    report.check(lateReports == 1,
                        std::format("and that question is asked exactly once, immediately before the commit ({} times)", lateReports));

                    const auto afterLateCancel = renderedAudioOf(reExportedPath);
                    report.check(afterLateCancel.totalSamples == beforeCancelling.totalSamples,
                        std::format("the export that was already there is still untouched ({} vs {} samples)",
                            afterLateCancel.totalSamples,
                            beforeCancelling.totalSamples));
                    report.check(!std::filesystem::exists(partialPath, ec), "and the finished render was discarded rather than committed");
                }

                // --- A write that fails part way through leaves the previous export alone ---
                //
                // The propagation this proves: a refused write makes the mixing loop fail, run()
                // discards the partial instead of committing it, and the export that was already on
                // disk is untouched. All three at once, because any one of them alone would pass on a
                // build where another was broken.
                //
                // The refusal is injected by subclassing the WAV export and giving its writer a stream
                // that accepts 64 KB and then refuses. That is a substitution of the output stream and
                // nothing else - the mixing loop, its write check, releaseOutput and run() are the
                // shipping code. See ExportWavMixImplementation, which is not final for this reason.
                {
                    constexpr int64_t kRefuseAfterBytes{64 * 1024};

                    const auto beforeFailedWrite = renderedAudioOf(reExportedPath);
                    report.check(beforeFailedWrite.totalSamples > 0, "there is a finished export for a failed write to threaten");
                    const auto sizeBefore = std::filesystem::file_size(reExportedPath, ec);

                    WriteRefusalLog log;
                    {
                        // Its own settings object, held here: ExportMixImplementation keeps a reference
                        // to it for the whole run.
                        audio::ActiveExportSettings refusingSettings{};
                        refusingSettings.outputPath = reExportedPath;

                        WriteRefusingWavExport refusing{toneMixInfo.mixId, refusingSettings, kRefuseAfterBytes, log};
                        const auto refused = refusing.run();
                        report.check(!refused.success, std::format("an export whose writes start failing reports failure (said: '{}')", refused.message));

                        // Which step failed, not merely that one did. The write check is not the only
                        // thing standing between a refused write and a committed truncation:
                        // releaseOutput reads the finished file back and compares its length, and it
                        // catches the same fault a moment later. So a check that only asked whether the
                        // export failed would pass with the write check deleted, and would be pinning
                        // the second guard while claiming to pin the first.
                        report.check(refused.message.find("Run Mixing Loop") != std::string::npos,
                            std::format("and fails in the mixing loop, where the write was refused, rather than later (said: '{}')", refused.message));
                    }

                    // Without these two the checks below would pass on an export that failed for some
                    // other reason, or never started rendering at all.
                    report.check(log.refusals > 0, std::format("the injected stream refused at least one write ({} refusals)", log.refusals));
                    report.check(log.bytesAccepted >= kRefuseAfterBytes,
                        std::format("and did so after the render was under way, not at the header ({} bytes accepted)", log.bytesAccepted));

                    report.check(std::filesystem::exists(reExportedPath, ec) && std::filesystem::file_size(reExportedPath, ec) == sizeBefore,
                        std::format("the export that was already there is untouched ({} bytes)", sizeBefore));

                    const auto afterFailedWrite = renderedAudioOf(reExportedPath);
                    report.check(afterFailedWrite.totalSamples == beforeFailedWrite.totalSamples,
                        std::format("and still reads back as the render it was ({} samples)", afterFailedWrite.totalSamples));
                    report.check(afterFailedWrite.onsets == beforeFailedWrite.onsets, "with its audio where it was");

                    report.check(!std::filesystem::exists(partialPath, ec), "and the truncated partial was discarded rather than committed");
                }

                // --- The same, through the MP3 path ---
                //
                // Its own writer, its own output stream and its own releaseOutput override, none of
                // which the WAV checks above touch. Kept small: that it renders, that a refused
                // re-export leaves the previous file alone, and that no partial survives either way.
                {
                    audio::ActiveExportSettings mp3Settings{};
                    mp3Settings.outputPath = exportRoot / "gap.mp3";

                    // Metadata, deliberately. Without it LAME emits no ID3v2 tag, the placeholder
                    // frame lands at offset zero, and the offset bookkeeping this suite exists to
                    // check is right whether or not anyone kept it. With a tag in front, an exporter
                    // that recorded the wrong offset writes the finished frame over that tag instead
                    // of over the placeholder.
                    mp3Settings.artist = "JucyAudio Self Test";
                    mp3Settings.album = "Export Format Checks";
                    mp3Settings.title = "Info frame placement";
                    mp3Settings.trackNumber = "1";
                    mp3Settings.year = "2026";
                    mp3Settings.genre = "Other";

                    // Deliberately past the 10 KiB the exporter used to reserve on the stack. Nothing
                    // limits the length of this field between the export dialog and
                    // lame_get_id3v2_tag, and when the tag outgrew that buffer LAME copied nothing and
                    // returned the size it wanted, which then went to write() as a length - reading
                    // off the end of the stack and into the file. A tag this size is unusual but it is
                    // the user's to ask for, so the export has to carry it rather than refuse or
                    // truncate it.
                    mp3Settings.comment.reserve(kOversizedCommentBytes + 64);
                    while (mp3Settings.comment.size() < kOversizedCommentBytes)
                    {
                        mp3Settings.comment += "This comment is here to outgrow a ten kilobyte buffer. ";
                    }
                    auto mp3Partial{mp3Settings.outputPath};
                    mp3Partial += ".jucyaudio-part";

                    const auto mp3Export = toneExporter.exportMixToFile(toneMixInfo.mixId, mp3Settings, nullptr);
                    report.check(mp3Export.success, std::format("the mix exports to MP3 (said: '{}')", mp3Export.message));
                    report.check(std::filesystem::exists(mp3Settings.outputPath, ec), "the MP3 was written");
                    report.check(!std::filesystem::exists(mp3Partial, ec), "and its partial was committed rather than left behind");

                    // What LAME's tag frame says about the file it heads, read back off the bytes.
                    //
                    // The encoder reserves an empty frame at the head of the audio and requires the
                    // finished tag - the one that knows how many frames and bytes there turned out to
                    // be - to replace it. This exporter appended it after the last audio frame instead,
                    // so every file it produced carried a placeholder in front declaring nothing and a
                    // duplicate behind. Players read the one in front, which is why the symptom was a
                    // wrong duration and seeking that landed in the wrong place, in other people's
                    // players rather than anywhere this application would notice.
                    //
                    // Both counts are checked because either alone can be right by accident: a zeroed
                    // placeholder declares 0 for both, and so does a frame that was never filled in.
                    if (std::filesystem::exists(mp3Settings.outputPath, ec))
                    {
                        const auto exported = describeMp3Fixture(mp3Settings.outputPath, 0);

                        // The tag has to be there, or this check is back to testing the offset-zero
                        // case where a wrong offset cannot be told from a right one.
                        report.check(exported.startsWithId3v2, "the exported MP3 carries the ID3v2 tag its settings asked for");
                        report.check(exported.id3v2Bytes > kOldId3v2BufferBytes,
                            std::format("that tag is larger than the {} bytes the exporter used to reserve on the stack, so this is the case that used to read past it ({} bytes)",
                                kOldId3v2BufferBytes,
                                exported.id3v2Bytes));
                        // The placeholder frame follows the tag immediately, so the first sync lands
                        // exactly at the tag's declared end when the whole tag was written. A short or
                        // garbage tag puts it somewhere else.
                        report.check(exported.firstSyncAt == exported.id3v2Bytes,
                            std::format("the whole tag was written - the audio starts at byte {}, where the tag says it ends ({})",
                                exported.firstSyncAt,
                                exported.id3v2Bytes));
                        report.check(!exported.id3v2WrittenTwice,
                            "and carries it once - LAME was told not to write the tag this exporter writes itself");
                        report.check(exported.id3v1Footers == 1, std::format("one ID3v1 footer, not two ({} found)", exported.id3v1Footers));

                        report.check(exported.problem.empty(),
                            std::format("the exported MP3's frames walk from the first sync to the end of the audio{}",
                                exported.problem.empty() ? "" : std::format(" - {}", exported.problem)));
                        report.check(exported.firstFrameCarriesVbrTag, "its first frame is LAME's tag frame");
                        report.check(exported.declaredBytes == exported.presentBytes,
                            std::format("and that frame is the finished one, not the placeholder - it declares {} bytes and {} are there",
                                exported.declaredBytes,
                                exported.presentBytes));
                        report.check(exported.parsedFrames == exported.declaredFrames + 1,
                            std::format("its frame count agrees too - {} audio frames plus the tag frame, {} walked",
                                exported.declaredFrames,
                                exported.parsedFrames));
                    }

                    const auto mp3SizeBefore = std::filesystem::file_size(mp3Settings.outputPath, ec);

                    // Cancelling through the MP3 loop, which is its own branch against its own writer.
                    // Before the track is removed below, while the mix still renders: a cancel is only
                    // meaningful for an export that would otherwise have succeeded, and an export that
                    // is refused at source preparation never reaches the loop being tested.
                    {
                        int mp3RenderReports{0};
                        const auto mp3Cancelled = toneExporter.exportMixToFile(toneMixInfo.mixId,
                            mp3Settings,
                            [&mp3RenderReports](float, const std::string &message)
                            {
                                if (!message.starts_with("Exporting MP3..."))
                                {
                                    return true;
                                }
                                ++mp3RenderReports;
                                return false;
                            });

                        report.check(!mp3Cancelled.success, "a cancelled MP3 export reports failure");
                        report.check(mp3RenderReports == 1,
                            std::format("and stops at the first refusal ({} render reports)", mp3RenderReports));
                        report.check(std::filesystem::exists(mp3Settings.outputPath, ec) &&
                                std::filesystem::file_size(mp3Settings.outputPath, ec) == mp3SizeBefore,
                            std::format("the MP3 that was already there is untouched ({} bytes)", mp3SizeBefore));
                        report.check(!std::filesystem::exists(mp3Partial, ec), "and the cancelled MP3 render left no partial behind");
                    }

                    // --- A refused write, through the MP3 path ---
                    //
                    // The same three things the WAV check proves, against code that shares none of it:
                    // LAME rather than a juce::AudioFormatWriter, this exporter's own output stream,
                    // and its own releaseOutput override. Five writes here lead to a `return fail(...)`
                    // and until now not one of those branches had ever run - nothing reachable through
                    // exportMixToFile can make a write refuse, because every injectable failure is
                    // caught at setup before a byte of audio is written.
                    //
                    // 64 KB in, deliberately. The ID3v2 tag written at setup is larger than 12 KB here,
                    // so a stream that refused from the first byte would fail the setup step instead
                    // and test a path that is already covered. This one refuses during the render.
                    constexpr int64_t kRefuseAfterBytes{64 * 1024};
                    {
                        WriteRefusalLog mp3Log;
                        std::vector<std::string> mp3Errors;
                        {
                            // Its own settings object, held for the whole run: ExportMixImplementation
                            // keeps a reference to it.
                            audio::ActiveExportSettings refusingMp3Settings{mp3Settings};

                            WriteRefusingMp3Export refusing{toneMixInfo.mixId,
                                refusingMp3Settings,
                                RefusalTrigger::AfterBytes,
                                kRefuseAfterBytes,
                                mp3Log,
                                [&mp3Errors](float, const std::string &message)
                                {
                                    if (message.starts_with("Error: "))
                                    {
                                        mp3Errors.push_back(message);
                                    }
                                    return true;
                                }};
                            const auto refused = refusing.run();
                            report.check(
                                !refused.success, std::format("an MP3 export whose writes start failing reports failure (said: '{}')", refused.message));

                            report.check(refused.message.find("Run Mixing Loop") != std::string::npos,
                                std::format("and fails in the mixing loop rather than later (said: '{}')", refused.message));
                        }

                        // Which of the five writes refused, not merely that the step failed. This is
                        // the part the step name cannot carry: all five of this exporter's write checks
                        // live inside Run Mixing Loop, so deleting the per-block one only moves the
                        // failure to the flush write a moment later and the step name does not change.
                        // Only the message fail() reported tells them apart.
                        report.check(!mp3Errors.empty(), "the failing export reported an error through its progress callback");
                        if (!mp3Errors.empty())
                        {
                            report.check(mp3Errors.front().find("Failed to write encoded MP3 data") != std::string::npos,
                                std::format("and the first thing that failed is the per-block write, not a later one (said: '{}')", mp3Errors.front()));
                        }

                        // Exactly one, for the same reason: the loop has to stop at the first refused
                        // write. With that check removed it keeps encoding and every subsequent write
                        // is refused too, which is a far larger number.
                        report.check(
                            mp3Log.refusals == 1, std::format("and it stopped at the first refusal rather than encoding on ({} refusals)", mp3Log.refusals));

                        // Without this the checks below would pass on an export that failed for some
                        // other reason, or never reached the encoder at all.
                        report.check(mp3Log.bytesAccepted >= kRefuseAfterBytes,
                            std::format("and did so during the render, not at the ID3v2 tag ({} bytes accepted)", mp3Log.bytesAccepted));

                        report.check(
                            std::filesystem::exists(mp3Settings.outputPath, ec) && std::filesystem::file_size(mp3Settings.outputPath, ec) == mp3SizeBefore,
                            std::format("the MP3 that was already there is untouched ({} bytes)", mp3SizeBefore));
                        report.check(!std::filesystem::exists(mp3Partial, ec), "and the truncated partial was discarded rather than committed");
                    }

                    // --- The same, refused at the LAME info frame instead ---
                    //
                    // The other end of the render. A whole export is encoded and flushed successfully,
                    // and the write that refuses is the one putting the finished info frame back over
                    // LAME's placeholder - the write whose *success* path this suite already checks in
                    // detail, and whose failure path had never run.
                    //
                    // Worth its own block rather than folded into the one above, because it is a
                    // different branch reached through different code: everything from the encoder's
                    // last flush onwards has already succeeded when it fires.
                    {
                        WriteRefusalLog frameLog;
                        std::vector<std::string> frameErrors;
                        {
                            audio::ActiveExportSettings frameSettings{mp3Settings};

                            WriteRefusingMp3Export refusing{toneMixInfo.mixId,
                                frameSettings,
                                RefusalTrigger::AfterSeek,
                                0,
                                frameLog,
                                [&frameErrors](float, const std::string &message)
                                {
                                    if (message.starts_with("Error: "))
                                    {
                                        frameErrors.push_back(message);
                                    }
                                    return true;
                                }};
                            const auto refused = refusing.run();
                            report.check(
                                !refused.success, std::format("an MP3 export whose info-frame write is refused reports failure (said: '{}')", refused.message));
                        }

                        // That the render got all the way to finalisation, which is what makes this a
                        // different check from the one above rather than the same one later.
                        report.check(
                            frameLog.seeks > 0, std::format("the export encoded and flushed a whole file before it seeked back ({} seeks)", frameLog.seeks));
                        report.check(frameLog.bytesAccepted > kRefuseAfterBytes,
                            std::format("and wrote more than the block-refusal check ever got to ({} bytes accepted)", frameLog.bytesAccepted));

                        // Exactly one, for the reason the block above needs the same assertion: if the
                        // info-frame failure were logged but the encoder carried on, the ID3v1 write
                        // would refuse as well and fail the export by itself, satisfying every other
                        // check here. Two refusals would mean this block was pinning that write instead.
                        report.check(
                            frameLog.refusals == 1, std::format("and stopped there rather than carrying on to the footer ({} refusals)", frameLog.refusals));

                        report.check(!frameErrors.empty(), "the failing export reported an error through its progress callback");
                        if (!frameErrors.empty())
                        {
                            report.check(frameErrors.front().find("could not write the LAME info frame") != std::string::npos,
                                std::format("and names the info-frame write as what failed (said: '{}')", frameErrors.front()));
                        }

                        report.check(
                            std::filesystem::exists(mp3Settings.outputPath, ec) && std::filesystem::file_size(mp3Settings.outputPath, ec) == mp3SizeBefore,
                            std::format("the MP3 that was already there is untouched ({} bytes)", mp3SizeBefore));
                        report.check(
                            !std::filesystem::exists(mp3Partial, ec), "and the fully encoded partial was discarded rather than committed, tag frame and all");
                    }

                    // Break it again and re-export over the MP3 that is now there.
                    {
                        SqliteDatabase saboteur;
                        const bool staged = saboteur.open(pathToString(databasePath)) && saboteur.execute("PRAGMA foreign_keys = OFF;") &&
                                            saboteur.execute(std::format("DELETE FROM Tracks WHERE track_id = {};", middleTrackId).c_str());
                        report.check(staged, "the middle track could be removed again for the MP3 check");
                    }

                    const auto mp3Refused = toneExporter.exportMixToFile(toneMixInfo.mixId, mp3Settings, nullptr);
                    report.check(!mp3Refused.success, "an MP3 export whose track cannot be resolved is refused too");
                    report.check(std::filesystem::exists(mp3Settings.outputPath, ec), "and the MP3 it was written over still exists");
                    report.check(std::filesystem::file_size(mp3Settings.outputPath, ec) == mp3SizeBefore,
                        std::format("at the size it was ({} bytes)", mp3SizeBefore));
                    report.check(!std::filesystem::exists(mp3Partial, ec), "with its partial cleaned up");
                }

                // The stage, not the track: fail() logs which track it was and hands it to the progress
                // callback, but the message that comes back to the caller names the operation. Asserted
                // as it is rather than as it might ideally be, so this says what a caller can rely on.
                report.check(gappedExport.message.find("Preparing active track sources") != std::string::npos,
                    std::format("and the refusal names the stage it failed at: '{}'", gappedExport.message));
            }

            // --- 5. A track query that fails is not a mix with no tracks ---
            //
            // The loader used to read its TrackInfos with a call that reports a failed query and an
            // empty result the same way, and published the result either way.
            //
            // The rows themselves were never at risk - they come from readMixTracks, which is checked,
            // and that is what a save writes. What was at risk is the picture: the editor draws what
            // this read returned, so a failed read makes the mix look shorter than it is, and the
            // edits the user makes against that picture are then saved against the real rows. The
            // read-only guard did not catch it either: it asks isCacheLoaded(), which the loader was
            // setting true.
            //
            // Tracks is renamed out of the way rather than dropped, so this is reversible - and with
            // legacy_alter_table on, so the rename does not rewrite the triggers and views that name
            // it. Nothing writes while it is hidden. Last in this suite, and restored before the end,
            // because the suite after this one uses the same library.
            {
                const auto hideTracks = [&report](const std::filesystem::path &dbPath, bool hide)
                {
                    SqliteDatabase saboteur;
                    const bool done = saboteur.open(pathToString(dbPath)) && saboteur.execute("PRAGMA legacy_alter_table=ON;") &&
                        saboteur.execute(hide ? "ALTER TABLE Tracks RENAME TO Tracks_hidden;" : "ALTER TABLE Tracks_hidden RENAME TO Tracks;");
                    report.check(done, hide ? "the Tracks table could be hidden" : "and put back afterwards");
                    return done;
                };

                // It loads now, so that the refusal below is the hiding and not something already wrong.
                audio::MixProjectLoader before;
                report.check(before.loadMix(mixId), "the mix loads while its tracks can be read");

                if (hideTracks(databasePath, true))
                {
                    audio::MixProjectLoader broken;
                    const auto loaded = broken.loadMix(mixId);
                    report.check(!loaded, "a mix whose track query fails does not load");

                    // The same statement from the side the editor asks from: isLoaded() is what
                    // MixNode::isCacheLoaded() returns, and that is what opens the editor read-only.
                    report.check(!broken.isLoaded(), "and says so through isLoaded(), which is what makes the editor read-only");

                    // Nothing was published from the failed read. The statusless version would have
                    // published the mix's rows in full - they come from readMixTracks and were never
                    // in doubt - alongside the TrackInfos it could not read, which is the mismatch the
                    // editor would then have drawn and been edited against.
                    report.check(broken.getMixTracks().empty(), "and publishes no rows from a read it could not trust");

                    std::ignore = hideTracks(databasePath, false);
                }

                audio::MixProjectLoader after;
                report.check(after.loadMix(mixId), "and the mix loads again once the table is back");
            }

            // --- 6. A read that stops partway is a failure, not a shorter answer ---
            //
            // The check above hides Tracks, so the statement never prepares and the hasError() branch
            // that catches a partial read is not reached - deleting that branch would leave every
            // check so far passing. This one makes the read return rows and *then* fail, which is the
            // case the branch exists for and the one that cannot be seen from the row count alone.
            //
            // Over its own scratch database and against the database directly, because the prefix is
            // what has to be inspected: the loader deliberately publishes nothing on failure, so the
            // rows that were read before it failed are only visible here.
            {
                const auto partialDbPath = selfTestRoot / "timeline-partial" / "jucyaudio.db";
                std::error_code partialEc;
                std::filesystem::remove_all(partialDbPath.parent_path(), partialEc);
                std::filesystem::create_directories(partialDbPath.parent_path(), partialEc);
                if (partialEc)
                {
                    return stop(std::format("Could not create {}: {}", pathToString(partialDbPath.parent_path()), partialEc.message()));
                }

                SqliteTrackDatabase scratch;
                const auto connected = scratch.connect(partialDbPath);
                report.check(connected.isOk(), std::format("a scratch database for the partial read could be created (said: '{}')", connected.errorMessage));
                if (!connected.isOk())
                {
                    writeResultsFile(resultsPath, "jucyaudio timeline self test", report);
                    return 1;
                }

                {
                    SqliteDatabase seed;
                    const bool seeded = seed.open(pathToString(partialDbPath)) &&
                        seed.execute("INSERT INTO Folders (folder_id, parent_id, name, root_path, actual_path) VALUES "
                                     "(60, NULL, 'partial', 'c:\\partial', 'C:\\Partial');") &&
                        seed.execute("INSERT INTO Tracks (track_id, folder_id, filename, title) VALUES "
                                     "(600, 60, 'a.mp3', 'A'), (601, 60, 'b.mp3', 'B'), (602, 60, 'c.mp3', 'C');");
                    report.check(seeded, "the scratch database could be seeded with three tracks");
                }

                // The offline filter names temp.OfflineFolders, a temp table that only exists on a
                // connection someone has called rebuildOfflineFoldersTable on - which nothing has, for
                // this scratch database. Left in, the query fails before it reads anything, and a
                // check for a partial read would be watching the wrong failure. Restored below.
                const auto offlineFilterWasOff = config::theSettings.uiSettings.showOfflineTracks.get();
                config::theSettings.uiSettings.showOfflineTracks.set(true);

                // What a working read returns, so the partial one below can be compared against it.
                TrackQueryArgs allTracks{};
                allTracks.usePaging = false;
                // The read on its own line, not inside the check: the arguments to report.check are
                // evaluated in an unspecified order, so formatting the message from `whole` in the same
                // expression that fills it printed the size it had beforehand. The check passed and
                // said "0 of 3".
                std::vector<TrackInfo> whole;
                const auto wholeRead = scratch.getTracks(allTracks, whole);
                report.check(wholeRead.isOk() && whole.size() == 3, std::format("the scratch tracks read back cleanly ({} of 3)", whole.size()));

                // Tracks becomes a view that yields its rows and then raises. abs() of the most
                // negative integer is a runtime error in SQLite, not a parse error, so the failure
                // lands in the middle of the read rather than when the statement is prepared - which
                // is the whole point, and the same technique the folder cache suite uses.
                {
                    SqliteDatabase saboteur;
                    // The first branch deliberately withholds the last track, so what comes out before
                    // the failure is a genuine prefix - fewer rows than the clean read gave - and not
                    // the whole answer with an error stapled to the end. A check that could not tell
                    // those apart would pass on a read that had finished and then tripped.
                    const bool broken = saboteur.open(pathToString(partialDbPath)) &&
                        saboteur.execute("PRAGMA legacy_alter_table=ON;") &&
                        saboteur.execute("ALTER TABLE Tracks RENAME TO Tracks_real;") &&
                        saboteur.execute("CREATE VIEW Tracks AS SELECT * FROM Tracks_real WHERE track_id < 602 "
                                         "UNION ALL SELECT * FROM Tracks_real WHERE track_id = abs(-9223372036854775807 - 1);");
                    report.check(broken, "Tracks was replaced by one that answers with part of itself and then fails");
                }

                std::vector<TrackInfo> partial;
                const auto partialRead = scratch.getTracks(allTracks, partial);
                const auto partialCount = partial.size();
                report.check(!partialRead.isOk(), "a read that stops partway reports failure rather than a shorter answer");
                report.check(partialCount > 0, std::format("and it really did read rows before failing ({})", partialCount));
                report.check(partialCount < whole.size(),
                    std::format("and stopped before the end - a prefix of the answer, not the whole of it ({} of {})", partialCount, whole.size()));

                // --- And a tag read that fails is a failed read too ---
                //
                // The tags are filled in by a second statement after the rows are in hand. It used to
                // report nothing, so a TrackTags that was missing or stopped early produced tracks
                // carrying some of their tags and a successful result - fewer tags reading as a
                // smaller answer rather than as a failure. Neither check above reaches it: both fail
                // on the main query and return first.
                {
                    SqliteDatabase repair;
                    const bool restored = repair.open(pathToString(partialDbPath)) && repair.execute("PRAGMA legacy_alter_table=ON;") &&
                        repair.execute("DROP VIEW Tracks;") && repair.execute("ALTER TABLE Tracks_real RENAME TO Tracks;") &&
                        repair.execute("ALTER TABLE TrackTags RENAME TO TrackTags_hidden;");
                    report.check(restored, "Tracks was put back and TrackTags hidden instead");
                }

                std::vector<TrackInfo> withoutTags;
                const auto tagRead = scratch.getTracks(allTracks, withoutTags);
                report.check(!tagRead.isOk(), "a read whose tag query fails reports failure rather than tracks with fewer tags");
                report.check(withoutTags.size() == whole.size(),
                    std::format("even though the rows themselves came back ({} of {})", withoutTags.size(), whole.size()));

                config::theSettings.uiSettings.showOfflineTracks.set(offlineFilterWasOff);
            }

            // The timeline is a local and takes its components with it; this drops the loader pointer
            // first so nothing outlives the loader either.
            timeline.releaseMixLoader();

            // --- What the export dialog takes from the mix name ---
            //
            // The dialog can rename the mix, and three of its fields are functions of that name: the
            // track title is the name, the track number is the number in front of it, and the output
            // file is the name plus an extension. Each follows a rename until the user types into it,
            // and then stops.
            {
                using jucyaudio::ui::ExportMixDialog;
                using Access = jucyaudio::ui::ExportMixDialogTestAccess;
                using Singleton = jucyaudio::ui::SingletonComponentDialogTestAccess;

                const auto numberOf = [](const char *name)
                {
                    return Access::leadingTrackNumber(juce::String{name}).toStdString();
                };

                report.check(numberOf("4025 - Automix 2025-10-26") == "4025", "a mix name that starts with a number exports as that track number");
                report.check(numberOf("7 - Seven") == "7", "one digit is enough");

                // Each of these has no track number, for a different reason.
                report.check(numberOf("Automix 2025-10-26").empty(), "a name that starts with a word has none");
                report.check(numberOf("4025-Automix").empty(), "and neither has one whose number is not a word of its own");
                report.check(numberOf("2025-10-26 Automix").empty(), "a date is not a track number, because it is not all digits");
                report.check(numberOf("4025").empty(), "nor is a name that is only a number - there is nothing after it to be the title");
                report.check(numberOf("").empty(), "an empty name gives an empty number rather than anything surprising");
                report.check(numberOf(" 4025 Automix").empty(), "and a leading space means the first word is empty, not a number");

                // One effective name, used for the comparison, the rename and every derivation.
                report.check(Access::effectiveMixName(" 7 - Set ").toStdString() == "7 - Set",
                    "the name the dialog means is the trimmed one, so padding cannot mean two things at once");

                // --- and now the dialog itself ---
                //
                // Constructed and driven directly. The message loop is pumped after construction on
                // purpose: TextEditor posts its change notifications rather than calling them, and the
                // first version of this feature marked both derived fields as user-edited when those
                // posted notifications finally arrived, so nothing ever followed a rename. Without the
                // pump that bug is invisible here and the check is worthless.
                {
                    // Its own mix, with no tracks, so the rename below has something real to write to
                    // and nothing else in this suite depends on what happens to it.
                    MixInfo dialogMix{};
                    dialogMix.name = "4025 - Automix";
                    std::vector<MixTrack> noTracks;
                    report.check(theTrackLibrary.getMixManager().createOrUpdateMix(dialogMix, noTracks) && dialogMix.mixId > 0,
                        "a mix could be created for the export dialog checks");

                    ExportMixDialog dialog{dialogMix, nullptr};
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                    report.check(Access::title(dialog).getText().toStdString() == "4025 - Automix", "the dialog opens with the mix name as the track title");
                    report.check(Access::trackNumber(dialog).getText().toStdString() == "4025", "and the number in front of it as the track number");

                    const auto fileBefore = Access::outputFile(dialog);

                    // Rename it. Nothing has been typed into the derived fields, so all three follow.
                    Access::type(dialog, Access::mixName(dialog), "4026 - Later Automix");
                    report.check(Access::title(dialog).getText().toStdString() == "4026 - Later Automix", "renaming the mix carries the track title with it");
                    report.check(Access::trackNumber(dialog).getText().toStdString() == "4026", "and the track number");
                    report.check(Access::outputFile(dialog).getFileNameWithoutExtension().toStdString() == "4026 - Later Automix",
                        std::format("and the output file ({})", Access::outputFile(dialog).getFileName().toStdString()));
                    report.check(
                        Access::outputFile(dialog).getParentDirectory() == fileBefore.getParentDirectory(), "without moving it out of the folder it was in");

                    // Type into the title, and it stops following.
                    Access::type(dialog, Access::title(dialog), "A Title Of My Own");
                    Access::type(dialog, Access::mixName(dialog), "4027 - Later Still");
                    report.check(Access::title(dialog).getText().toStdString() == "A Title Of My Own",
                        "a track title the user typed is not overwritten by a later rename");
                    report.check(Access::trackNumber(dialog).getText().toStdString() == "4027", "while the fields they left alone still follow");

                    // The output file, the other way it stops following: chosen through the component
                    // rather than typed, so it is filenameComponentChanged that has to notice.
                    {
                        const auto chosen = Access::outputFile(dialog).getParentDirectory().getChildFile("Chosen By Hand.mp3");
                        Access::chooseOutputFile(dialog, chosen);
                        Access::type(dialog, Access::mixName(dialog), "4028 - Renamed Again");
                        report.check(Access::outputFile(dialog).getFileNameWithoutExtension().toStdString() == "Chosen By Hand",
                            std::format("an output file the user picked is not renamed out from under them ({})",
                                Access::outputFile(dialog).getFileName().toStdString()));
                        report.check(Access::trackNumber(dialog).getText().toStdString() == "4028", "while the track number, still untouched, follows");
                    }

                    // A name that is a path is a name, not a path.
                    const auto parentBefore = Access::outputFile(dialog).getParentDirectory();
                    Access::type(dialog, Access::mixName(dialog), "../escaped");
                    report.check(Access::outputFile(dialog).getParentDirectory() == parentBefore,
                        std::format("a mix name that looks like a path does not move the export out of its folder ({})",
                            pathToString(std::filesystem::path{Access::outputFile(dialog).getFullPathName().toStdString()})));

                    // And the commit. Back to a plain name first.
                    Access::type(dialog, Access::mixName(dialog), "  Renamed On Export  ");
                    report.check(Access::commit(dialog), "committing a changed name reports success");
                    report.check(
                        Access::mixInfo(dialog).name == "Renamed On Export", "the dialog now holds the trimmed name, which is what the caller is handed");

                    const auto renamed = theTrackLibrary.getMixManager().getMix(dialogMix.mixId);
                    report.check(renamed.name == "Renamed On Export", std::format("and the mix in the database has it too (says '{}')", renamed.name));

                    // Committing again with nothing changed is a no-op rather than a second rename.
                    report.check(Access::commit(dialog), "committing an unchanged name succeeds without doing anything");
                }

                // --- a plugin that throws is suspended without logging from the audio thread ---
                //
                // PluginChain::processBlock is reached from PlaybackController::getNextAudioBlock, so
                // everything in it runs on the audio callback. Both of its exception handlers used to
                // call spdlog::error with plugin->getName().toStdString() - a juce::String allocation,
                // spdlog formatting, a sink mutex, and an fflush, because the flush threshold is set
                // equal to the log threshold (Utils/LoggingUtils.cpp:18) and error is enabled at every
                // level this app offers. Once per offending plugin, since suspendProcessing follows
                // immediately, but once is a dropout at the moment someone is listening.
                //
                // The fault is now recorded and reported later. What this checks is the split: that
                // processBlock leaves something to report rather than having reported it.
                {
                    audio::PluginChain chain;
                    auto thrower = std::make_shared<jucyaudio::ui::ThrowingPlugin>("Throwing Probe", true);
                    auto unknownThrower = std::make_shared<jucyaudio::ui::ThrowingPlugin>("Silent Probe", false);
                    chain.setChain({thrower, unknownThrower});

                    juce::AudioBuffer<float> block{2, 64};
                    block.clear();
                    chain.processBlock(block);

                    report.check(thrower->blocksProcessed() == 1, "the chain reached the plugin that throws");
                    report.check(thrower->isSuspended(), "which is suspended, so it is not asked again");
                    report.check(unknownThrower->isSuspended(), "and so is the one that threw something not derived from std::exception");

                    // The property. Before this change processBlock logged and left nothing behind.
                    report.check(chain.pendingFaultCount() == 2,
                        std::format("processBlock recorded both faults rather than reporting them ({} pending)", chain.pendingFaultCount()));

                    // A second block asks neither of them again, so nothing further is recorded.
                    chain.processBlock(block);
                    report.check(thrower->blocksProcessed() == 1, "a suspended plugin is not processed again");
                    report.check(chain.pendingFaultCount() == 2, "and nothing further is recorded");

                    // The name travels with the fault, so a plugin removed before the report is
                    // still named correctly - and, more to the point, an unrelated plugin is not
                    // named instead. Looking the name up at report time by address could attribute
                    // the fault to whatever was allocated over the top of the dead one.
                    auto replacement = std::make_shared<jucyaudio::ui::ThrowingPlugin>("Innocent Bystander", true);
                    chain.setChain({replacement});
                    thrower.reset();
                    unknownThrower.reset();

                    report.check(chain.pendingFaultCount() == 2, "the faults survive the chain being replaced");

                    const auto names = jucyaudio::audio::PluginChainTestAccess::pendingFaultNames(chain);
                    report.check(names.size() == 2 && names[0] == "Throwing Probe" && names[1] == "Silent Probe",
                        std::format("and still name the plugins that threw, not the one that took their place ('{}', '{}')",
                            names.size() > 0 ? names[0] : std::string{},
                            names.size() > 1 ? names[1] : std::string{}));

                    chain.reportPendingFaults();
                    report.check(chain.pendingFaultCount() == 0, "reporting them clears them");
                    report.check(!replacement->isSuspended(), "and the plugin that replaced them was never touched");
                }

                // --- a fault that cannot be recorded is counted, not lost ---
                //
                // The guard is tried once and never waited on, by either side: waiting is what this
                // arrangement exists to avoid on the audio thread, and a drain that blocked would put
                // the wait back by making the audio thread wake it. So a record that arrives while
                // the drain holds the guard has nowhere to go. It is counted and reported rather than
                // disappearing, which is the difference between a bounded buffer and a lie.
                {
                    audio::PluginChain chain;
                    std::vector<std::shared_ptr<juce::AudioPluginInstance>> many;
                    for (int i = 0; i < 10; ++i)
                    {
                        many.push_back(std::make_shared<jucyaudio::ui::ThrowingPlugin>(juce::String{"Thrower "} + juce::String{i}, true));
                    }
                    chain.setChain(many);

                    juce::AudioBuffer<float> block{2, 64};
                    block.clear();
                    chain.processBlock(block);

                    // Ten threw, and the buffer holds eight.
                    report.check(chain.pendingFaultCount() == 8,
                        std::format("a burst larger than the buffer records what fits ({} pending)", chain.pendingFaultCount()));

                    for (const auto &plugin : many)
                    {
                        report.check(plugin->isSuspended(), "every plugin that threw is suspended, recorded or not");
                        break; // one is enough to say it; the loop below checks the rest
                    }
                    size_t suspended = 0;
                    for (const auto &plugin : many)
                    {
                        suspended += plugin->isSuspended() ? 1 : 0;
                    }
                    report.check(suspended == many.size(), std::format("all ten of them ({} suspended)", suspended));

                    // The two that did not fit are counted rather than forgotten, which is what makes
                    // the buffer bounded rather than lossy-and-quiet.
                    const auto dropped = jucyaudio::audio::PluginChainTestAccess::droppedFaults(chain);
                    report.check(dropped == 2, std::format("and the two that did not fit are counted ({} dropped)", dropped));

                    chain.reportPendingFaults();
                    report.check(chain.pendingFaultCount() == 0, "and the report drains the buffer");
                    report.check(jucyaudio::audio::PluginChainTestAccess::droppedFaults(chain) == 0, "and the dropped count with it");
                }

                // --- neither side waits for the other, and nothing is lost when they collide ---
                //
                // The block above fills the buffer, which is a different branch: every one of its
                // records takes the guard successfully. What is checked here is what happens when the
                // guard is already held - the audio thread's record and the timer's drain both give up
                // rather than wait, because a wait on either side is what puts the audio thread back
                // in the kernel. Held by hand rather than by a second thread: the branches do not care
                // who holds the flag, only that somebody does, and a real race would not be
                // deterministic.
                {
                    audio::PluginChain chain;
                    auto first = std::make_shared<jucyaudio::ui::ThrowingPlugin>("Guarded Probe", true);
                    chain.setChain({first});

                    juce::AudioBuffer<float> block{2, 64};
                    block.clear();
                    chain.processBlock(block);
                    report.check(chain.pendingFaultCount() == 1, "one fault is recorded while nothing holds the guard");

                    report.check(jucyaudio::audio::PluginChainTestAccess::holdGuard(chain), "the guard is free to take");

                    // Reading the count does not wait either - it answers 0 rather than block.
                    report.check(chain.pendingFaultCount() == 0, "a count taken while the guard is held gives up instead of waiting");

                    // The record side. The plugin is still stopped: a fault that could not be written
                    // down costs a diagnostic, never correctness.
                    auto second = std::make_shared<jucyaudio::ui::ThrowingPlugin>("Collided Probe", true);
                    chain.setChain({second});
                    chain.processBlock(block);
                    report.check(second->blocksProcessed() == 1, "a plugin still throws while the guard is held");
                    report.check(second->isSuspended(), "and is suspended even though its fault could not be recorded");

                    const auto droppedWhileHeld = jucyaudio::audio::PluginChainTestAccess::droppedFaults(chain);
                    report.check(droppedWhileHeld == 1, std::format("the record it could not make is counted ({} dropped)", droppedWhileHeld));

                    auto held = jucyaudio::audio::PluginChainTestAccess::pendingFaultNames(chain);
                    report.check(held.size() == 1 && held[0] == "Guarded Probe",
                        std::format("and the fault already in the buffer is untouched ('{}')", held.empty() ? std::string{} : held[0]));

                    // The drain side. It leaves the buffer alone and comes back on the next tick.
                    chain.reportPendingFaults();
                    held = jucyaudio::audio::PluginChainTestAccess::pendingFaultNames(chain);
                    report.check(held.size() == 1 && held[0] == "Guarded Probe",
                        std::format("a report that finds the guard held drains nothing ({} still pending)", held.size()));
                    report.check(jucyaudio::audio::PluginChainTestAccess::droppedFaults(chain) == 1, "and leaves the dropped count for the next one");

                    // And once it is free, the deferred report is the one that runs.
                    jucyaudio::audio::PluginChainTestAccess::releaseGuard(chain);
                    report.check(chain.pendingFaultCount() == 1, "with the guard free the fault is visible again");
                    chain.reportPendingFaults();
                    report.check(chain.pendingFaultCount() == 0, "and the next report drains it");
                    report.check(jucyaudio::audio::PluginChainTestAccess::droppedFaults(chain) == 0, "along with what was dropped while they collided");
                }

                // --- a dismissed singleton dialog leaves the registry at dismissal ---
                //
                // The regression check issue #55 could not have. Its fix was one line - create()
                // instead of launchAsync(), so the enterModalState already written below it is the
                // only one - and the leak it stopped is invisible from outside: the orphaned callback
                // is unreachable and uncounted, and the jassertfalse is Debug-only while this builds
                // Release.
                //
                // What *is* visible is the timing. Owned, the callback runs modalStateFinished on
                // dismissal and unregisters the dialog there. Orphaned, the entry sits in the map
                // until something asks for that id again. getValidDialogWindow cannot show the
                // difference because it erases a dead entry while answering, so this reads the raw
                // map through SingletonComponentDialogTestAccess.
                //
                // This is the one check in the suite that puts a window on screen: enterModalState
                // calls setVisible(true), and a juce::TopLevelWindow becoming visible goes on the
                // desktop. It is created and dismissed inside this block.
                {
                    const juce::String probeId{"SelfTestModalProbe"};
                    report.check(!Singleton::registered(probeId), "the registry does not already hold the probe id");

                    juce::DialogWindow::LaunchOptions probeOptions;
                    probeOptions.resizable = false;
                    jucyaudio::ui::SingletonComponentDialog::showComponent(probeId, "Self Test Modal Probe", new juce::Component{}, probeOptions, true);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                    report.check(Singleton::registered(probeId), "showing a modal singleton dialog registers it");

                    auto *probeWindow = Singleton::window(probeId);
                    report.check(probeWindow != nullptr, "and the registry holds the window it made");

                    if (probeWindow != nullptr)
                    {
                        report.check(probeWindow->isCurrentlyModal(false), "which is modal, once");

                        // Dismissed the way the window itself would be, rather than through
                        // closeDialog - closeDialog unregisters by hand before deleting, so it erases
                        // the entry whether the callback was owned or not, and would pass either way.
                        probeWindow->exitModalState(0);
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                        report.check(!Singleton::registered(probeId),
                            "and dismissing it unregisters the dialog there and then, which only the owned cleanup callback does");
                    }

                    // Whatever happened above, do not leave an entry behind for the next run.
                    jucyaudio::ui::SingletonComponentDialog::closeDialog(probeId);
                }

                // --- entering modal state twice drops the second callback on the floor ---
                //
                // The premise SingletonComponentDialog's modal path rests on, executed rather than
                // read. It used to call launchOptions.launchAsync(), which is create() followed by
                // enterModalState(true, nullptr, true) (juce_DialogWindow.cpp:125-130), and then call
                // enterModalState again with a heap-allocated cleanup callback. Component::enterModalState
                // guards its body with `if (! isCurrentlyModal (false))`, so the second call reached
                // the else branch - a jassertfalse and nothing else. mcm.attachCallback never ran, so
                // nothing owned that callback and nothing deleted it.
                //
                // A plain juce::Component is used rather than a DialogWindow: enterModalState calls
                // setVisible(true), and a component that is not on the desktop shows nothing. This
                // check describes JUCE's behaviour and needs no window to do it - unlike the lifecycle
                // check above, which drives the real showComponent and does put one on screen.
                {
                    struct Recorder final : public juce::ModalComponentManager::Callback
                    {
                        Recorder(int &finishedCount, int &destroyedCount)
                            : finished{finishedCount},
                              destroyed{destroyedCount}
                        {
                        }
                        ~Recorder() override
                        {
                            ++destroyed;
                        }
                        void modalStateFinished(int) override
                        {
                            ++finished;
                        }
                        int &finished;
                        int &destroyed;
                    };

                    int firstFinished = 0;
                    int firstDestroyed = 0;
                    int secondFinished = 0;
                    int secondDestroyed = 0;

                    juce::Component probe;
                    probe.enterModalState(false, new Recorder{firstFinished, firstDestroyed}, false);
                    report.check(probe.isCurrentlyModal(false), "a component that has entered modal state says so");

                    // The second entry. Its callback is kept here because nothing else will own it -
                    // which is the whole point - and is deleted below rather than leaked by the suite.
                    auto *orphan = new Recorder{secondFinished, secondDestroyed};
                    probe.enterModalState(false, orphan, false);

                    probe.exitModalState(0);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                    report.check(firstFinished == 1, std::format("the callback of the first entry is invoked ({} time(s))", firstFinished));
                    report.check(firstDestroyed == 1, std::format("and deleted by the modal manager that took it ({} time(s))", firstDestroyed));
                    report.check(secondFinished == 0, std::format("the second entry's callback is never invoked ({} time(s))", secondFinished));
                    report.check(
                        secondDestroyed == 0, std::format("nor deleted - nothing took ownership of it, which is the leak ({} deletion(s))", secondDestroyed));

                    delete orphan;
                }

                // --- Escape reaches the dialog even while the caret is in a text field ---
                //
                // Reported from use: "Create Working Set" could not be dismissed with Escape until the
                // user tabbed out of the name field. juce::TextEditor::consumeEscAndReturnKeys defaults
                // to true (juce_TextEditor.h:825) and TextEditor::keyPressed returns it for the escape
                // key, so the key never reaches the DialogWindow and escapeKeyTriggersCloseButton -
                // which these dialogs all set - has nothing to act on.
                //
                // Focusing the field on open is the right behaviour and is not what changed. What
                // changed is that Escape now works while the focus is where it should be, through the
                // listener callback JUCE offers for exactly this, the way ExportMixDialog already did.
                {
                    const auto escape = juce::KeyPress{juce::KeyPress::escapeKey};

                    // The metadata dialogs, through their shared base. These were never broken -
                    // MetaDataEditorDialogBase already wires m_nameEditor.onEscapeKey, which is why
                    // Escape worked on them while Create Working Set needed a tab-out first, and how
                    // the idiom used by the three dialogs that were broken was chosen. The check is
                    // here to hold that wiring in place. closeDialog(false) invokes the finished
                    // callback before it touches any window, so it needs no window.
                    {
                        MixInfo escapeMix{};
                        escapeMix.name = "Escape Me";
                        std::vector<MixTrack> noTracks;
                        const bool created = theTrackLibrary.getMixManager().createOrUpdateMix(escapeMix, noTracks) && escapeMix.mixId > 0;
                        report.check(created, "a mix could be created for the Escape checks");

                        if (created)
                        {
                            int calls = 0;
                            bool sawNameChanged = true;
                            jucyaudio::ui::EditMixMetaDataDialog metadata{escapeMix,
                                [&calls, &sawNameChanged](bool nameChanged, std::string_view)
                                {
                                    ++calls;
                                    sawNameChanged = nameChanged;
                                }};
                            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                            auto *editor = firstTextEditor(metadata);
                            report.check(editor != nullptr, "the mix metadata dialog has a text field to press Escape in");

                            if (editor != nullptr)
                            {
                                report.check(editor->keyPressed(escape), "the field consumes Escape, which is why the dialog has to be told");

                                // escapePressed() is postCommandMessage, not a direct call
                                // (juce_TextEditor.cpp:623), so the listener hears about it from the
                                // message loop rather than from keyPressed. Same shape as the
                                // textChanged notification ExportMixDialog documents.
                                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                                report.check(calls == 1, std::format("and the dialog closes anyway ({} call(s))", calls));
                                report.check(!sawNameChanged, "as a cancel rather than a save - Escape is the discard key");
                            }
                        }
                    }

                    // The two dialogs that take a name. Their cancel is exitModalState on a parent
                    // DialogWindow, which this suite does not construct, so what is checked here is
                    // that the handler is wired at all - the line the fix adds, and the line whose
                    // absence was the defect. What that handler then does is the same handleCancel
                    // the Cancel button has always called.
                    {
                        jucyaudio::ui::CreateWorkingSetDialogComponent workingSet{5, [](const juce::String &, WorkingSetId) {}};
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                        auto *editor = firstTextEditor(workingSet);
                        report.check(editor != nullptr, "the create-working-set dialog has a text field");
                        report.check(editor != nullptr && editor->onEscapeKey != nullptr, "and something listening for Escape in it");
                    }

                    // Create Mix, which unlike its sibling can be driven all the way here:
                    // closeThisDialog(false) invokes the callback before it looks for a parent
                    // window, so the cancel is observable without one.
                    {
                        int calls = 0;
                        bool sawSuccess = true;
                        std::vector<TrackInfo> noTracksForMix;
                        jucyaudio::ui::CreateMixDialogComponent createMix{noTracksForMix,
                            -1,
                            [&calls, &sawSuccess](bool success, const database::MixInfo &)
                            {
                                ++calls;
                                sawSuccess = success;
                            }};
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                        auto *editor = firstTextEditor(createMix);
                        report.check(editor != nullptr, "the create-mix dialog has a text field to press Escape in");

                        if (editor != nullptr)
                        {
                            report.check(editor->keyPressed(escape), "its field consumes Escape as well");

                            // Delivered through the message loop - see the note above.
                            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                            report.check(calls == 1, std::format("and the dialog closes anyway ({} call(s))", calls));
                            report.check(!sawSuccess, "as a cancel, so nothing is created");
                        }
                    }

                    // The marker dialog, whose comment field is multi-line - which changes what Return
                    // does in it and nothing about Escape.
                    {
                        int cancels = 0;
                        jucyaudio::ui::MarkerEditDialog marker;
                        marker.onCancel = [&cancels]
                        {
                            ++cancels;
                        };
                        marker.setupForNewMarker(std::chrono::milliseconds{1000});
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                        auto *editor = firstTextEditor(marker);
                        report.check(editor != nullptr, "the marker dialog has a text field to press Escape in");

                        if (editor != nullptr)
                        {
                            report.check(editor->keyPressed(escape), "its field consumes Escape too");

                            // Delivered through the message loop - see the note above.
                            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
                            report.check(cancels == 1, std::format("and the dialog cancels anyway ({} call(s))", cancels));
                        }
                    }
                }

                // --- a mix whose stored name is a path opens inside the Music folder ---
                //
                // The other half of "a name that is a path is a name". The rename check above covers a
                // name typed into the dialog; this covers one the dialog was opened with, which used
                // to go straight to userMusicDirectory.getChildFile. getChildFile *resolves* ".."
                // rather than rejecting it, so such a mix opened already pointing outside Music - and
                // the guard on renaming could not undo it, because what that guarantees is the file
                // staying in the folder it is already in.
                {
                    const auto musicFolder = juce::File::getSpecialLocation(juce::File::userMusicDirectory);

                    for (const auto *hostileName : {"../escaped", "..", "../../deeper/still", "/", "   "})
                    {
                        MixInfo hostileMix{};
                        hostileMix.name = hostileName;
                        std::vector<MixTrack> noTracks;
                        // Reported rather than skipped over. Nothing rejects a name like these today,
                        // so a failure here means the database could not be written - and a silent
                        // continue would drop the checks below while leaving the suite green, which
                        // is the shape of every false clean this project has spent the week removing.
                        // If mix naming ever does start refusing these, this says so out loud and the
                        // case can be dropped deliberately.
                        const bool created = theTrackLibrary.getMixManager().createOrUpdateMix(hostileMix, noTracks) && hostileMix.mixId > 0;
                        report.check(created, std::format("a mix stored as '{}' could be created for the check", hostileName));
                        if (!created)
                        {
                            continue;
                        }

                        ExportMixDialog hostile{hostileMix, nullptr};
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                        const auto opened = Access::outputFile(hostile);
                        report.check(opened.getParentDirectory() == musicFolder,
                            std::format("a mix stored as '{}' opens inside the Music folder ({})",
                                hostileName,
                                pathToString(std::filesystem::path{opened.getFullPathName().toStdString()})));
                        report.check(opened.getFileName().isNotEmpty(), std::format("and on a file with a name ('{}')", opened.getFileName().toStdString()));
                    }
                }

                // --- a name nobody touched is not renamed, however it is spelled ---
                //
                // The name this dialog works in is the trimmed one, so a mix stored with padding
                // differs from its own effective name the moment the two are compared. Comparing them
                // is what the first version did, and exporting such a mix quietly renamed it to
                // something the user never typed.
                {
                    MixInfo paddedMix{};
                    paddedMix.name = "  Padded Mix  ";
                    std::vector<MixTrack> noTracks;
                    const bool created = theTrackLibrary.getMixManager().createOrUpdateMix(paddedMix, noTracks) && paddedMix.mixId > 0;
                    report.check(created, "a mix whose stored name has padding could be created");

                    if (created)
                    {
                        ExportMixDialog padded{paddedMix, nullptr};
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                        report.check(Access::commit(padded), "committing without touching the name succeeds");
                        report.check(theTrackLibrary.getMixManager().getMix(paddedMix.mixId).name == "  Padded Mix  ",
                            std::format(
                                "and leaves the stored name exactly as it was (says '{}')", theTrackLibrary.getMixManager().getMix(paddedMix.mixId).name));

                        // Typed in and put back. "Has the user been in this field" is the wrong
                        // question - what matters is whether the name in it now differs from the one
                        // stored, and after an edit and an undo it does not.
                        Access::type(padded, Access::mixName(padded), "Something Else");
                        Access::type(padded, Access::mixName(padded), "  Padded Mix  ");
                        report.check(Access::commit(padded), "committing after an edit that was undone succeeds");
                        report.check(theTrackLibrary.getMixManager().getMix(paddedMix.mixId).name == "  Padded Mix  ",
                            std::format("and still leaves the stored name alone (says '{}')", theTrackLibrary.getMixManager().getMix(paddedMix.mixId).name));

                        // Editing the padding, on the other hand, is editing. The field no longer
                        // holds what was stored, so the effective name is written - which normalises
                        // the stored name rather than preserving padding nobody can see. Pinned
                        // because the code decides it either way and the decision should be visible:
                        // untouched is preserved, touched is normalised.
                        Access::type(padded, Access::mixName(padded), "   Padded Mix   ");
                        report.check(Access::commit(padded), "committing a name whose padding was edited succeeds");
                        report.check(theTrackLibrary.getMixManager().getMix(paddedMix.mixId).name == "Padded Mix",
                            std::format("and stores the effective name, because the field was edited (says '{}')",
                                theTrackLibrary.getMixManager().getMix(paddedMix.mixId).name));
                    }
                }

                // --- a rename that cannot happen stops the export ---
                //
                // Mixes.name is UNIQUE COLLATE NOCASE, so a second mix wanting the first one's name is
                // a refusal the database produces on its own - no table has to be broken for it.
                {
                    MixInfo takenName{};
                    takenName.name = "Name Already Taken";
                    MixInfo wantsIt{};
                    wantsIt.name = "Wants That Name";
                    std::vector<MixTrack> noTracks;
                    const bool created = theTrackLibrary.getMixManager().createOrUpdateMix(takenName, noTracks) &&
                                         theTrackLibrary.getMixManager().createOrUpdateMix(wantsIt, noTracks) && takenName.mixId > 0 && wantsIt.mixId > 0;
                    report.check(created, "two mixes could be created, one holding the name the other will ask for");

                    if (created)
                    {
                        ExportMixDialog clashing{wantsIt, nullptr};
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                        Access::type(clashing, Access::mixName(clashing), "Name Already Taken");
                        std::string refusal;
                        report.check(!Access::commit(clashing, refusal), "a rename onto a name another mix already has is refused");
                        report.check(!refusal.empty(), std::format("and says why, for the caller to show ('{}')", refusal));
                        report.check(theTrackLibrary.getMixManager().getMix(wantsIt.mixId).name == "Wants That Name", "and the mix keeps the name it had");
                        report.check(Access::mixInfo(clashing).name == "Wants That Name", "so the dialog still reports the old name to its caller");
                    }
                }

                // --- Export, Schedule and Cancel, through the buttons rather than around them ---
                //
                // handleExport is where the rename is committed, so a check that calls
                // commitMixNameIfChanged directly proves it works and not that anything calls it. These
                // go through the same code the buttons run, and watch what the callback is handed.
                {
                    MixInfo buttonMix{};
                    buttonMix.name = "Button Mix";
                    std::vector<MixTrack> noTracks;
                    const bool created = theTrackLibrary.getMixManager().createOrUpdateMix(buttonMix, noTracks) && buttonMix.mixId > 0;
                    report.check(created, "a mix could be created for the button checks");

                    const auto exportTarget = juce::File{juce::String{pathToString(selfTestRoot / "dialog-export.mp3")}};

                    if (created)
                    {
                        // Cancel first: it must not rename, whatever has been typed.
                        {
                            int calls = 0;
                            ExportMixDialog::Result seen{ExportMixDialog::Result::ExportNow};
                            std::string seenName;
                            ExportMixDialog dialog{buttonMix,
                                [&](ExportMixDialog::Result result, const MixInfo &info, const audio::ActiveExportSettings &)
                                {
                                    ++calls;
                                    seen = result;
                                    seenName = info.name;
                                }};
                            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                            Access::type(dialog, Access::mixName(dialog), "Cancelled Rename");
                            Access::pressCancel(dialog);

                            report.check(
                                calls == 1 && seen == ExportMixDialog::Result::Cancelled, std::format("Cancel reports itself once ({} call(s))", calls));
                            report.check(seenName == "Button Mix", "and hands the caller the name the mix still has");
                            report.check(theTrackLibrary.getMixManager().getMix(buttonMix.mixId).name == "Button Mix",
                                "Cancel renames nothing, however much was typed into the name");
                        }

                        // What the dialog actually handed over, so the reopen check compares against
                        // what was submitted rather than what it assumes was.
                        audio::ActiveExportSettings submitted{};
                        bool scheduledInDatabase = false;

                        // Schedule: renames, because the name belongs to the mix and not to the file.
                        //
                        // handleExport refuses without an export folder, and a scratch library has
                        // none, so one is made here rather than the check being skipped.
                        report.check(theTrackLibrary.getMixManager().createExportFolder("SelfTest Exports", "for the export dialog checks"),
                            "an export folder could be created");
                        {
                            int calls = 0;
                            ExportMixDialog::Result seen{ExportMixDialog::Result::Cancelled};
                            std::string seenName;
                            ExportMixDialog dialog{buttonMix,
                                [&](ExportMixDialog::Result result, const MixInfo &info, const audio::ActiveExportSettings &settings)
                                {
                                    ++calls;
                                    seen = result;
                                    seenName = info.name;

                                    // What MainComponent.cpp:2908 does with a ScheduleForLater, and the
                                    // only thing anywhere that writes pending_export_settings: the
                                    // dialog hands the settings over, it does not persist them. The
                                    // reopen check below reads back what this writes.
                                    if (result == ExportMixDialog::Result::ScheduleForLater)
                                    {
                                        submitted = settings;
                                        scheduledInDatabase = theTrackLibrary.getMixManager().scheduleMixForExport(info.mixId, settings);
                                    }
                                }};
                            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                            const bool folderSelected = Access::selectAnExportFolder(dialog);
                            report.check(folderSelected, "an export folder could be selected");
                            if (folderSelected)
                            {
                                Access::setOutputFile(dialog, exportTarget);
                                Access::setSchedule(dialog, true);
                                Access::type(dialog, Access::mixName(dialog), "Scheduled Rename");
                                Access::pressExport(dialog);

                                report.check(calls == 1 && seen == ExportMixDialog::Result::ScheduleForLater,
                                    std::format("scheduling reports itself once ({} call(s))", calls));
                                report.check(seenName == "Scheduled Rename", "and hands the caller the new name");
                                report.check(theTrackLibrary.getMixManager().getMix(buttonMix.mixId).name == "Scheduled Rename",
                                    "a scheduled export renames the mix now rather than when it runs");
                            }
                        }

                        // Reopening a mix that has a pending export restores what was chosen last time,
                        // and a rename must leave every bit of it alone. The scheduled export above
                        // wrote exactly such a row, so this reads one back rather than staging one.
                        {
                            report.check(scheduledInDatabase, "scheduling wrote the pending settings, the way the app writes them");

                            const auto pending = theTrackLibrary.getMixManager().getPendingExportSettings(buttonMix.mixId);
                            report.check(pending.has_value(), "and they read back");

                            const auto scheduled = theTrackLibrary.getMixManager().getMix(buttonMix.mixId);
                            const juce::File submittedFile{juce::String{submitted.outputPath.string()}};

                            ExportMixDialog reopened{scheduled, nullptr};
                            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                            report.check(Access::outputFile(reopened) == submittedFile,
                                std::format("reopening restores the output file the scheduled export was given ({})",
                                    Access::outputFile(reopened).getFileName().toStdString()));

                            const auto restoredTitle = Access::title(reopened).getText().toStdString();
                            const auto restoredNumber = Access::trackNumber(reopened).getText().toStdString();
                            report.check(restoredTitle == submitted.title, std::format("and the track title it was given (says '{}')", restoredTitle));

                            // Why the restore marks all three fields edited: everything here was chosen
                            // deliberately last time, so a rename now must not quietly discard it.
                            Access::type(reopened, Access::mixName(reopened), "4099 - Renamed After Scheduling");
                            report.check(Access::outputFile(reopened) == submittedFile,
                                std::format("and renaming does not move the pending export's output file ({})",
                                    Access::outputFile(reopened).getFileName().toStdString()));
                            report.check(Access::title(reopened).getText().toStdString() == restoredTitle,
                                std::format("nor rewrite the track title it restored (says '{}')", Access::title(reopened).getText().toStdString()));
                            report.check(Access::trackNumber(reopened).getText().toStdString() == restoredNumber,
                                std::format("nor the track number (says '{}')", Access::trackNumber(reopened).getText().toStdString()));
                        }
                    }
                }
            }

            writeResultsFile(resultsPath, "jucyaudio timeline self test", report);
            spdlog::info("[SelfTest] Timeline test finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runFolderCacheSelfTest(const std::filesystem::path &selfTestRoot)
        {
            Report report;
            const auto resultsPath = selfTestRoot / "foldercache-results.txt";
            const auto workRoot = selfTestRoot / "foldercache-library";

            spdlog::info("[SelfTest] Starting folder cache self test. Root: {}", pathToString(selfTestRoot));

            auto &folders = theTrackLibrary.getTrackDatabase().getFolderDatabase();

            // Rows, not directories: findOrCreateFolderByPath writes the Folders table and never looks
            // at the disk. Enough of them that a cache build takes long enough to overlap with the
            // other thread - the window this test exists to enter is open only while one is running.
            constexpr int kSeedFolders = 400;
            for (int i = 0; i < kSeedFolders; ++i)
            {
                const auto path = workRoot / std::format("artist{:02}", i % 20) / std::format("album{:03}", i);
                if (folders.findOrCreateFolderByPath(path) <= 0)
                {
                    report.abort(std::format("Could not create the seed folder {}", pathToString(path)));
                    writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                    return 1;
                }
            }
            const auto rootFolderId = folders.findOrCreateFolderByPath(workRoot);
            if (rootFolderId <= 0)
            {
                report.abort("Could not create the folder the reader watches.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }
            report.note(std::format("seeded {} folders under {} (folder id {})", kSeedFolders, pathToString(workRoot), rootFolderId));

            // The two paths that used to take the two mutexes in opposite orders. A reader forces cache
            // builds by invalidating between reads - buildCacheIfNeeded then holds the cache mutex and
            // reaches for the database mutex through its statements - while a writer runs
            // findOrCreateFolderByPath, which holds the database mutex and then wants the cache mutex.
            // Run against the inverted order these two stop each other dead; the test then fails by
            // deadline rather than by assertion, which is why there is one.
            constexpr int kIterations = 300;
            std::atomic<int> readsDone{0};
            std::atomic<int> writesDone{0};
            std::atomic<bool> readerFinished{false};
            std::atomic<bool> writerFinished{false};
            std::atomic<bool> invalidatorFinished{false};
            std::atomic<bool> stopInvalidating{false};
            std::atomic<int> readsThatLostTheRoot{0};
            std::atomic<int> creationsThatFailed{0};
            std::atomic<int> pathsThatChangedId{0};

            std::thread reader{[&]()
                {
                    for (int i = 0; i < kIterations; ++i)
                    {
                        folders.invalidateCache();
                        const auto seen = folders.getAllChildFolders({rootFolderId});
                        std::ignore = seen;
                        if (!folders.getFolderById(rootFolderId).has_value())
                        {
                            ++readsThatLostTheRoot;
                        }
                        ++readsDone;
                    }
                    readerFinished = true;
                }};

            // A third thread doing nothing but invalidating. The window that produced duplicate
            // folder rows is between the cache build inside findOrCreateFolderByPath and the lookup
            // that follows it, and it is a few microseconds wide: the reader above invalidates once
            // per pass and hit it about half the time. This one spins, which is not realistic use but
            // is what makes the check reliable rather than lucky.
            std::thread invalidator{[&]()
                {
                    while (!stopInvalidating)
                    {
                        folders.invalidateCache();
                    }
                    invalidatorFinished = true;
                }};

            std::thread writer{[&]()
                {
                    for (int i = 0; i < kIterations; ++i)
                    {
                        const auto path = workRoot / std::format("artist{:02}", i % 20) / std::format("live{:03}", i);
                        const auto id = folders.findOrCreateFolderByPath(path);
                        if (id <= 0)
                        {
                            ++creationsThatFailed;
                        }
                        else if (folders.findOrCreateFolderByPath(path) != id)
                        {
                            // The same path twice must be the same row, whatever the reader is doing to
                            // the cache in between.
                            ++pathsThatChangedId;
                        }
                        ++writesDone;
                    }
                    writerFinished = true;
                }};

            // Generous: this is a deadline for a hang, not a performance assertion. The work itself is
            // hundreds of small statements and takes a fraction of it.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
            while ((!readerFinished || !writerFinished) && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }

            const bool finished = readerFinished && writerFinished;
            report.check(finished,
                std::format("the cache reader and the folder writer both finished ({}/{} reads, {}/{} writes)",
                    readsDone.load(),
                    kIterations,
                    writesDone.load(),
                    kIterations));

            // Told to stop either way, though on the failure path below nothing waits for it.
            stopInvalidating = true;

            if (!finished)
            {
                report.abort("Timed out. Two threads holding one mutex each and waiting for the other is what this looks like.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);

                // Out, without unwinding. There is no safe way back from here: a thread stuck on a mutex
                // never returns, so it can be neither joined nor left behind - joining hangs the run,
                // and returning destroys the state these threads hold references to and then tears down
                // the folder database they are standing in. The report is written and the log flushed
                // above, which is everything this run had to say.
                spdlog::error("[SelfTest] Folder cache test timed out; leaving the process without unwinding.");
                spdlog::default_logger()->flush();
                std::_Exit(1);
            }

            reader.join();
            writer.join();
            invalidator.join();
            report.check(invalidatorFinished, "the invalidator came back too");

            report.check(creationsThatFailed == 0,
                std::format("every folder the writer asked for was created ({} failed)", creationsThatFailed.load()));
            report.check(pathsThatChangedId == 0,
                std::format("the same path always came back as the same row ({} did not)", pathsThatChangedId.load()));
            // A count, not a check. An accessor builds the cache, releases both mutexes and then takes
            // the cache mutex to read, so the invalidator can empty it in between and the read misses -
            // the known window tracked as issue #36, and not something this change set out to close.
            // Asserting zero here would fail a correct implementation on an unlucky schedule.
            report.note(std::format("{} of {} reads found the cache emptied under them, which is the known accessor window",
                readsThatLostTheRoot.load(),
                kIterations));

            // The cache has to agree with the database afterwards, or the locking is right and the
            // bookkeeping is not.
            folders.invalidateCache();
            const auto rebuiltCount = folders.getAllChildFolders({rootFolderId}).size();
            report.check(rebuiltCount > static_cast<size_t>(kSeedFolders),
                std::format("a rebuilt cache holds the seeded folders and the ones written during the run (found {})", rebuiltCount));


            // --- What removeEmptyFolders deletes, and what it must not ---
            //
            // A destructive path, and the one whose phases were rearranged to get the lock order right,
            // so it gets exercised rather than reasoned about. It deletes every folder with no tracks
            // under it, which is why it runs at the end: the folders seeded above are all empty.
            const auto usedRoot = selfTestRoot / "foldercache-used";
            const auto usedAlbum = usedRoot / "deep" / "album";
            std::error_code removalEc;
            std::filesystem::create_directories(usedAlbum, removalEc);
            if (removalEc)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(usedAlbum), removalEc.message()));
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }
            if (!writeSilentWav(usedAlbum / "keep.wav", static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
            {
                report.abort("Could not write the fixture the surviving folder is built on.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }

            auto &trackDb = theTrackLibrary.getTrackDatabase();
            if (!trackDb.getLibraryRootManager().addRoot(pathToString(usedRoot)).has_value())
            {
                report.abort("Could not add the used-folder library as a root.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }
            const auto usedRootId = folders.findOrCreateFolderByPath(usedRoot);
            if (usedRootId <= 0 || !runScan({usedRootId}, false, report, "used-folder discovery"))
            {
                report.abort("Could not discover the used-folder library.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }

            // The three that must survive - the folder holding the track and both its ancestors - and
            // one sibling with nothing in it, which must not.
            const auto albumId = folders.findOrCreateFolderByPath(usedAlbum);
            const auto deepId = folders.findOrCreateFolderByPath(usedRoot / "deep");
            const auto emptySiblingId = folders.findOrCreateFolderByPath(usedRoot / "deep" / "nothing-here");
            if (albumId <= 0 || deepId <= 0 || emptySiblingId <= 0)
            {
                report.abort("Could not resolve the folders the removal check is about.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }

            report.check(folders.removeEmptyFolders(), "removeEmptyFolders reports success");

            // By id, not by path: findOrCreateFolderByPath would put a deleted folder straight back.
            report.check(folders.getFolderById(albumId).has_value(),
                std::format("the folder holding a track survived (id {})", albumId));
            report.check(folders.getFolderById(deepId).has_value(),
                std::format("its ancestor survived (id {})", deepId));
            report.check(folders.getFolderById(usedRootId).has_value(),
                std::format("the library root above it survived (id {})", usedRootId));
            report.check(!folders.getFolderById(emptySiblingId).has_value(),
                std::format("the empty sibling is gone (id {})", emptySiblingId));
            report.check(!folders.getFolderById(rootFolderId).has_value(),
                std::format("so are the empty folders this suite seeded (id {})", rootFolderId));

            // --- A path the cache does not know, but the table does ---
            //
            // The schema says one Folders row per path since v31, so the insert findOrCreateFolderByPath
            // falls back on can now be refused - and it is refused exactly when the cache it just
            // consulted is out of date. Reporting that as "could not create the folder" would leave a
            // scan unable to place any track in the folder while the row it needed sat in the table.
            //
            // Staged over a second connection, because nothing reachable through this interface can
            // produce a cache that is valid and missing a row - which is the whole point of the index.
            // Its own database, since it deliberately writes behind a live cache's back.
            {
                const auto stagedDbPath = selfTestRoot / "foldercache-staged" / "jucyaudio.db";
                std::filesystem::remove_all(stagedDbPath.parent_path(), removalEc);
                std::filesystem::create_directories(stagedDbPath.parent_path(), removalEc);
                if (removalEc)
                {
                    report.abort(std::format("Could not create {}: {}", pathToString(stagedDbPath.parent_path()), removalEc.message()));
                    writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                    return 1;
                }

                SqliteTrackDatabase scratch;
                const auto connected = scratch.connect(stagedDbPath);
                report.check(connected.isOk(), std::format("a scratch database for the staged folder could be created (said: '{}')", connected.errorMessage));
                if (!connected.isOk())
                {
                    writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                    return 1;
                }

                auto &scratchFolders = scratch.getFolderDatabase();
                const auto knownPath = stagedDbPath.parent_path() / "library";
                const auto knownId = scratchFolders.findOrCreateFolderByPath(knownPath);
                report.check(knownId > 0, "the parent of the staged folder is in the cache");

                // A child of it, so the recursion in findOrCreateFolderByPath resolves the parent from
                // the cache and only the child itself reaches the insert.
                const auto stagedPath = knownPath / "staged";
                const auto stagedKey = normalizeForCache(pathToString(stagedPath));
                FolderId stagedId{-1};
                {
                    SqliteDatabase direct;
                    if (!direct.open(pathToString(stagedDbPath)))
                    {
                        report.abort("Could not reopen the scratch database to stage a folder behind its cache.");
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }

                    SqliteStatement insert{direct, "INSERT INTO Folders (parent_id, name, root_path, actual_path) VALUES (?, 'staged', ?, ?);"};
                    const bool staged = insert.isValid() && insert.addParam(knownId) && insert.addParam(stagedKey) &&
                        insert.addParam(pathToString(stagedPath)) && insert.execute();
                    report.check(staged, "a folder row could be written behind the live cache's back");
                    if (!staged)
                    {
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }
                    stagedId = direct.getLastInsertRowId();

                    // The index refuses a second row for that path, which is what makes the recovery
                    // below reachable in the first place.
                    SqliteStatement again{direct, "INSERT INTO Folders (parent_id, name, root_path) VALUES (?, 'staged-again', ?);"};
                    report.check(again.isValid() && again.addParam(knownId) && again.addParam(stagedKey) && !again.execute(),
                        "a second row for a path the table already has is refused");
                }

                report.check(scratchFolders.findOrCreateFolderByPath(stagedPath) == stagedId,
                    std::format("a folder the cache never saw comes back as the row that already exists (id {})", stagedId));
                report.check(scratchFolders.findOrCreateFolderByPath(stagedPath) == stagedId, "and the rebuilt cache agrees");
                report.check(scratchFolders.getFolderById(stagedId).has_value(), "the cache holds it by id too");
            }

            // --- A read that fails must delete nothing ---
            //
            // Everything removeEmptyFolders deletes is decided by what is *absent* from one read of
            // Tracks, so a read that fails or stops partway looks exactly like a library nobody uses.
            // Its own database, its own connection: this deliberately breaks the table the read needs,
            // and doing that to the library the other suites share would be a poor trade for a check.
            const auto brokenDbPath = selfTestRoot / "foldercache-broken" / "jucyaudio.db";
            std::filesystem::remove_all(brokenDbPath.parent_path(), removalEc);
            std::filesystem::create_directories(brokenDbPath.parent_path(), removalEc);
            if (removalEc)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(brokenDbPath.parent_path()), removalEc.message()));
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }

            {
                SqliteTrackDatabase scratch;
                const auto connected = scratch.connect(brokenDbPath);
                report.check(connected.isOk(), std::format("a scratch database could be created (said: '{}')", connected.errorMessage));
                if (!connected.isOk())
                {
                    writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                    return 1;
                }

                auto &scratchFolders = scratch.getFolderDatabase();
                std::vector<FolderId> seeded;
                for (int i = 0; i < 5; ++i)
                {
                    const auto id = scratchFolders.findOrCreateFolderByPath(brokenDbPath.parent_path() / std::format("empty{:02}", i));
                    if (id <= 0)
                    {
                        report.abort("Could not seed the scratch database with folders.");
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }
                    seeded.push_back(id);
                }

                // Counted in the database rather than through the cache. With a broken Tracks the cache
                // cannot be rebuilt at all, so a cache-based count would drop to zero whether the rows
                // were deleted or not - it would report a refusal that never happened as a success.
                const auto countFolderRows = [&brokenDbPath, &report]() -> int64_t
                {
                    SqliteDatabase counter;
                    if (!counter.open(pathToString(brokenDbPath)))
                    {
                        report.abort("Could not reopen the scratch database to count its folders.");
                        return -1;
                    }
                    SqliteStatement stmt{counter, "SELECT COUNT(*) FROM Folders"};
                    return (stmt.isValid() && stmt.getNextResult()) ? stmt.getInt64(0) : -1;
                };

                // Built while Tracks still answers, and not invalidated afterwards: removeEmptyFolders
                // takes its list of candidates from the cache as it finds it, so an empty cache would
                // leave it nothing to delete and every check below would pass without proving anything.
                report.check(scratchFolders.getFolderById(seeded.front()).has_value(), "the scratch folder cache is built and holds the seeded folders");

                const auto folderRowsBefore = countFolderRows();
                report.check(folderRowsBefore >= static_cast<int64_t>(seeded.size()),
                    std::format("the scratch database holds the {} empty folders and their parents ({} rows)", seeded.size(), folderRowsBefore));

                // A Tracks that answers one row and then fails, over a second connection so the schema
                // change is committed before the folder database reads it. abs() of the most negative
                // integer is a runtime error in SQLite, not a parse error, so the failure lands in the
                // middle of the read rather than when it is prepared - the partial read this is about.
                {
                    SqliteDatabase saboteur;
                    if (!saboteur.open(pathToString(brokenDbPath)))
                    {
                        report.abort("Could not reopen the scratch database to break its Tracks table.");
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }
                    const bool broken = saboteur.execute("DROP TABLE Tracks;") &&
                        saboteur.execute("CREATE VIEW Tracks (folder_id, track_id) AS "
                                         "SELECT 1, 1 UNION ALL SELECT abs(-9223372036854775807 - 1), 2;");
                    report.check(broken, "the scratch Tracks was replaced by one that fails halfway through a read");
                    if (!broken)
                    {
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }
                }

                report.check(!scratchFolders.removeEmptyFolders(), "removeEmptyFolders refuses when the read of the folders in use fails");
                report.check(countFolderRows() == folderRowsBefore,
                    std::format("it deleted no folder it could not prove was unused ({} rows before, {} after)",
                        folderRowsBefore,
                        countFolderRows()));

                // And a Tracks that is not there at all. Worth its own case even though it lands in the
                // same check: sqlite3_prepare_v2 re-prepares a statement when the schema has changed, so
                // a table that has since been dropped is reported when the statement is stepped and not
                // when it is prepared. Reading the missing table is a runtime failure, not a syntax one.
                {
                    SqliteDatabase saboteur;
                    if (!saboteur.open(pathToString(brokenDbPath)) || !saboteur.execute("DROP VIEW Tracks;"))
                    {
                        report.abort("Could not remove the scratch Tracks view.");
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }
                }

                report.check(!scratchFolders.removeEmptyFolders(), "removeEmptyFolders refuses when the table it reads is gone");
                report.check(countFolderRows() == folderRowsBefore,
                    std::format("it deleted nothing then either ({} rows)", countFolderRows()));

                // --- What a caller can now find out about a cache that did not build ---
                //
                // Tracks is gone at this point, so a rebuild fails at the read that fills the track
                // pass. The cache has to be invalidated first: removeEmptyFolders refused before it
                // committed, so it never invalidated anything, and the cache built when these folders
                // were seeded is still marked valid. Without this the fast path at the top of
                // buildCacheIfNeeded returns the old good cache and none of the checks below mean
                // anything - which is exactly what they did on the first run of this test.
                scratchFolders.invalidateCache();
                //
                // The old shape of this was that nobody could tell. buildCacheIfNeeded returned bool
                // and all eight call sites dropped it, initialize() returned void, and every accessor
                // answered out of whatever the maps held when the read gave up - so a folder came back
                // from a build that failed exactly as it does from one that worked.
                {
                    const auto cacheResult = scratchFolders.initialize();
                    report.check(!cacheResult.isOk(), "initialize() reports a folder cache that could not be built");
                    report.check(!cacheResult.errorMessage.empty(),
                        std::format("and says what went wrong rather than only that it did ('{}')", cacheResult.errorMessage));

                    std::unordered_set<FolderId> reported;
                    const auto walkResult = scratchFolders.getAllChildFolders(seeded, reported);
                    report.check(!walkResult.isOk(), "the reporting form of getAllChildFolders says the tree it walked was partial");

                    // Deliberately still answering. The point of the pair is that a caller who wants to
                    // show what could be read still can, and only the caller who must not act on a
                    // partial tree refuses - the same bargain ITrackDatabase::getTracks strikes.
                    const auto quiet = scratchFolders.getAllChildFolders(seeded);
                    report.check(quiet.size() == reported.size(),
                        std::format("the quiet form answers the same as ever, status and all ({} folder(s) either way)", quiet.size()));
                }

                // --- And the caller that must not act on it ---
                //
                // What a short scope actually costs, stated carefully, because the obvious guess is
                // wrong and this check is only worth what its reasoning is. existingTrackCache holds
                // the tracks the scan has in scope; every file found on disk is erased from it, and
                // whatever is left at the end is treated as gone. So a scope that is too short means
                // FEWER tracks in that cache, fewer leftovers, and therefore LESS deletion - it
                // under-reports rather than destroys. A scan on a partial tree does not lose data; it
                // returns true having examined a subset of the library and told nobody.
                //
                // That is what the guard buys and all it buys: the difference between a scan that
                // admits it could not determine its scope and one that reports success for work it did
                // not do. The genuinely destructive cousin - a root that could not be walked, whose
                // tracks DO reach the leftovers because the scope query does not care whether the disk
                // is plugged in - is a different defect and is tracked as issue #42.
                //
                // Both directions are checked, because a guard that refuses everything would pass the
                // first one on its own.
                {
                    std::atomic<bool> neverCancel{false};
                    // A real folder id rather than an empty list, so the scope query has something to
                    // resolve and the assertion below is about a scope that was actually asked for.
                    const auto runScan = [&neverCancel](ITrackDatabase &db, const std::vector<FolderId> &scope)
                    {
                        TrackScanner scanner{db};
                        return scanner.scan(scope, false, true, nullptr, nullptr, &neverCancel);
                    };

                    report.check(!runScan(scratch, seeded), "a scan refuses a scope it could not determine rather than reporting success for a subset");

                    // The control, on its own database. This one's Tracks was dropped outright rather
                    // than renamed, so there is nothing here to repair - and a check that only ever sees
                    // the broken case would pass just as well against a guard that refused everything.
                    {
                        const auto healthyDbPath = selfTestRoot / "foldercache-healthy" / "jucyaudio.db";
                        std::error_code healthyEc;
                        std::filesystem::remove_all(healthyDbPath.parent_path(), healthyEc);
                        std::filesystem::create_directories(healthyDbPath.parent_path(), healthyEc);

                        SqliteTrackDatabase healthy;
                        const auto healthyConnected = healthy.connect(healthyDbPath);
                        report.check(healthyConnected.isOk(),
                            std::format("a database whose reads all work could be created (said: '{}')", healthyConnected.errorMessage));

                        if (healthyConnected.isOk())
                        {
                            report.check(healthy.getFolderDatabase().initialize().isOk(), "its folder cache builds");
                            // Its own folder, so this scan resolves a non-empty scope too and the two
                            // halves differ only in whether the cache behind them could be built.
                            const auto healthyFolder = healthy.getFolderDatabase().findOrCreateFolderByPath(healthyDbPath.parent_path() / "one");
                            report.check(healthyFolder > 0, std::format("a folder could be created in it (id {})", healthyFolder));
                            report.check(runScan(healthy, {healthyFolder}),
                                "and the same scan runs against it, so the refusal is the broken cache and not scanning as such");
                        }
                    }
                }
            }

            // How a sabotaged read actually ended, asked of the exact query the cache runs.
            //
            // Every check below rests on the induced failure being *partial* - rows and then an error.
            // A total failure is a different case and a much weaker one: the album pass never learns a
            // folder id from it, so it invents nothing, and a check that could not tell the two apart
            // would stay green with the guard removed. That is not hypothetical here. The obvious
            // sabotage, the UNION ALL view the timeline suite uses, is a total failure against the
            // track query, because a UNION ALL view cannot be answered from an index and SQLite sorts -
            // which consumes every row before handing over the first.
            //
            // So the shape is asserted rather than assumed, and asserted against the production SQL, so
            // that a planner that stops streaming one day is reported here instead of quietly making
            // these checks vacuous.
            struct ReadPrefix
            {
                int64_t rows{0};
                bool prepared{false};
                bool failed{false};
            };

            const auto prefixOf = [](const std::filesystem::path &dbPath, const char *sql)
            {
                ReadPrefix result;
                SqliteDatabase probe;
                if (!probe.open(pathToString(dbPath)))
                {
                    return result;
                }
                SqliteStatement stmt{probe, sql};
                result.prepared = stmt.isValid();
                if (!result.prepared)
                {
                    return result;
                }
                while (stmt.getNextResult())
                {
                    ++result.rows;
                }
                result.failed = stmt.hasError();
                return result;
            };

            // --- A cache build that could not read the tracks invents no album ---
            //
            // The album pass decides a folder's album from the tracks it has seen so far, and it only
            // learns a folder is finished when a row for the next one arrives. So a read that stops
            // part way has seen a prefix, the last folder is still pending, and the block after the
            // loop used to add it and the transaction below used to write it - neither having asked
            // whether the read finished. The track that would have disqualified the folder was simply
            // never reached.
            //
            // That row outlives everything. A wrong track count is corrected by the next rebuild; a
            // wrong Albums row is not, because the rebuild finds an album already there and leaves it
            // alone. Nothing deletes it.
            //
            // Its own database, because it renames Tracks and would write Albums.
            {
                const auto readFailRoot = selfTestRoot / "foldercache-readfail";
                const auto readFailDb = readFailRoot / "jucyaudio.db";
                std::error_code readFailEc;
                std::filesystem::remove_all(readFailRoot, readFailEc);
                std::filesystem::create_directories(readFailRoot, readFailEc);

                SqliteTrackDatabase scratch;
                const auto connected = scratch.connect(readFailDb);
                report.check(connected.isOk(), std::format("a scratch database for the failed read could be created (said: '{}')", connected.errorMessage));

                if (connected.isOk())
                {
                    auto &folders = scratch.getFolderDatabase();

                    const auto albumCount = [&](const char *what)
                    {
                        SqliteDatabase counter;
                        if (!counter.open(pathToString(readFailDb)))
                        {
                            report.check(false, std::format("the albums could be counted {}", what));
                            return -1LL;
                        }
                        SqliteStatement stmt{counter, "SELECT COUNT(*) FROM Albums;"};
                        return (stmt.isValid() && stmt.getNextResult()) ? stmt.getInt64(0) : -1LL;
                    };

                    // One folder, four tracks, and the fourth is the one that disqualifies it: three
                    // tracks agree on artist and album, the fourth names a different artist. So a
                    // complete read must create no album at all, and a read that stops before the
                    // fourth row sees a folder that looks like one album.
                    //
                    // Filenames in index order, because ORDER BY folder_id is answered from
                    // idx_tracks_parent_filename and that decides which rows arrive first.
                    {
                        SqliteDatabase seed;
                        const bool seeded = seed.open(pathToString(readFailDb)) &&
                            seed.execute("INSERT INTO Folders (folder_id, parent_id, name, root_path) VALUES (1, NULL, 'aged', 'c:\\aged');") &&
                            seed.execute("INSERT INTO Tracks (track_id, folder_id, filename, artist_name, album_title) VALUES "
                                         "(1, 1, 'a1.mp3', 'Aged Artist', 'Aged Album'), "
                                         "(2, 1, 'a2.mp3', 'Aged Artist', 'Aged Album'), "
                                         "(3, 1, 'a3.mp3', 'Aged Artist', 'Aged Album'), "
                                         "(4, 1, 'a4.mp3', 'Other Artist', 'Aged Album');");
                        report.check(seeded, std::format("the folder and its four tracks could be seeded (said: '{}')", seed.getLastError()));
                    }

                    // The complete read first, so that "no album" below means the read was refused
                    // rather than that this fixture never had an album in it to invent.
                    folders.invalidateCache();
                    std::ignore = folders.getFolderById(1);
                    report.check(albumCount("after a complete read") == 0,
                        std::format("a complete read of four tracks that disagree creates no album ({} albums)", albumCount("after a complete read")));

                    // Tracks becomes a view that answers with part of itself and then raises. abs() of
                    // the most negative integer is a runtime error, so it lands mid-read rather than
                    // when the statement is prepared.
                    //
                    // A simple view with the error in its WHERE, not the UNION ALL form the timeline
                    // suite uses: this query has an ORDER BY, and a UNION ALL view cannot be answered
                    // from the index, so SQLite sorts - which consumes every row, and the error then
                    // arrives before the first one is handed over. That would be a total failure, and
                    // a total failure invents nothing, because the pass never learns a folder id. The
                    // flattened form streams through idx_tracks_parent_filename and fails partway,
                    // which is the case the fix is about.
                    {
                        SqliteDatabase saboteur;
                        const bool broken = saboteur.open(pathToString(readFailDb)) &&
                            saboteur.execute("PRAGMA legacy_alter_table=ON;") &&
                            saboteur.execute("ALTER TABLE Tracks RENAME TO Tracks_real;") &&
                            saboteur.execute("CREATE VIEW Tracks AS SELECT * FROM Tracks_real "
                                             "WHERE (CASE WHEN track_id >= 4 THEN abs(-9223372036854775807 - 1) ELSE 1 END) = 1;");
                        report.check(broken, "Tracks was replaced by one that answers with part of itself and then fails");
                    }

                    {
                        const auto prefix = prefixOf(readFailDb,
                            "SELECT folder_id, COALESCE(artist_name, ''), COALESCE(album_title, '') FROM Tracks ORDER BY folder_ID ASC");
                        report.check(prefix.prepared && prefix.rows > 0 && prefix.failed,
                            std::format("the track read really does answer and then fail - {} row(s), prepared={}, failed={}",
                                prefix.rows,
                                prefix.prepared,
                                prefix.failed));
                    }

                    folders.invalidateCache();
                    std::ignore = folders.getFolderById(1);

                    const auto afterFailure = albumCount("after a failed read");
                    report.check(afterFailure == 0,
                        std::format("a read that stopped partway writes no album from the folder it never finished ({} albums)", afterFailure));

                    // And the cache still works once the table is back, so what the fix refuses is the
                    // failed read and not the folder.
                    {
                        SqliteDatabase repair;
                        const bool restored = repair.open(pathToString(readFailDb)) && repair.execute("PRAGMA legacy_alter_table=ON;") &&
                            repair.execute("DROP VIEW Tracks;") && repair.execute("ALTER TABLE Tracks_real RENAME TO Tracks;");
                        report.check(restored, "Tracks was put back");
                    }

                    folders.invalidateCache();
                    const auto recovered = folders.getFolderById(1);
                    report.check(recovered.has_value() && recovered->folderId == 1, "the folder reads back once its tracks can be read again");
                    report.check(albumCount("after the repair") == 0, "and the complete read still creates no album");
                }
            }

            // --- And a cache build that could not read the albums invents no album either ---
            //
            // The other direction, and it writes. What Albums already holds decides what the pass may
            // add: a folder whose existing album does not describe its tracks is left alone, and a
            // folder with no album at all gets one. A read that stops before a folder's row cannot
            // tell those apart, so it takes the second branch and writes an album the complete read
            // would have suppressed.
            //
            // The unique index on (title, folder_id) does not stand in the way, which is the point of
            // the shape below: the invented album carries the tracks' title, the existing one carries
            // a different title, so the index permits both and the folder ends up with two.
            //
            // Its own database again, and Tracks is left alone here - this failure is in the Albums
            // read, and mixing the two would not say which guard refused the build.
            {
                const auto albumFailRoot = selfTestRoot / "foldercache-albumfail";
                const auto albumFailDb = albumFailRoot / "jucyaudio.db";
                std::error_code albumEc;
                std::filesystem::remove_all(albumFailRoot, albumEc);
                std::filesystem::create_directories(albumFailRoot, albumEc);

                SqliteTrackDatabase scratch;
                const auto connected = scratch.connect(albumFailDb);
                report.check(connected.isOk(),
                    std::format("a scratch database for the failed album read could be created (said: '{}')", connected.errorMessage));

                if (connected.isOk())
                {
                    auto &folders = scratch.getFolderDatabase();

                    // Counted from the real table, because Albums becomes a view below.
                    const auto albumCount = [&](const char *table)
                    {
                        SqliteDatabase counter;
                        if (!counter.open(pathToString(albumFailDb)))
                        {
                            return -1LL;
                        }
                        SqliteStatement stmt{counter, std::format("SELECT COUNT(*) FROM {};", table)};
                        return (stmt.isValid() && stmt.getNextResult()) ? stmt.getInt64(0) : -1LL;
                    };

                    {
                        SqliteDatabase seed;
                        const bool seeded = seed.open(pathToString(albumFailDb)) &&
                            seed.execute("INSERT INTO Folders (folder_id, parent_id, name, root_path) VALUES "
                                         "(1, NULL, 'aged', 'c:\\aged'), (2, NULL, 'other', 'c:\\other'), (3, NULL, 'third', 'c:\\third');") &&
                            // Two tracks, agreeing, so the folder qualifies for an album at all.
                            seed.execute("INSERT INTO Tracks (track_id, folder_id, filename, artist_name, album_title) VALUES "
                                         "(1, 1, 'a1.mp3', 'Aged Artist', 'Aged Album'), "
                                         "(2, 1, 'a2.mp3', 'Aged Artist', 'Aged Album');") &&
                            // Folder 1's album names something else, so a complete read suppresses any
                            // new one. It is last, so the rows before it are the prefix the sabotaged
                            // read gets through before failing.
                            seed.execute("INSERT INTO Albums (album_id, album_artist, title, folder_id) VALUES "
                                         "(1, 'Other One', 'Album One', 2), "
                                         "(2, 'Other Two', 'Album Two', 3), "
                                         "(3, 'Someone Else', 'Something Else', 1);");
                        report.check(seeded, std::format("the folders, tracks and albums could be seeded (said: '{}')", seed.getLastError()));
                    }

                    folders.invalidateCache();
                    std::ignore = folders.getFolderById(1);
                    report.check(albumCount("Albums") == 3,
                        std::format("a complete read leaves the folder whose album says something else alone ({} albums)", albumCount("Albums")));

                    // Albums answers with part of itself and then raises, the same flattened-view trick
                    // the track sabotage uses. The INSTEAD OF trigger keeps the write working: without
                    // it the build's INSERT would fail against a view, and a build that failed to write
                    // would look like a build that declined to - which is the distinction being tested.
                    {
                        SqliteDatabase saboteur;
                        const bool broken = saboteur.open(pathToString(albumFailDb)) &&
                            saboteur.execute("PRAGMA legacy_alter_table=ON;") &&
                            saboteur.execute("ALTER TABLE Albums RENAME TO Albums_real;") &&
                            saboteur.execute("CREATE VIEW Albums AS SELECT * FROM Albums_real "
                                             "WHERE (CASE WHEN album_id >= 3 THEN abs(-9223372036854775807 - 1) ELSE 1 END) = 1;") &&
                            saboteur.execute("CREATE TRIGGER albums_insert INSTEAD OF INSERT ON Albums BEGIN "
                                             "INSERT INTO Albums_real (album_artist, title, folder_id) "
                                             "VALUES (new.album_artist, new.title, new.folder_id); END;");
                        report.check(broken, "Albums was replaced by one that answers with part of itself and then fails");
                    }

                    {
                        const auto prefix = prefixOf(albumFailDb, "SELECT album_id, album_artist, title, folder_id FROM Albums");
                        report.check(prefix.prepared && prefix.rows > 0 && prefix.failed,
                            std::format("the album read really does answer and then fail - {} row(s), prepared={}, failed={}",
                                prefix.rows,
                                prefix.prepared,
                                prefix.failed));
                    }

                    folders.invalidateCache();
                    std::ignore = folders.getFolderById(1);

                    const auto afterFailure = albumCount("Albums_real");
                    report.check(afterFailure == 3,
                        std::format("a read that stopped partway writes no album for a folder it never saw the album of ({} albums)", afterFailure));

                    {
                        SqliteDatabase repair;
                        const bool restored = repair.open(pathToString(albumFailDb)) && repair.execute("PRAGMA legacy_alter_table=ON;") &&
                            repair.execute("DROP TRIGGER albums_insert;") && repair.execute("DROP VIEW Albums;") &&
                            repair.execute("ALTER TABLE Albums_real RENAME TO Albums;");
                        report.check(restored, "Albums was put back");
                    }

                    folders.invalidateCache();
                    const auto recovered = folders.getFolderById(1);
                    report.check(recovered.has_value() && recovered->folderId == 1, "the folder reads back once its albums can be read again");
                    report.check(albumCount("Albums") == 3, "and the complete read still leaves that folder alone");
                }
            }

            writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
            spdlog::info("[SelfTest] Folder cache test finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }


        int runTransactionSelfTest(const std::filesystem::path &selfTestRoot)
        {
            Report report;
            const auto workRoot = selfTestRoot / "transaction";
            const auto resultsPath = selfTestRoot / "transaction-results.txt";
            const auto dbPath = workRoot / "isolation.db";

            spdlog::info("[SelfTest] Starting transaction self test. Root: {}", pathToString(selfTestRoot));

            std::error_code ec;
            std::filesystem::remove_all(workRoot, ec);
            std::filesystem::create_directories(workRoot, ec);
            if (ec)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(workRoot), ec.message()));
                writeResultsFile(resultsPath, "jucyaudio transaction self test", report);
                return 1;
            }

            // Its own connection, not the library's: this test holds a transaction open on purpose while
            // another thread hammers the same connection, and nothing else should be waiting behind it.
            SqliteDatabase db;
            if (!db.open(pathToString(dbPath)) || !db.execute("CREATE TABLE Marks (name TEXT NOT NULL);"))
            {
                report.abort(std::format("Could not set up the scratch database: {}", db.getLastError()));
                writeResultsFile(resultsPath, "jucyaudio transaction self test", report);
                return 1;
            }

            // Two threads, one connection. The holder begins a transaction, writes a row and then stands
            // still with it open. The intruder, told that the transaction is open, runs a plain INSERT
            // and then a transaction of its own. Then the holder rolls back.
            //
            // A SQLite transaction belongs to the connection, so on a connection nobody owns the
            // intruder's plain INSERT lands inside the holder's transaction and goes with the rollback,
            // and its own BEGIN is refused because the connection is already in one. On an owned
            // connection the intruder waits at the mutex until the rollback, and both writes stand.
            using Clock = std::chrono::steady_clock;
            const auto ticksNow = []() -> int64_t
            {
                return Clock::now().time_since_epoch().count();
            };
            std::atomic<bool> holderBegan{false};
            std::atomic<bool> holderBeginOk{false};
            std::atomic<bool> holderWriteOk{false};
            std::atomic<bool> intruderAboutToWrite{false};
            std::atomic<bool> holderFinished{false};
            std::atomic<bool> intruderFinished{false};
            std::atomic<bool> plainWriteOk{false};
            std::atomic<bool> intruderBeginOk{false};
            std::atomic<bool> intruderCommitOk{false};
            std::atomic<int64_t> rollbackStartedAt{0};
            std::atomic<int64_t> plainWriteAt{0};

            // Generous: this is a deadline for a hang, not a performance assertion.
            const auto deadline = Clock::now() + std::chrono::seconds{30};

            std::thread holder{[&]()
                {
                    SqliteTransaction transaction{db};
                    holderBeginOk = static_cast<bool>(transaction);
                    if (transaction)
                    {
                        holderWriteOk = transaction.execute("INSERT INTO Marks (name) VALUES ('holder');");
                    }
                    holderBegan = true;

                    while (!intruderAboutToWrite && Clock::now() < deadline)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    }
                    // Long enough for a statement on an unowned connection to get in. On an owned one
                    // this is time the intruder spends waiting at the mutex.
                    std::this_thread::sleep_for(std::chrono::milliseconds{250});
                    // Stamped before, not after: rollback() hands the connection back on its way out, and
                    // the intruder can have written before this thread gets to the next line.
                    rollbackStartedAt = ticksNow();
                    transaction.rollback();
                    holderFinished = true;
                }};

            std::thread intruder{[&]()
                {
                    while (!holderBegan && Clock::now() < deadline)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    }
                    intruderAboutToWrite = true;
                    {
                        SqliteStatement stmt{db, "INSERT INTO Marks (name) VALUES ('plain');"};
                        plainWriteOk = stmt.execute();
                        plainWriteAt = ticksNow();
                    }
                    {
                        SqliteTransaction transaction{db};
                        intruderBeginOk = static_cast<bool>(transaction);
                        if (transaction && transaction.execute("INSERT INTO Marks (name) VALUES ('transacted');"))
                        {
                            intruderCommitOk = transaction.commit();
                        }
                    }
                    intruderFinished = true;
                }};

            while ((!holderFinished || !intruderFinished) && Clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }

            if (!holderFinished || !intruderFinished)
            {
                report.abort(std::format("Timed out with the holder {} and the intruder {}. A thread that never gets the connection back is what this looks like.",
                    holderFinished ? "finished" : "stuck",
                    intruderFinished ? "finished" : "stuck"));
                writeResultsFile(resultsPath, "jucyaudio transaction self test", report);

                // Out, without unwinding, for the same reason the folder cache test does it: a thread
                // stuck on a mutex can be neither joined nor left behind with references to this frame.
                spdlog::error("[SelfTest] Transaction test timed out; leaving the process without unwinding.");
                spdlog::default_logger()->flush();
                std::_Exit(1);
            }

            holder.join();
            intruder.join();

            report.check(holderBeginOk && holderWriteOk, "the holder's transaction began and wrote its row");
            report.check(plainWriteOk, "the intruder's plain INSERT executed");
            report.check(intruderBeginOk, "the intruder's own transaction began while the holder's was open - it waited rather than being refused");
            report.check(intruderCommitOk, "the intruder's transaction committed");
            report.check(plainWriteAt > rollbackStartedAt, "the intruder's INSERT ran after the holder's rollback began, not inside its transaction");

            const auto countOf = [&db](std::string_view name) -> int64_t
            {
                SqliteStatement stmt{db, "SELECT COUNT(*) FROM Marks WHERE name = ?;"};
                stmt.addParam(name);
                return stmt.execute() ? stmt.getInt64(0) : -1;
            };
            report.check(countOf("holder") == 0, "the holder's row went with its rollback");
            report.check(countOf("plain") == 1, "the intruder's plain INSERT survived the holder's rollback");
            report.check(countOf("transacted") == 1, "the intruder's transacted INSERT survived the holder's rollback");

            // The other way a transaction ends up owning nothing: BEGIN IMMEDIATE itself is refused. A
            // second connection holds the write lock, so this connection's BEGIN comes back SQLITE_BUSY -
            // quickly, because the busy timeout is shortened first. The lock is then released, and a
            // write goes through the transaction that never began, the way a caller that does not check
            // its transaction would issue one. With nothing begun that write would autocommit on its
            // own, so it has to be refused, and nothing may reach the table.
            {
                SqliteDatabase blocker;
                report.check(blocker.open(pathToString(dbPath)) && blocker.execute("BEGIN IMMEDIATE;"),
                    "a second connection could take the write lock");
                report.check(db.execute("PRAGMA busy_timeout = 100;"), "the busy timeout could be shortened for the refused BEGIN");

                SqliteTransaction refused{db};
                report.check(!refused, "BEGIN IMMEDIATE was refused while another connection held the write lock");

                report.check(blocker.execute("ROLLBACK;"), "the second connection released the write lock");

                report.check(!refused.execute("INSERT INTO Marks (name) VALUES ('orphan');"),
                    "a write through a transaction that never began is refused");
                report.check(countOf("orphan") == 0, "nothing reached the table through the refused transaction");
            }

            writeResultsFile(resultsPath, "jucyaudio transaction self test", report);
            spdlog::info("[SelfTest] Transaction test finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runAudioFormatSelfTest(const std::filesystem::path &selfTestRoot)
        {
            Report report;
            const auto resultsPath = selfTestRoot / "audioformat-results.txt";
            const auto workingDir = selfTestRoot / "audioformat";

            std::error_code ec;
            std::filesystem::remove_all(workingDir, ec);
            std::filesystem::create_directories(workingDir, ec);
            if (ec)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(workingDir), ec.message()));
                writeResultsFile(resultsPath, "jucyaudio audio format self test", report);
                return 1;
            }

            juce::AudioFormatManager formatManager;
            formatManager.registerBasicFormats();

            // A WAV whose last chunk has an odd length and no pad byte after it.
            {
                const auto wavPath = workingDir / "odd-data-no-pad.wav";
                const bool written = writeOddLengthWavWithoutFinalPadByte(wavPath, kOddSampleCount);
                report.check(written, std::format("the odd-length WAV fixture could be written ({} samples, no trailing pad byte)", kOddSampleCount));

                if (written)
                {
                    const auto actualSize = std::filesystem::file_size(wavPath, ec);
                    report.check(!ec && (actualSize % 2) == 1,
                        std::format("the fixture really is odd-sized on disk ({} bytes)", ec ? static_cast<std::uintmax_t>(0) : actualSize));

                    std::unique_ptr<juce::AudioFormatReader> reader{
                        formatManager.createReaderFor(juce::File{juce::String{pathToString(wavPath)}})};
                    report.check(reader != nullptr, "a reader opens the WAV that is missing its final pad byte");

                    if (reader != nullptr)
                    {
                        report.check(reader->lengthInSamples == static_cast<juce::int64>(kOddSampleCount),
                            std::format("the reader reports all {} samples (said {})", kOddSampleCount, static_cast<int64_t>(reader->lengthInSamples)));

                        juce::AudioBuffer<float> buffer{static_cast<int>(reader->numChannels), static_cast<int>(kOddSampleCount)};
                        buffer.clear();
                        const bool read = reader->read(&buffer, 0, static_cast<int>(kOddSampleCount), 0, true, false);
                        report.check(read, "every sample of it reads back");

                        // The fixture is a square wave. A decode that quietly produced nothing fails
                        // here even though the read above said it succeeded.
                        report.check(buffer.getMagnitude(0, static_cast<int>(kOddSampleCount)) > 0.0f,
                            "and what reads back is the fixture's audio, not silence");
                    }
                }
            }

            // The same omission one chunk later: a final LIST chunk of odd length, unpadded. Kept as a
            // regression guard - it passed on both 9.0.0 and 9.0.2, so it is not evidence for the
            // upgrade, only a check that a shape which works today goes on working.
            {
                const auto wavPath = workingDir / "odd-list-no-pad.wav";
                std::string expectedLabel;
                const bool written = writeWavWithUnpaddedFinalListChunk(wavPath, expectedLabel);
                report.check(written, "the WAV with an unpadded odd-length final LIST chunk could be written");

                if (written)
                {
                    std::unique_ptr<juce::AudioFormatReader> reader{
                        formatManager.createReaderFor(juce::File{juce::String{pathToString(wavPath)}})};
                    report.check(reader != nullptr, "a reader opens it");

                    if (reader != nullptr)
                    {
                        report.check(reader->lengthInSamples == static_cast<juce::int64>(kListFixtureSampleCount),
                            std::format("its {} samples are all there (said {})", kListFixtureSampleCount, static_cast<int64_t>(reader->lengthInSamples)));

                        // The metadata behind the missing pad byte. A parser that runs the last chunk to
                        // a boundary one byte past the file could read the label against the wrong end
                        // and come back with nothing, having reported a perfectly good reader and the
                        // right sample count. Both tested versions read it correctly.
                        const auto label = reader->metadataValues.getValue("CueLabel0Text", juce::String{}).toStdString();
                        report.check(label == expectedLabel, std::format("and the cue label behind the missing pad byte reads back as '{}' (said '{}')", expectedLabel, label));
                    }
                }
            }

            // A truncated WAV whose header still describes the whole thing.
            {
                const auto wavPath = workingDir / "overclaiming-data.wav";
                constexpr uint32_t actualBytes = 6;
                const bool written = writeWavWithOverclaimingDataChunk(wavPath, kOverclaimedSampleCount, actualBytes);
                report.check(written, std::format("the truncated WAV could be written - its data chunk claims {} bytes and {} are there", kOverclaimedSampleCount, actualBytes));

                if (written)
                {
                    std::unique_ptr<juce::AudioFormatReader> reader{
                        formatManager.createReaderFor(juce::File{juce::String{pathToString(wavPath)}})};

                    // The invariant is that the samples which do not exist are not reported, not that
                    // the reader answers in one particular way. Refusing the file is correct; so is
                    // opening it and reporting nothing; so would be clamping to the six samples that
                    // are really there. Only a length past the end of the file is wrong, so that is
                    // what the check draws the line at - a reader that legitimately clamped would pass
                    // here rather than fail a test pinned to zero.
                    const auto reported = reader == nullptr ? int64_t{-1} : static_cast<int64_t>(reader->lengthInSamples);
                    report.check(reader == nullptr || reader->lengthInSamples <= static_cast<juce::int64>(actualBytes),
                        std::format("a data chunk claiming more than the file holds is not believed (reader {}, length {}, at most {} real samples)",
                            reader == nullptr ? "refused" : "opened",
                            reported,
                            actualBytes));
                }
            }

            // A VBR MP3 with bytes between the end of the ID3v2 tag and the first frame sync.
            {
                const auto mp3Path = workingDir / "vbr-padded-id3v2.mp3";
                std::vector<unsigned char> frames;
                const auto encodeError = encodeVbrMp3Frames(frames);
                report.check(encodeError.empty(),
                    std::format("a VBR MP3 could be encoded for the fixture ({} bytes of frames){}",
                        frames.size(),
                        encodeError.empty() ? std::string{} : std::format(" - {}", encodeError)));

                if (encodeError.empty())
                {
                    const bool written = writeMp3WithPaddingAfterId3v2(mp3Path, frames, kDeclaredId3Padding, kExtraPaddingAfterId3);
                    report.check(written,
                        std::format("the fixture could be written - a {}-byte ID3v2 tag followed by {} bytes that are not a frame sync",
                            kDeclaredId3Padding + kId3HeaderBytes,
                            kExtraPaddingAfterId3));

                    if (written)
                    {
                        // Before asking what a reader makes of the fixture, check the fixture is the
                        // one shape it is supposed to be. A file malformed in a second, unintended way
                        // would still fail on 9.0.0 and pass on 9.0.2, and the mutation proof would be
                        // evidence for the wrong thing. This is exactly how the first version of this
                        // suite was wrong: the finished Xing frame was put in front of the placeholder
                        // rather than over it, and 42 frames sat behind a header declaring 40.
                        const auto shape = describeMp3Fixture(mp3Path, kId3HeaderBytes + kDeclaredId3Padding + kExtraPaddingAfterId3);
                        report.check(shape.problem.empty(), std::format("the fixture's frames walk end to end{}", shape.problem.empty() ? "" : std::format(" - {}", shape.problem)));
                        report.check(shape.firstFrameCarriesVbrTag, "its first frame is the Xing header, so it reads as VBR");
                        report.check(shape.declaredBytes == shape.presentBytes,
                            std::format("the Xing header's byte count matches what is there ({} declared, {} present)", shape.declaredBytes, shape.presentBytes));
                        report.check(shape.parsedFrames == shape.declaredFrames + 1,
                            std::format("and its frame count does too - {} audio frames plus the tag frame, {} walked", shape.declaredFrames, shape.parsedFrames));

                        std::unique_ptr<juce::AudioFormatReader> reader{
                            formatManager.createReaderFor(juce::File{juce::String{pathToString(mp3Path)}})};
                        report.check(reader != nullptr, "a reader opens the VBR MP3 that has padding after its ID3v2 header");

                        if (reader != nullptr)
                        {
                            // Not ">0". A reader that stops at the padding still finds some frames -
                            // on JUCE 9.0.0 this fixture reports 20736 samples, 18 frames of the 40
                            // that are there - so the check is that the whole second is there.
                            report.check(reader->lengthInSamples >= kMp3EncodedSamples,
                                std::format("the reader finds every frame past the padding ({} samples, at least {} expected)",
                                    static_cast<int64_t>(reader->lengthInSamples),
                                    kMp3EncodedSamples));

                            const auto toRead = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, kMp3SamplesToRead));
                            if (toRead > 0)
                            {
                                juce::AudioBuffer<float> buffer{static_cast<int>(reader->numChannels), toRead};
                                buffer.clear();
                                const bool read = reader->read(&buffer, 0, toRead, 0, true, false);
                                report.check(read, std::format("{} samples of it decode", toRead));

                                // A reader that opened the file, reported a length and then handed back
                                // an empty buffer - which is what losing the frames behind the padding
                                // looks like from outside - fails here rather than passing three checks
                                // and saying nothing.
                                report.check(buffer.getMagnitude(0, toRead) > 0.0f, "and what decodes is the tone that was encoded, not silence");
                            }
                            else
                            {
                                report.check(false, "there was something to read");
                            }
                        }
                    }
                }
            }

            // An ID3v2.4 tag whose title frame is zlib-compressed.
            //
            // The one path in this binary that calls zlib for a user's file: TagLib inflates the frame
            // body before it parses the text. On macOS TagLib is built against the SDK's libz, and
            // JUCE's embedded zlib is switched off there so that the executable does not define a second
            // inflate beside the one libz exports. The fixture is deflated by JUCE and inflated by
            // TagLib - a round trip between the two users, not within one - so it passes only if both
            // are bound to a working zlib, whichever copy that is on the platform at hand.
            {
                const auto mp3Path = workingDir / "compressed-title-frame.mp3";
                const std::string title{"Compressed Title \xc3\xa9\xc3\xa8 Self Test"}; // with two non-ASCII characters
                std::vector<unsigned char> frames;
                const auto encodeError = encodeVbrMp3Frames(frames);
                report.check(encodeError.empty(),
                    std::format("an MP3 could be encoded to carry the compressed frame{}", encodeError.empty() ? std::string{} : std::format(" - {}", encodeError)));

                if (encodeError.empty())
                {
                    std::vector<unsigned char> compressed;
                    const bool written = writeMp3WithCompressedTitleFrame(mp3Path, frames, title, compressed);
                    report.check(written,
                        std::format("the fixture could be written - a v2.4 TIT2 frame whose {}-byte text field is a {}-byte zlib stream",
                            title.size() + 1,
                            compressed.size()));

                    if (written)
                    {
                        // JUCE's half on its own first, so that a failure below is attributable: what
                        // JUCE deflated, JUCE inflates back.
                        const auto roundTrip = inflateThroughJuce(compressed);
                        // Compared as strings, not element by element: the title has bytes above 0x7F,
                        // and char against unsigned char makes those unequal while looking right.
                        const std::string inflatedText{roundTrip.begin() + (roundTrip.empty() ? 0 : 1), roundTrip.end()};
                        report.check(!roundTrip.empty() && roundTrip.front() == 3 && inflatedText == title,
                            std::format("JUCE inflates its own stream back to the {} bytes it deflated (got {}: '{}')", title.size() + 1, roundTrip.size(), inflatedText));

                        // The fixture is the shape under test: TagLib parses the frame and sees the
                        // compression flag. Without this a scanner that read the title would prove
                        // only that plain frames work.
                        {
                            TagLib::MPEG::File file{mp3Path.c_str()};
                            const auto *tag = file.ID3v2Tag();
                            const bool flagged = tag != nullptr && !tag->frameList("TIT2").isEmpty() && tag->frameList("TIT2").front()->header()->compression();
                            report.check(flagged, "TagLib parses the frame and sees its compression flag, so the fixture is the shape under test");
                        }

                        // Then the production path: the scanner that fills a TrackInfo during a library
                        // scan, through the interface the scan drives it by.
                        NoTagManager noTags;
                        database::scanners::Id3TagScanner scanner{noTags};
                        database::ITrackInfoScanner &viaInterface = scanner;
                        database::TrackInfo info{};
                        const auto established = viaInterface.processTrack(info, mp3Path);
                        report.check(database::includes(established, database::ScannedFields::Tags), "the scanner reads the tag");
                        report.check(database::includes(established, database::ScannedFields::AudioProperties) && info.duration > Duration_t{0},
                            std::format("and the audio behind it ({} ms)", info.duration.count()));

                        // Asserted on both platforms now, where this used to assert on macOS and
                        // record a note on Windows.
                        //
                        // Without a zlib TagLib does not attempt the frame at all: its factory hands
                        // back an UnknownFrame whose text is empty, so the scanner reports the tag as
                        // read and writes an empty title - silent data loss on a scan that says it
                        // succeeded. That was Windows until issue #58, because nothing there answered
                        // find_package(ZLIB). It now borrows the zlib JUCE already compiles into this
                        // executable; see the comment in CMakeLists.txt next to the HAVE_ZLIB define.
                        //
                        // isAvailable() is checked first and separately, because it is the difference
                        // between "the decoder was not built" and "the decoder is wrong". Those want
                        // different fixes, and one assertion on the title alone cannot tell them apart.
                        report.check(TagLib::zlib::isAvailable(), "TagLib on this platform was built with a zlib");
                        report.check(info.title == title, std::format("and the compressed title inflates to what was written (scanner said '{}')", info.title));
                    }
                }
            }

            writeResultsFile(resultsPath, "jucyaudio audio format self test", report);
            spdlog::info("[SelfTest] Audio format test finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }
    } // namespace tests
} // namespace jucyaudio
