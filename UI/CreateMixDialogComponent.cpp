#include <Database/Includes/ILongRunningTask.h>
#include <Database/Includes/MixTrackUtils.h>
#include <Database/BackgroundTasks/EnergyAnalysisTask.h>
#include <Database/BackgroundTasks/EnergyAnalyzer.h>
#include <Database/BackgroundTasks/TransitionCalculator.h>
#include <UI/CreateMixDialogComponent.h>
#include <UI/Settings.h>
#include <UI/MainComponent.h>
#include <UI/SkippedTracksDialog.h>
#include <UI/TaskDialog.h>
#include <Utils/AssortedUtils.h> // For pathToString, durationToString if needed for logging
#include <ctime>
#include <filesystem>
#include <format>
#include <iomanip>
#include <locale>
#include <memory>
#include <spdlog/spdlog.h>
#include <sstream>

// Forward declare if TrackLibrary provides these directly, or include necessary headers
// Assuming TrackLibrary provides access to IMixManager and IMixExporter

namespace
{
    using jucyaudio::database::TrackInfo;
    using jucyaudio::pathToString;

    /// @brief How to call a track in a message: "Artist - Title", falling back as fields run out.
    std::string describeTrack(const TrackInfo &track)
    {
        if (!track.artist_name.empty() && !track.title.empty())
        {
            return std::format("{} - {}", track.artist_name, track.title);
        }
        if (!track.title.empty())
        {
            return track.title;
        }
        return track.filename;
    }

    /// @brief Stats a list of files on a background thread and reports which of them are gone.
    ///
    /// A task rather than an inline loop because an unavailable drive is exactly the case this check
    /// exists to catch, and a single std::filesystem::exists() against a disconnected network path can
    /// block for an OS-level timeout. On the message thread that freezes the window, and takes the
    /// cancel button with it.
    ///
    /// Results are indices into the caller's list, not paths or track ids: the caller erases from its
    /// own vector, and a mix may legitimately contain the same track id twice.
    class MissingFileScanTask final : public jucyaudio::database::ILongRunningTask
    {
    public:
        MissingFileScanTask(std::vector<std::filesystem::path> paths, std::shared_ptr<std::vector<size_t>> missingIndices)
            : ILongRunningTask{"Checking Files", true},
              m_paths{std::move(paths)},
              m_missingIndices{std::move(missingIndices)}
        {
        }

        void run(jucyaudio::database::ProgressCallback progressCb,
            jucyaudio::database::CompletionCallback completionCb,
            std::atomic<bool> &shouldCancel) override
        {
            for (size_t i = 0; i < m_paths.size(); ++i)
            {
                if (shouldCancel.load())
                {
                    completionCb(false, "Cancelled.");
                    return;
                }

                // Named, not just counted: if one lookup does hang, the user needs to see on what.
                // pathToString, not path::string(): the narrow form of a Windows path is not UTF-8 and
                // throws outright on characters the active code page cannot represent.
                progressCb(static_cast<int>((i * 100) / m_paths.size()), std::format("Checking {}", pathToString(m_paths[i].filename())));

                // The error_code overload: the throwing one would abort mix creation over a malformed
                // path, which is precisely the case we are here to report politely.
                std::error_code ec;
                if (!std::filesystem::exists(m_paths[i], ec))
                {
                    m_missingIndices->push_back(i);
                }
            }

            completionCb(true, std::format("Checked {} file(s), {} missing.", m_paths.size(), m_missingIndices->size()));
        }

    private:
        std::vector<std::filesystem::path> m_paths;
        std::shared_ptr<std::vector<size_t>> m_missingIndices;
    };
} // namespace

