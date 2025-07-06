#include <Database/Includes/ILongRunningTask.h>
#include <UI/CreateMixDialogComponent.h>
#include <UI/TaskDialog.h>
#include <Utils/AssortedUtils.h> // For pathToString, durationToString if needed for logging
#include <ctime>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <sstream>
#include <UI/MainComponent.h>

// Forward declare if TrackLibrary provides these directly, or include necessary headers
// Assuming TrackLibrary provides access to IMixManager and IMixExporter

namespace jucyaudio
{
    namespace ui
    {
        CreateMixDialogComponent::CreateMixDialogComponent(audio::AudioLibrary &audioLibrary, database::TrackLibrary &trackLibrary,
                                                           const std::vector<database::TrackInfo> &tracksForMix, OnMixCreatedAndExportedCallback onOkCallback)
            : m_audioLibrary{audioLibrary},
              m_trackLibrary{trackLibrary},
              m_tracksForMix{tracksForMix}, // Store reference
              m_onOkCallback{std::move(onOkCallback)},
              m_titleLabel{"titleLabel", "Create Auto-Mix"},
              m_countLabel{"countLabel", ""},
              m_nameLabel{"nameLabel", "Mix Name:"},
              m_nameEditor{"nameEditor"},
              m_okButton{"Create & Export"},
              m_cancelButton{"Cancel"}
        {
            m_lookAndFeel.setColourScheme (getColourSchemeFromConfig());
            setLookAndFeel(&m_lookAndFeel); // Set custom LookAndFeel
            setSize(450, 220); // Slightly wider for "Create & Export"

            // Title label
            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(24.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);

            // Count label
            addAndMakeVisible(m_countLabel);
            juce::String countText = "Create an auto-mix from these " + juce::String(m_tracksForMix.size()) + " tracks?";
            m_countLabel.setText(countText, juce::dontSendNotification);
            m_countLabel.setJustificationType(juce::Justification::centred);

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
                handleCreateAndExport();
            }
            else if (button == &m_cancelButton)
            {
                handleCancel();
            }
        }

        void CreateMixDialogComponent::textEditorReturnKeyPressed(juce::TextEditor & /*editor*/)
        {
            handleCreateAndExport();
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

        void CreateMixDialogComponent::handleCreateAndExport()
        {
            juce::String mixNameJuce = m_nameEditor.getText().trim();
            if (mixNameJuce.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Invalid Mix Name", "Please enter a name for the mix.");
                m_nameEditor.grabKeyboardFocus();
                return;
            }

            std::string mixNameStd = mixNameJuce.toStdString();
            spdlog::info("Attempting to create auto-mix with name: '{}' from {} tracks.", mixNameStd, m_tracksForMix.size());

            if (m_tracksForMix.empty())
            {
                spdlog::warn("No tracks provided to create mix.");
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "No Tracks", "Cannot create a mix with zero tracks.");
                closeThisDialog(false);
                return;
            }

            database::MixInfo newMixInfo{};
            newMixInfo.name = mixNameStd;
            std::vector<database::MixTrack> resultingMixTracks;
            // Using the default crossfade duration from IMixManager's createAndSaveAutoMix
            // Or pass a specific one: database::Duration_t crossfadeDuration{5000};
            bool mixDefined =
                m_trackLibrary.getMixManager().createAndSaveAutoMix(m_tracksForMix, newMixInfo, resultingMixTracks /*, crossfadeDuration (optional) */);

            if (!mixDefined || newMixInfo.mixId == -1)
            {
                spdlog::error("Failed to define mix '{}' in the database.", mixNameStd);
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Mix Creation Failed",
                                                       "Could not save the mix definition to the database. Check logs for details.");
                closeThisDialog(false);
                return;
            }
            if (m_onOkCallback)
            {
                m_onOkCallback(true, newMixInfo.mixId);
                m_onOkCallback = nullptr; // Clear callback after use
            }
            spdlog::info("Mix '{}' (ID: {}) defined successfully in database with {} tracks.", newMixInfo.name, newMixInfo.mixId, resultingMixTracks.size());

