#include <Database/Includes/ILongRunningTask.h>
#include <UI/CreateMixDialogComponent.h>
#include <UI/MainComponent.h>
#include <UI/TaskDialog.h>
#include <Utils/AssortedUtils.h> // For pathToString, durationToString if needed for logging
#include <ctime>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <sstream>

// Forward declare if TrackLibrary provides these directly, or include necessary headers
// Assuming TrackLibrary provides access to IMixManager and IMixExporter

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
            juce::MessageManager::callAsync(
                [this]()
                {
                    if (isShowing())
                    {
                        m_nameEditor.grabKeyboardFocus();
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

            int selectedId = m_mixSelectCombo.getSelectedId();

            if (selectedId == 1)
            {
                // Create new mix
                juce::String mixNameJuce = m_nameEditor.getText().trim();
                if (mixNameJuce.isEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Invalid Mix Name", "Please enter a name for the mix.");
                    m_nameEditor.grabKeyboardFocus();
                    return;
                }

                std::string mixNameStd = mixNameJuce.toStdString();
                spdlog::info("Attempting to create auto-mix with name: '{}' from {} tracks.", mixNameStd, m_tracksForMix.size());

                database::MixInfo newMixInfo{};
                newMixInfo.name = mixNameStd;
                std::vector<database::MixTrack> resultingMixTracks;

                bool mixDefined =
                    ::jucyaudio::database::theTrackLibrary.getMixManager().createAndSaveAutoMix(m_tracksForMix, newMixInfo, resultingMixTracks, m_source_ws_id);

                if (!mixDefined || newMixInfo.mixId == -1)
                {
                    spdlog::error("Failed to define mix '{}' in the database.", mixNameStd);
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                        "Mix Creation Failed",
                        "Could not save the mix definition to the database. Check logs for details.");
                    closeThisDialog(false);
                    return;
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
            else if (selectedId > 1)
            {
                // Append to existing mix
                int mixIndex = selectedId - 2;
                if (mixIndex < 0 || mixIndex >= static_cast<int>(m_availableMixes.size()))
                {
                    spdlog::error("Invalid mix selection index: {}", mixIndex);
                    closeThisDialog(false);
                    return;
                }

                database::MixInfo targetMix = m_availableMixes[mixIndex];
                spdlog::info("Attempting to append {} tracks to existing mix '{}' (ID: {})", m_tracksForMix.size(), targetMix.name, targetMix.mixId);

                // Get existing mix tracks
                auto existingTracks = theTrackLibrary.getMixManager().getMixTracks(targetMix.mixId);

                // Add new tracks with appropriate crossfade settings
                const Duration_t defaultCrossfade{5000}; // 5 seconds
                int nextOrder = static_cast<int>(existingTracks.size());

                for (const auto &trackInfo : m_tracksForMix)
                {
                    database::MixTrack newMixTrack{};
                    newMixTrack.trackId = trackInfo.trackId;
                    newMixTrack.orderInMix = nextOrder++;

                    // Set up crossfade from previous track
                    if (!existingTracks.empty())
                    {
                        newMixTrack.attachFrom = defaultCrossfade;
                    }
                    newMixTrack.attachTo = trackInfo.duration - defaultCrossfade;

                    // CUE points - use full track (default behavior)
                    newMixTrack.cueStart = Duration_t{0};
                    newMixTrack.cueEnd = Duration_t{0}; // 0 means use full track duration

                    // Create envelope points for crossfade, mirroring the automix logic
                    const auto fadeInMidpoint = Duration_t{2000};                       // 2 seconds
                    const auto fadeOutMidpoint = trackInfo.duration - Duration_t{2000}; // 2 seconds before end

                    newMixTrack.envelopePoints = {{Duration_t{0}, Volume_t{200}},
                        {fadeInMidpoint, Volume_t{700}},
                        {defaultCrossfade, database::VOLUME_NORMALIZATION},
                        {trackInfo.duration - defaultCrossfade, database::VOLUME_NORMALIZATION},
                        {fadeOutMidpoint, Volume_t{700}},
                        {trackInfo.duration, Volume_t{200}}};

                    existingTracks.push_back(newMixTrack);
                }

                // Update the mix with all tracks
                bool success = theTrackLibrary.getMixManager().createOrUpdateMix(targetMix, existingTracks);

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
                    m_onOkCallback(true, targetMix);
                    m_onOkCallback = nullptr;
                }
                spdlog::info("Successfully appended {} tracks to mix '{}' (ID: {})", m_tracksForMix.size(), targetMix.name, targetMix.mixId);
                closeThisDialog(true);
            }
        }

        void CreateMixDialogComponent::closeThisDialog(bool success)
        {
            if (!success & (m_onOkCallback != nullptr))
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
                    if (mixIndex >= 0 && mixIndex < static_cast<int>(m_availableMixes.size()))
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
            auto now = std::time(nullptr);
#ifdef _MSC_VER // Use localtime_s on Windows
            std::tm tm_s;
            localtime_s(&tm_s, &now);
            auto tm = tm_s;
#else // Use localtime on other platforms
            auto tm = *std::localtime(&now);
#endif

            std::ostringstream oss;
            oss << "Auto-Mix " << std::put_time(&tm, "%Y-%m-%d %H-%M-%S"); // Added seconds for uniqueness
            return juce::String(oss.str());
        }

    } // namespace ui
} // namespace jucyaudio