namespace jucyaudio
{
    namespace ui
    {
        CreateMixDialogComponent::CreateMixDialogComponent(
            const std::vector<database::TrackInfo> &tracksForMix, WorkingSetId source_ws_id, OnMixCreatedAndExportedCallback onOkCallback)
            : m_tracksForMix{tracksForMix}, // Store reference
              m_source_ws_id{source_ws_id},
              m_onOkCallback{std::move(onOkCallback)},
              m_titleLabel{"titleLabel", "Create Mix"},
              m_countLabel{"countLabel", ""},
              m_mixSelectLabel{"mixSelectLabel", "Target Mix:"},
              m_mixSelectCombo{"mixSelectCombo"},
              m_nameLabel{"nameLabel", "Mix Name:"},
              m_nameEditor{"nameEditor"},
              m_okButton{"Create"},
              m_cancelButton{"Cancel"}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);
            setSize(450, 280); // Made taller to accommodate combo box

            // Title label
            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(24.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);

            // Count label
            addAndMakeVisible(m_countLabel);
            juce::String countText = "Create a mix from these " + juce::String(m_tracksForMix.size()) + " tracks?";
            m_countLabel.setText(countText, juce::dontSendNotification);
            m_countLabel.setJustificationType(juce::Justification::centred);

            // Mix selection combo box
            addAndMakeVisible(m_mixSelectLabel);
            addAndMakeVisible(m_mixSelectCombo);
            m_mixSelectCombo.addListener(this);

            // Load existing mixes
            m_availableMixes = theTrackLibrary.getMixManager().getMixes({});

            // Add empty option for "Create New Mix"
            m_mixSelectCombo.addItem("<Create New Mix>", 1);
            m_mixSelectCombo.addSeparator();

            // Add existing mixes
            for (size_t i = 0; i < m_availableMixes.size(); ++i)
            {
                m_mixSelectCombo.addItem(m_availableMixes[i].name, static_cast<int>(i + 2));
            }

            // Select "Create New Mix" by default
            m_mixSelectCombo.setSelectedId(1);

            // Name label and editor
            addAndMakeVisible(m_nameLabel);
            addAndMakeVisible(m_nameEditor);
            m_nameEditor.setText(generateDefaultMixName(), false);
            m_nameEditor.selectAll();
            m_nameEditor.addListener(this);

            // Buttons
            addAndMakeVisible(m_okButton);
            addAndMakeVisible(m_cancelButton);
            m_okButton.addListener(this);
            m_cancelButton.addListener(this);

