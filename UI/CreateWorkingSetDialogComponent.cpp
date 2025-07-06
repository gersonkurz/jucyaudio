// CreateWorkingSetDialogComponent.cpp
#include <UI/CreateWorkingSetDialogComponent.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <UI/MainComponent.h>

namespace jucyaudio
{
    namespace ui
    {
        CreateWorkingSetDialogComponent::CreateWorkingSetDialogComponent(int64_t trackCount, std::function<void(const juce::String &)> onOkCallback)
            : m_trackCount{trackCount},
              m_onOkCallback{std::move(onOkCallback)},
              m_titleLabel{"titleLabel", "Create Working Set"},
              m_countLabel{"countLabel", ""},
              m_nameLabel{"nameLabel", "Name:"},
              m_nameEditor{"nameEditor"},
              m_okButton{"OK"},
              m_cancelButton{"Cancel"}
        {
            m_lookAndFeel.setColourScheme (getColourSchemeFromConfig());
            setLookAndFeel(&m_lookAndFeel); // Set custom LookAndFeel
            setSize(400, 200);

            // Title label
            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(24.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);

            // Count label
            addAndMakeVisible(m_countLabel);
            juce::String countText = "Create working set from these " + juce::String(m_trackCount) + " tracks?";
            m_countLabel.setText(countText, juce::dontSendNotification);
            m_countLabel.setJustificationType(juce::Justification::centred);

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

        void CreateWorkingSetDialogComponent::handleOk()
        {
            juce::String name = m_nameEditor.getText().trim();
            if (name.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Invalid Name", "Please enter a name for the working set.");
                m_nameEditor.grabKeyboardFocus();
                return;
            }

            // Call the callback with the entered name
            if (m_onOkCallback)
            {
                m_onOkCallback(name);
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
