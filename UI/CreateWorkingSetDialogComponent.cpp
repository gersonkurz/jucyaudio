// CreateWorkingSetDialogComponent.cpp
#include <UI/CreateWorkingSetDialogComponent.h>
#include <Database/TrackLibrary.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <spdlog/spdlog.h>
#include <UI/MainComponent.h>

namespace jucyaudio
{
    namespace ui
    {
        CreateWorkingSetDialogComponent::CreateWorkingSetDialogComponent(int64_t trackCount, OnCreateWorkingSetCallback onOkCallback)
            : m_trackCount{trackCount},
              m_onOkCallback{std::move(onOkCallback)},
              m_titleLabel{"titleLabel", "Create Working Set"},
              m_countLabel{"countLabel", ""},
              m_wsSelectLabel{"wsSelectLabel", "Target:"},
              m_wsSelectCombo{"wsSelectCombo"},
              m_nameLabel{"nameLabel", "Name:"},
              m_nameEditor{"nameEditor"},
              m_okButton{"OK"},
              m_cancelButton{"Cancel"}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);
            setSize(400, 240);  // Increased height for the combo box

            // Title label
            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(24.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);

            // Count label
            addAndMakeVisible(m_countLabel);
            const juce::String countText = std::format("Create working set from {:L} tracks?", m_trackCount);
            m_countLabel.setText(countText, juce::dontSendNotification);
            m_countLabel.setJustificationType(juce::Justification::centred);
            
            // Working set selection combo box
            addAndMakeVisible(m_wsSelectLabel);
            addAndMakeVisible(m_wsSelectCombo);
            m_wsSelectCombo.addListener(this);
            
            // Load existing working sets
            m_availableWorkingSets = database::theTrackLibrary.getWorkingSetManager().getWorkingSets({});
            
            // Add empty option for "Create New Working Set"
            m_wsSelectCombo.addItem("<Create New Working Set>", 1);
            m_wsSelectCombo.addSeparator();
            
            // Add existing working sets
            for (size_t i = 0; i < m_availableWorkingSets.size(); ++i)
            {
                m_wsSelectCombo.addItem(m_availableWorkingSets[i].name, static_cast<int>(i + 2));
            }
            
            // Select "Create New Working Set" by default
            m_wsSelectCombo.setSelectedId(1);

            // Name label and editor
            addAndMakeVisible(m_nameLabel);
            addAndMakeVisible(m_nameEditor);
            m_nameEditor.setText(generateDefaultName(), false);
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

        CreateWorkingSetDialogComponent::~CreateWorkingSetDialogComponent()
        {
            setLookAndFeel(nullptr);
            // Listeners are automatically removed by JUCE
        }

        void CreateWorkingSetDialogComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void CreateWorkingSetDialogComponent::resized()
        {
            juce::Rectangle<int> area = getLocalBounds().reduced(20);

            // Title
            m_titleLabel.setBounds(area.removeFromTop(30));
            area.removeFromTop(10); // spacing

            // Count message
            m_countLabel.setBounds(area.removeFromTop(25));
            area.removeFromTop(15); // spacing
            
            // Working set selection row
            auto wsSelectRow = area.removeFromTop(25);
            m_wsSelectLabel.setBounds(wsSelectRow.removeFromLeft(60));
            wsSelectRow.removeFromLeft(10); // spacing
            m_wsSelectCombo.setBounds(wsSelectRow);
            
            area.removeFromTop(10); // spacing

            // Name input row
            auto nameRow = area.removeFromTop(25);
            m_nameLabel.setBounds(nameRow.removeFromLeft(60));
            nameRow.removeFromLeft(10); // spacing
            m_nameEditor.setBounds(nameRow);

            area.removeFromTop(20); // spacing before buttons

            // Buttons at bottom
            auto buttonRow = area.removeFromBottom(30);
            int buttonWidth = 80;
            int buttonSpacing = 10;

            m_cancelButton.setBounds(buttonRow.removeFromRight(buttonWidth));
            buttonRow.removeFromRight(buttonSpacing);
            m_okButton.setBounds(buttonRow.removeFromRight(buttonWidth));
        }