            // Set initial focus to name editor after dialog is shown
            juce::Component::SafePointer<CreateMixDialogComponent> safeThis = this;
            juce::MessageManager::callAsync(
                [safeThis]()
                {
                    if (safeThis && safeThis->isShowing())
                    {
                        safeThis->m_nameEditor.grabKeyboardFocus();
                    }
                });
        }

        CreateMixDialogComponent::~CreateMixDialogComponent()
        {
            setLookAndFeel(nullptr);
            // Listeners are automatically removed by JUCE
        }

        void CreateMixDialogComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void CreateMixDialogComponent::resized()
        {
            juce::Rectangle<int> area = getLocalBounds().reduced(20);

            // Title
            m_titleLabel.setBounds(area.removeFromTop(30));
            area.removeFromTop(10); // spacing

            // Count message
            m_countLabel.setBounds(area.removeFromTop(25));
            area.removeFromTop(15); // spacing

            // Mix selection row
            auto mixSelectRow = area.removeFromTop(25);
            m_mixSelectLabel.setBounds(mixSelectRow.removeFromLeft(80));
            mixSelectRow.removeFromLeft(10); // spacing
            m_mixSelectCombo.setBounds(mixSelectRow);

            area.removeFromTop(10); // spacing

            // Name input row
            auto nameRow = area.removeFromTop(25);
            m_nameLabel.setBounds(nameRow.removeFromLeft(80)); // Adjusted for "Mix Name:"
            nameRow.removeFromLeft(10);                        // spacing
            m_nameEditor.setBounds(nameRow);

            area.removeFromTop(20); // spacing before buttons

            // Buttons at bottom
            auto buttonRow = area.removeFromBottom(30);
            int okButtonWidth = 120; // Wider for "Create & Export"
            int cancelButtonWidth = 80;
            int buttonSpacing = 10;

            // Place cancel button on the right, then OK button
            m_cancelButton.setBounds(buttonRow.removeFromRight(cancelButtonWidth));
            buttonRow.removeFromRight(buttonSpacing);
            m_okButton.setBounds(buttonRow.removeFromRight(okButtonWidth));
        }

        void CreateMixDialogComponent::buttonClicked(juce::Button *button)
        {
            if (button == &m_okButton)
            {
                handleCreateMix();
            }
            else if (button == &m_cancelButton)
            {
                handleCancel();
            }
        }

        void CreateMixDialogComponent::textEditorReturnKeyPressed(juce::TextEditor & /*editor*/)
        {
            handleCreateMix();
        }

        bool CreateMixDialogComponent::keyPressed(const juce::KeyPress &key)
        {
            if (key == juce::KeyPress::escapeKey)
            {
                handleCancel();
                return true;
            }
            return juce::Component::keyPressed(key);
        }

        void CreateMixDialogComponent::handleCreateMix()
        {
            if (m_tracksForMix.empty())
            {
                spdlog::warn("No tracks provided to create mix.");
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "No Tracks", "Cannot create a mix with zero tracks.");
                closeThisDialog(false);
                return;
            }

            // Validate the name before anything expensive or interactive, so the user is never asked
            // about missing files and only then told the name is blank.
            if (m_mixSelectCombo.getSelectedId() == 1 && m_nameEditor.getText().trim().isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Invalid Mix Name", "Please enter a name for the mix.");
                m_nameEditor.grabKeyboardFocus();
                return;
            }

            // Paths are resolved here, on the message thread, because reconstructFullPath() reads the
            // library's folder cache and is not for a worker to call. The resolution itself is in-memory;
            // only the stat calls that follow can block, and those are what the task is for.
            std::vector<std::filesystem::path> paths;
            paths.reserve(m_tracksForMix.size());
            for (const auto &track : m_tracksForMix)
            {
                paths.push_back(track.reconstructFullPath());
            }

            auto missingIndices = std::make_shared<std::vector<size_t>>();
            auto *scanTask = new MissingFileScanTask{std::move(paths), missingIndices};

            juce::Component::SafePointer<CreateMixDialogComponent> safeThis{this};
            TaskDialog::launch("Checking Files",
                scanTask,
                TaskDialog::AutoCloseMode::Immediate,
                0,
                this,
                [safeThis, missingIndices](bool success)
                {
                    if (safeThis == nullptr)
                    {
                        return;
                    }

                    auto *self = safeThis.getComponent();
                    if (!success)
                    {
                        // Cancelled part-way, so the list is not a verdict on anything and going ahead
                        // would be a guess about the files we never reached. That is a reason not to
                        // continue, not a reason to throw away the mix: nothing has been written yet, so
                        // leaving this dialog open keeps the typed name and the target selection and lets
                        // the user simply press Create again. Closing the task dialog with its title-bar
                        // button lands here too - by discarding the callback rather than by running it -
                        // so both gestures now end in the same place.
                        spdlog::info("Missing-file check did not complete; returning to Create Mix.");
                        return;
                    }

                    self->onMissingFilesKnown(*missingIndices);
                });

            // Balances the reference ILongRunningTask starts with; TaskDialog took its own in its ctor.
            scanTask->release(REFCOUNT_DEBUG_ARGS);
        }

        void CreateMixDialogComponent::onMissingFilesKnown(const std::vector<size_t> &missingIndices)
        {
            if (missingIndices.empty())
            {
                proceedWithSelectedTarget();
                return;
            }

            spdlog::warn("{} of {} track(s) selected for the mix have no file on disk.", missingIndices.size(), m_tracksForMix.size());

            std::vector<SkippedTracksDialog::Entry> entries;
            entries.reserve(missingIndices.size());
            for (const auto index : missingIndices)
            {
                const auto &track = m_tracksForMix[index];
                entries.push_back(SkippedTracksDialog::Entry{describeTrack(track), track.reconstructFullPath(), "file not found"});
            }

            if (missingIndices.size() == m_tracksForMix.size())
            {
                // Dropping them all would leave nothing to create, and every file being gone points at an
                // offline drive rather than at the tracks. Report and leave the selection alone.
                SkippedTracksDialog::show("Tracks Not Found",
                    juce::String{static_cast<int>(entries.size())} + " track(s) selected for this mix have no file on disk - that is all of them,"
                        " which usually means the drive they live on is not available. No mix was created.",
                    entries,
                    this);
                closeThisDialog(false);
                return;
            }

            const auto remaining = m_tracksForMix.size() - missingIndices.size();
            juce::String summary{juce::String{static_cast<int>(entries.size())} + " of " + juce::String{static_cast<int>(m_tracksForMix.size())}
                + " track(s) have no file on disk. They cannot be mixed or exported."
                  " Continue with the remaining " + juce::String{static_cast<int>(remaining)} + ", or cancel and put the files back first?"};

            // Nothing has been written yet: cancelling here simply creates no mix, so there is nothing
            // to roll back and the tracks stay in the working set untouched.
            juce::Component::SafePointer<CreateMixDialogComponent> safeThis{this};
            const std::vector<size_t> missingCopy{missingIndices};
            SkippedTracksDialog::showConfirm("Tracks Not Found",
                summary,
                entries,
                "Continue Without Them",
                "Cancel",
                this,
                [safeThis, missingCopy](bool confirmed)
                {
                    if (safeThis == nullptr)
                    {
                        return;
                    }

                    auto *self = safeThis.getComponent();
                    if (!confirmed)
                    {
                        spdlog::info("Mix creation cancelled: the user chose to resolve the missing files first.");
                        self->closeThisDialog(false);
                        return;
                    }

                    // Back to front, so each erase leaves the lower indices valid.
                    for (auto it = missingCopy.rbegin(); it != missingCopy.rend(); ++it)
                    {
                        self->m_tracksForMix.erase(self->m_tracksForMix.begin() + static_cast<std::ptrdiff_t>(*it));
                    }
                    spdlog::info("Continuing mix creation with {} track(s) after dropping the missing ones.", self->m_tracksForMix.size());
                    self->proceedWithSelectedTarget();
                });
        }

        void CreateMixDialogComponent::proceedWithSelectedTarget()
        {
            int selectedId = m_mixSelectCombo.getSelectedId();

            if (selectedId == 1)
            {
                // Create new mix. The name was validated in handleCreateMix() before we got here.
                std::string mixNameStd = m_nameEditor.getText().trim().toStdString();
                spdlog::info("Attempting to create auto-mix with name: '{}' from {} tracks.", mixNameStd, m_tracksForMix.size());

                const bool useSmartAutomix = config::theSettings.mixEditingSettings.useSmartAutomix.get();

                // Check if any tracks need energy analysis
                bool needsAnalysis = false;
                if (useSmartAutomix)
                {
                    for (const auto& track : m_tracksForMix)
                    {
                        if (!database::background_tasks::EnergyAnalyzer::hasValidCachedData(track))
                        {
                            needsAnalysis = true;
                            break;
                        }
                    }
                }

                if (needsAnalysis && useSmartAutomix)
                {
                    // Run energy analysis task with progress dialog, then create mix
                    auto* analysisTask = new database::background_tasks::EnergyAnalysisTask(m_tracksForMix);

                    // Capture what we need for the completion callback
                    juce::Component::SafePointer<CreateMixDialogComponent> safeThis = this;
                    std::string capturedMixName = mixNameStd;

                    TaskDialog::launch(
                        "Analyzing Tracks",
                        analysisTask,
                        TaskDialog::AutoCloseMode::Immediate,
                        0,
                        this,
                        [safeThis, capturedMixName](bool success)
                        {
                            // This runs on the message thread after analysis completes (or is cancelled)
                            if (safeThis && success)
                            {
                                safeThis->finishMixCreation(capturedMixName);
                            }
                            else if (safeThis && !success)
                            {
                                spdlog::info("Energy analysis cancelled or failed, mix creation aborted");
                                safeThis->closeThisDialog(false);
                            }
                        });

                    analysisTask->release(REFCOUNT_DEBUG_ARGS);
                }
                else
                {
                    // Smart automix disabled or all tracks already have cached energy data
                    finishMixCreation(mixNameStd);
                }
            }
            else if (selectedId > 1)
            {
                // Append to existing mix
                int mixIndex = selectedId - 2;
                // PVS-Studio: selectedId starts at 2, so selectedId - 2 maps to index 0, so the expression
                // "mixIndex < 0" is always false; removing the check to avoid confusion
                if (/*mixIndex < 0 || */mixIndex >= static_cast<int>(m_availableMixes.size()))
                {
                    spdlog::error("Invalid mix selection index: {}", mixIndex);
                    closeThisDialog(false);
                    return;
                }

                database::MixInfo targetMix = m_availableMixes[mixIndex];
                spdlog::info("Attempting to append {} tracks to existing mix '{}' (ID: {})", m_tracksForMix.size(), targetMix.name, targetMix.mixId);

                const bool useSmartAutomix = config::theSettings.mixEditingSettings.useSmartAutomix.get();
                bool needsAnalysis = false;

                if (useSmartAutomix)
                {
                    auto tracksToCheck = m_tracksForMix;
                    const auto existingTracks = theTrackLibrary.getMixManager().getMixTracks(targetMix.mixId);
                    if (!existingTracks.empty())
                    {
                        if (const auto lastTrackInfo = database::theTrackLibrary.getTrackById(existingTracks.back().trackId))
                        {
                            tracksToCheck.push_back(*lastTrackInfo);
                        }
                    }

                    for (const auto& track : tracksToCheck)
                    {
                        if (!database::background_tasks::EnergyAnalyzer::hasValidCachedData(track))
                        {
                            needsAnalysis = true;
                            break;
                        }
                    }
                }

                if (needsAnalysis && useSmartAutomix)
                {
                    auto tracksToAnalyze = m_tracksForMix;
                    const auto existingTracks = theTrackLibrary.getMixManager().getMixTracks(targetMix.mixId);
                    if (!existingTracks.empty())
                    {
                        if (const auto lastTrackInfo = database::theTrackLibrary.getTrackById(existingTracks.back().trackId))
                        {
                            tracksToAnalyze.push_back(*lastTrackInfo);
                        }
                    }

                    auto* analysisTask = new database::background_tasks::EnergyAnalysisTask(tracksToAnalyze);
                    juce::Component::SafePointer<CreateMixDialogComponent> safeThis = this;
                    database::MixInfo capturedTargetMix = targetMix;
                    TaskDialog::launch(
                        "Analyzing Tracks",
                        analysisTask,
                        TaskDialog::AutoCloseMode::Immediate,
                        0,
                        this,
                        [safeThis, capturedTargetMix](bool success)
                        {
                            if (safeThis && success)
                            {
                                safeThis->finishAppendToMix(capturedTargetMix);
                            }
                            else if (safeThis && !success)
                            {
                                spdlog::info("Energy analysis cancelled or failed, append aborted");
                                safeThis->closeThisDialog(false);
                            }
                        });

                    analysisTask->release(REFCOUNT_DEBUG_ARGS);
                }
                else
                {
                    finishAppendToMix(targetMix);
                }
            }
        }

        void CreateMixDialogComponent::finishAppendToMix(const database::MixInfo& targetMix)
        {
            auto mixToUpdate = targetMix;

            // readMixTracks, not getMixTracks: what comes back is appended to and then written
            // back through createOrUpdateMix, which replaces the whole row set. An empty vector
            // from a failed read would turn an append into a mix containing only the new tracks.
            std::vector<database::MixTrack> existingTracks;
            if (const auto read = theTrackLibrary.getMixManager().readMixTracks(targetMix.mixId, existingTracks); !read.isOk())
            {
                spdlog::error("Cannot append to mix '{}': {}", targetMix.name, read.errorMessage);
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "Append Failed",
                    "Could not read the existing mix, so nothing was appended. Check the logs for details.");
                closeThisDialog(false);
                return;
            }
            const Duration_t defaultCrossfade{5000};
            int nextOrder = static_cast<int>(existingTracks.size());
            const bool useSmartAutomix = config::theSettings.mixEditingSettings.useSmartAutomix.get();
            const bool linkEnvelopePoints = config::theSettings.mixEditingSettings.linkEnvelopePointsToAttachPoints.get();
            using database::background_tasks::EnergyAnalyzer;
            using database::background_tasks::TransitionCalculator;

            std::optional<database::TrackInfo> prevTrackInfoOpt;
            std::optional<database::background_tasks::EnergyAnalysisResult> prevEnergyOpt;
            if (useSmartAutomix && !existingTracks.empty())
            {
                prevTrackInfoOpt = database::theTrackLibrary.getTrackById(existingTracks.back().trackId);
                if (prevTrackInfoOpt)
                {
                    prevEnergyOpt = EnergyAnalyzer::getCachedData(*prevTrackInfoOpt);
                }
            }

            for (const auto &trackInfo : m_tracksForMix)
            {
                database::MixTrack newMixTrack{};
                newMixTrack.trackId = trackInfo.trackId;
                newMixTrack.orderInMix = nextOrder++;
                newMixTrack.cueStart = Duration_t{0};
                newMixTrack.cueEnd = Duration_t{0};

                const auto crossfade = database::calculateCrossfadeForTrack(trackInfo.duration, defaultCrossfade);
                Duration_t crossfadeDuration = crossfade.effectiveCrossfade;

                if (useSmartAutomix && !existingTracks.empty() && prevTrackInfoOpt.has_value())
                {
                    const auto currentEnergyOpt = EnergyAnalyzer::getCachedData(trackInfo);
                    if (prevEnergyOpt && prevEnergyOpt->isValid && currentEnergyOpt && currentEnergyOpt->isValid)
                    {
                        auto transition = TransitionCalculator::calculate(
                            *prevEnergyOpt,
                            prevTrackInfoOpt->duration,
                            *currentEnergyOpt,
                            trackInfo.duration);

                        auto &prevMixTrack = existingTracks.back();
                        const auto oldPrevAttachTo = prevMixTrack.attachTo;
                        auto newPrevAttachTo = std::max(Duration_t{0}, std::min(transition.attachToA, prevTrackInfoOpt->duration));
                        if (newPrevAttachTo <= prevMixTrack.attachFrom)
                        {
                            newPrevAttachTo = prevTrackInfoOpt->duration;
                        }

                        if (newPrevAttachTo != oldPrevAttachTo)
                        {
                            if (linkEnvelopePoints)
                            {
                                prevMixTrack.scaleEnvelopePointsForAttachChange(
                                    prevMixTrack.attachFrom,
                                    prevMixTrack.attachFrom,
                                    oldPrevAttachTo,
                                    newPrevAttachTo,
                                    prevTrackInfoOpt->duration);
                            }
                            prevMixTrack.attachTo = newPrevAttachTo;
                        }

                        newMixTrack.attachFrom = std::max(Duration_t{0}, std::min(transition.attachFromB, trackInfo.duration));
                        newMixTrack.attachTo = trackInfo.duration;
                        crossfadeDuration = transition.crossfadeDuration;
                    }
                    else
                    {
                        // Fallback when smart data is unavailable for append boundary
                        newMixTrack.attachFrom = crossfade.attachFrom;
                        newMixTrack.attachTo = crossfade.attachTo;
                        newMixTrack.envelopePoints = crossfade.envelopePoints;
                    }
                    prevEnergyOpt = currentEnergyOpt;
                }
                else
                {
                    if (!existingTracks.empty())
                    {
                        newMixTrack.attachFrom = crossfade.attachFrom;
                    }
                    newMixTrack.attachTo = crossfade.attachTo;
                    newMixTrack.envelopePoints = crossfade.envelopePoints;
                }

                if (newMixTrack.envelopePoints.empty())
                {
                    const auto effectiveFadeIn = std::min(crossfadeDuration, newMixTrack.attachFrom);
                    const auto effectiveFadeOut = std::min(crossfadeDuration, trackInfo.duration - newMixTrack.attachTo);
                    if (effectiveFadeIn == Duration_t{0} && effectiveFadeOut == Duration_t{0})
                    {
                        newMixTrack.envelopePoints = {
                            {Duration_t{0}, VOLUME_NORMALIZATION},
                            {trackInfo.duration, VOLUME_NORMALIZATION}};
                    }
                    else
                    {
                        const auto fadeInMidpoint = std::min(Duration_t{2000}, effectiveFadeIn / 2);
                        const auto fadeOutStart = newMixTrack.attachTo;
                        const auto fadeOutMidpoint = trackInfo.duration - std::min(Duration_t{2000}, effectiveFadeOut / 2);
                        newMixTrack.envelopePoints = {
                            {Duration_t{0}, Volume_t{200}},
                            {fadeInMidpoint, Volume_t{700}},
                            {newMixTrack.attachFrom, VOLUME_NORMALIZATION},
                            {fadeOutStart, VOLUME_NORMALIZATION},
                            {fadeOutMidpoint, Volume_t{700}},
                            {trackInfo.duration, Volume_t{200}}};
                    }
                }

                existingTracks.push_back(newMixTrack);
                prevTrackInfoOpt = trackInfo;
                if (useSmartAutomix)
                {
                    prevEnergyOpt = EnergyAnalyzer::getCachedData(trackInfo);
                }
            }

            bool success = theTrackLibrary.getMixManager().createOrUpdateMix(mixToUpdate, existingTracks);
            if (!success)
            {
                spdlog::error("Failed to append tracks to mix '{}'", targetMix.name);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon, "Append Failed", "Could not append tracks to the existing mix. Check logs for details.");
                closeThisDialog(false);
                return;
            }

            if (m_onOkCallback)
            {
                m_onOkCallback(true, mixToUpdate);
                m_onOkCallback = nullptr;
            }
            spdlog::info("Successfully appended {} tracks to mix '{}' (ID: {})", m_tracksForMix.size(), targetMix.name, targetMix.mixId);
            closeThisDialog(true);
        }

        void CreateMixDialogComponent::closeThisDialog(bool success)
        {
            if (!success && (m_onOkCallback != nullptr))
            {
                database::MixInfo invalidMixInfo;
                m_onOkCallback(false, invalidMixInfo); // success=false, invalid mixId, empty path
                m_onOkCallback = nullptr;
            }
            if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                spdlog::info("CreateMixDialogComponent: Closing dialog with modal state: {}", success);
                dw->exitModalState(success ? 1 : 0); // 1 for success, 0 for failure/cancel
            }
            else
            {
                spdlog::warn("CreateMixDialogComponent: No parent DialogWindow found to close.");
            }
        }

        void CreateMixDialogComponent::handleCancel()
        {
            spdlog::debug("CreateMixDialogComponent cancelled by user.");
            closeThisDialog(false);
        }

        void CreateMixDialogComponent::comboBoxChanged(juce::ComboBox *comboBox)
        {
            if (comboBox == &m_mixSelectCombo)
            {
                int selectedId = m_mixSelectCombo.getSelectedId();

                if (selectedId == 1)
                {
                    // "Create New Mix" selected - enable name editor
                    m_nameEditor.setEnabled(true);
                    m_nameEditor.setText(generateDefaultMixName(), false);
                    m_nameEditor.selectAll();
                    m_okButton.setButtonText("Create");

                    // Set focus to name editor
                    m_nameEditor.grabKeyboardFocus();
                }
                else if (selectedId > 1)
                {
                    // Existing mix selected - disable name editor and show mix name
                    int mixIndex = selectedId - 2;
                    // PVS-Studio: selectedId starts at 2, so selectedId - 2 maps to index 0, so the expression
                    // "mixIndex >= 0" is always true; removing the check to avoid confusion
                    if (/*mixIndex >= 0 && */mixIndex < static_cast<int>(m_availableMixes.size()))
                    {
                        m_nameEditor.setText(m_availableMixes[mixIndex].name, false);
                        m_nameEditor.setEnabled(false);
                        m_okButton.setButtonText("Append");
                    }
                }
            }
        }

        juce::String CreateMixDialogComponent::generateDefaultMixName()
        {
            // Get the NEXT mix number for this working set (WITHOUT incrementing)
            // We'll increment it only when the mix is actually created
            int mixNumber = 0;
            if (m_source_ws_id > 0)
            {
                auto& trackDb = database::theTrackLibrary.getTrackDatabase();
                auto& wsManager = trackDb.getWorkingSetManager();
                mixNumber = wsManager.getNextMixNumber(m_source_ws_id);
            }

            auto now = std::time(nullptr);
#ifdef _MSC_VER // Use localtime_s on Windows
            std::tm tm_s;
            localtime_s(&tm_s, &now);
            auto tm = tm_s;
#else // Use localtime on other platforms
            auto tm = *std::localtime(&now);
#endif

            std::ostringstream oss;
            oss.imbue(std::locale::classic()); // Disable locale-specific formatting (no thousands separators)
            if (mixNumber > 0)
            {
                oss << std::setw(4) << std::setfill('0') << mixNumber << " - Automix "
                    << std::put_time(&tm, "%Y-%m-%d %H-%M-%S");
            }
            else
            {
                // Fallback if we couldn't get the number
                oss << "Auto-Mix " << std::put_time(&tm, "%Y-%m-%d %H-%M-%S");
            }
            return juce::String(oss.str());
        }

        void CreateMixDialogComponent::finishMixCreation(const std::string& mixNameStd)
        {
            database::MixInfo newMixInfo{};
            newMixInfo.name = mixNameStd;
            std::vector<database::MixTrack> resultingMixTracks;

            // Re-fetch track info from database to get updated energy data
            // (EnergyAnalysisTask stored intro_end, outro_start, beat_locations_json in DB)
            std::vector<database::TrackInfo> freshTrackInfos;
            freshTrackInfos.reserve(m_tracksForMix.size());
            for (const auto& track : m_tracksForMix)
            {
                auto freshInfo = database::theTrackLibrary.getTrackById(track.trackId);
                if (freshInfo)
                {
                    freshTrackInfos.push_back(*freshInfo);
                }
                else
                {
                    // Fallback to original if re-fetch fails (shouldn't happen)
                    spdlog::warn("Failed to re-fetch track {}, using original info", track.trackId);
                    freshTrackInfos.push_back(track);
                }
            }

            bool mixDefined =
                ::jucyaudio::database::theTrackLibrary.getMixManager().createAndSaveAutoMix(freshTrackInfos, newMixInfo, resultingMixTracks, m_source_ws_id);

            if (!mixDefined || newMixInfo.mixId == -1)
            {
                spdlog::error("Failed to define mix '{}' in the database.", mixNameStd);
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "Mix Creation Failed",
                    "Could not save the mix definition to the database. Check logs for details.");
                closeThisDialog(false);
                return;
            }

            // Now that the mix is successfully created, increment the mix counter
            if (m_source_ws_id > 0)
            {
                auto& trackDb = database::theTrackLibrary.getTrackDatabase();
                trackDb.getWorkingSetManager().incrementMixNumber(m_source_ws_id);
            }

            if (m_onOkCallback)
            {
                m_onOkCallback(true, newMixInfo);
                m_onOkCallback = nullptr; // Clear callback after use
            }
            spdlog::info(
                "Mix '{}' (ID: {}) defined successfully in database with {} tracks.", newMixInfo.name, newMixInfo.mixId, resultingMixTracks.size());

            closeThisDialog(true);
        }

    } // namespace ui
} // namespace jucyaudio