            // Now proceed to export
            launchExportFileChooserAndProcess(newMixInfo.mixId, mixNameJuce);
            // The actual callback and dialog closing will happen after the file chooser and export attempt.
        }

        void CreateMixDialogComponent::launchExportFileChooserAndProcess(MixId mixId, const juce::String &mixName)
        {
            // Store the FileChooser in a member unique_ptr to keep it alive
            m_activeFileChooser = std::make_unique<juce::FileChooser>("Export Mix As...", juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                                      "*.mp3;*.wav", true, false, this);

            int chooserFlags =
                juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;

            // Use std::bind (or a small capturing lambda) to call our member function
            // We need to pass mixId and mixName to the callback.
            m_activeFileChooser->launchAsync(chooserFlags,
                                             // This lambda is small and just forwards to the member function, passing captured state.
                                             // It is copyable.
                                             [this, mixId, mixName](const juce::FileChooser &chooser)
                                             {
                                                 this->onFileChooserModalDismissed(chooser, mixId, mixName);
                                             });
        }

        class CreateMixTask final : public database::ILongRunningTask
        {
        public:
            CreateMixTask(MixId mixId, const audio::IMixExporter &mixExporter, database::TrackLibrary &trackLibrary,
                          const std::filesystem::path &targetExportPath)
                : database::ILongRunningTask{"Creating Mix", false},
                  m_mixId{mixId},
                  m_targetExportPath{targetExportPath},
                  m_mixExporter{mixExporter},
                  m_trackLibrary{trackLibrary}
            {
            }

            void run(database::ProgressCallback progressCb, database::CompletionCallback completionCb, [[maybe_unused]] std::atomic<bool> &shouldCancel) override
            {
                auto exportProgressCallback = [progressCb](float progress, const std::string &statusMsg)
                {
                    static int lastProgress = -1;
                    int ni = static_cast<int>(progress * 100);
                    if (ni != lastProgress)
                    {
                        spdlog::info("Export progress: {}% - {}", ni, statusMsg);
                        progressCb(ni, statusMsg);
                        lastProgress = ni;
                    }
                };
                m_bExported = m_mixExporter.exportMixToFile(m_mixId, m_trackLibrary, m_targetExportPath, exportProgressCallback);
                completionCb(m_bExported, "Create mix task completed");
            }

            bool m_bExported{false};

        private:
            MixId m_mixId;
            std::filesystem::path m_targetExportPath;
            const audio::IMixExporter &m_mixExporter;
            database::TrackLibrary &m_trackLibrary;
        };

        // *** New member function to handle the FileChooser callback ***
        void CreateMixDialogComponent::onFileChooserModalDismissed(const juce::FileChooser &chooser, MixId mixId, const juce::String &mixName)
        {
            const juce::File chosenFile = chooser.getResult();

            // Release the FileChooser now that its job is done
            m_activeFileChooser.reset();

            if (chosenFile == juce::File{}) // User cancelled
            {
                closeThisDialog(false);
                return;
            }

            std::filesystem::path targetExportPath = chosenFile.getFullPathName().toStdString();
            spdlog::info("Exporting mix ID: {} (Name: '{}') to: {}", mixId, mixName.toStdString(), pathToString(targetExportPath));

            auto *task = new CreateMixTask(mixId, m_audioLibrary.getMixExporter(), m_trackLibrary, targetExportPath);
            TaskDialog::launch("Mix Creation In Progress", task, 500, this);
            task->release(REFCOUNT_DEBUG_ARGS);
            closeThisDialog(true);
        }

        void CreateMixDialogComponent::closeThisDialog(bool success)
        {
            if (!success & (m_onOkCallback != nullptr))
            {
                m_onOkCallback(false, -1); // success=false, invalid mixId, empty path
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
            oss << "Auto-Mix " << std::put_time(&tm, "%Y-%m-%d %H-%M"); // Added time for more uniqueness
            return juce::String(oss.str());
        }

    } // namespace ui
} // namespace jucyaudio