        void CreateWorkingSetDialogComponent::buttonClicked(juce::Button *button)
        {
            if (button == &m_okButton)
            {
                handleOk();
            }
            else if (button == &m_cancelButton)
            {
                handleCancel();
            }
        }

        void CreateWorkingSetDialogComponent::textEditorReturnKeyPressed(juce::TextEditor & /*editor*/)
        {
            handleOk();
        }

        bool CreateWorkingSetDialogComponent::keyPressed(const juce::KeyPress &key)
        {
            if (key == juce::KeyPress::escapeKey)
            {
                handleCancel();
                return true;
            }
            return juce::Component::keyPressed(key);
        }
        
        void CreateWorkingSetDialogComponent::comboBoxChanged(juce::ComboBox *comboBox)
        {
            if (comboBox == &m_wsSelectCombo)
            {
                int selectedId = m_wsSelectCombo.getSelectedId();
                
                if (selectedId == 1)
                {
                    // "Create New Working Set" selected - enable name editor
                    m_nameEditor.setEnabled(true);
                    m_nameLabel.setText("Name:", juce::dontSendNotification);
                    m_okButton.setButtonText("OK");
                    
                    // Restore default name
                    m_nameEditor.setText(generateDefaultName(), false);
                    m_nameEditor.selectAll();
                }
                else if (selectedId > 1)
                {
                    // Existing working set selected - disable name editor
                    m_nameEditor.setEnabled(false);
                    m_nameLabel.setText("Append to:", juce::dontSendNotification);
                    m_okButton.setButtonText("Append");
                    
                    // Show selected working set name in the disabled editor
                    int wsIndex = selectedId - 2;
                    if (wsIndex >= 0 && wsIndex < static_cast<int>(m_availableWorkingSets.size()))
                    {
                        m_nameEditor.setText(m_availableWorkingSets[wsIndex].name, false);
                    }
                }
            }
        }

        void CreateWorkingSetDialogComponent::handleOk()
        {
            int selectedId = m_wsSelectCombo.getSelectedId();
            
            if (selectedId == 1)
            {
                // Create new working set
                juce::String name = m_nameEditor.getText().trim();
                if (name.isEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, 
                        "Invalid Name", "Please enter a name for the working set.");
                    m_nameEditor.grabKeyboardFocus();
                    return;
                }

                // Call the callback with the entered name and -1 for new working set
                if (m_onOkCallback)
                {
                    m_onOkCallback(name, -1);
                }
            }
            else if (selectedId > 1)
            {
                // Append to existing working set
                int wsIndex = selectedId - 2;
                if (wsIndex >= 0 && wsIndex < static_cast<int>(m_availableWorkingSets.size()))
                {
                    WorkingSetId targetWsId = m_availableWorkingSets[wsIndex].id;
                    juce::String wsName = m_availableWorkingSets[wsIndex].name;
                    
                    spdlog::info("Appending {} tracks to existing working set '{}' (ID: {})", 
                                m_trackCount, wsName.toStdString(), targetWsId);
                    
                    // Call the callback with the existing name and ID
                    if (m_onOkCallback)
                    {
                        m_onOkCallback(wsName, targetWsId);
                    }
                }
                else
                {
                    spdlog::error("Invalid working set selection index: {}", wsIndex);
                    return;
                }
            }

            // Close the dialog
            if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                dw->exitModalState(1); // Return code 1 for OK
            }
        }

        void CreateWorkingSetDialogComponent::handleCancel()
        {
            if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                dw->exitModalState(0); // Return code 0 for Cancel
            }
        }

        juce::String CreateWorkingSetDialogComponent::generateDefaultName()
        {
            // Generate name like "Working Set 2024-12-30"
            auto now = std::time(nullptr);
            auto tm = *std::localtime(&now);

            std::ostringstream oss;
            oss << "Working Set " << std::put_time(&tm, "%Y-%m-%d");

            return juce::String(oss.str());
        }

    } // namespace ui
} // namespace jucyaudio
