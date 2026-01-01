#include <UI/DynamicToolbarComponent.h>
#include <UI/MainComponent.h>
#include <Utils/UiUtils.h>
#include <BinaryData.h>

namespace jucyaudio
{
    namespace ui
    {

        DynamicToolbarComponent::DynamicToolbarComponent()
        {
            // Configure Filter Label
            m_filterLabel.setText("Filter:", juce::dontSendNotification);
            m_filterLabel.setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(m_filterLabel);

            // Configure Filter Text Editor
            m_filterTextEditor.setMultiLine(false);
            m_filterTextEditor.setReturnKeyStartsNewLine(false);
            m_filterTextEditor.addListener(this); // Listen for text changes, return key, focus loss
            m_filterTextEditor.setTextToShowWhenEmpty("Search or filter (e.g., year:1991, bpm:>120)", juce::Colours::grey);
            addAndMakeVisible(m_filterTextEditor);

            // Create all possible action buttons upfront (for consistent UI)
            const DataAction allActions[] = {
                DataAction::Play,
                DataAction::CreateWorkingSet,
                DataAction::CreateMix,
                DataAction::ShowDetails,
                DataAction::EditWorkingSetMetadata,
                DataAction::EditMixMetadata,
                DataAction::RemoveTracks,
                DataAction::Delete,
                DataAction::ExportMix,
                DataAction::RunBpmAnalysis,
                DataAction::ShowMixEditor,
                DataAction::ShowTrackEditor,
                DataAction::ShowInFolder,
                DataAction::RemoveDuplicates
            };
            
            // Create special always-visible buttons (Settings, ScanFolders, ShowEqualizer, ShowReverb)
            const DataAction alwaysVisibleActions[] = {
                DataAction::Settings,
                DataAction::ScanFolders,
                DataAction::ShowEqualizer,
                DataAction::ShowReverb
            };

            for (const auto action : allActions)
            {
                auto icon = dataActionToIcon(action, &getLookAndFeel());
                if (icon)
                {
                    auto button = std::make_unique<juce::DrawableButton>(
                        dataActionToString(action, nullptr),
                        juce::DrawableButton::ImageFitted
                    );
                    
                    button->setImages(icon.get());
                    button->setTooltip(dataActionToString(action, nullptr));
                    button->setEnabled(false); // Start disabled
                    
                    // Capture action by value for the lambda
                    button->onClick = [this, action]
                    {
                        handleActionButtonClicked(action);
                    };
                    
                    addAndMakeVisible(button.get());
                    m_allActionButtons.emplace_back(ActionButtonInfo{action, std::move(button)});
                }
            }
            
            // Create the always-visible buttons (Settings, ScanFolders)
            for (const auto action : alwaysVisibleActions)
            {
                auto icon = dataActionToIcon(action, &getLookAndFeel());
                if (icon)
                {
                    auto button = std::make_unique<juce::DrawableButton>(
                        dataActionToString(action, nullptr),
                        juce::DrawableButton::ImageFitted
                    );
                    
                    button->setImages(icon.get());
                    button->setTooltip(dataActionToString(action, nullptr));
                    button->setEnabled(true); // Always enabled
                    
                    // Capture action by value for the lambda
                    button->onClick = [this, action]
                    {
                        handleActionButtonClicked(action);
                    };
                    
                    addAndMakeVisible(button.get());
                    m_alwaysVisibleButtons.emplace_back(ActionButtonInfo{action, std::move(button)});
                }
            }

            // Don't call updateActionButtons() here - wait until setCurrentNode() is called
        }

        DynamicToolbarComponent::~DynamicToolbarComponent()
        {
            m_filterTextEditor.removeListener(this);
            // m_allActionButtons will automatically clean up via unique_ptr destructors
        }

        void DynamicToolbarComponent::paint(juce::Graphics &g)
        {
            // Fill background, draw a border, etc.
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).darker(0.1f)); // Example background
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawLine(0, (float)getHeight() - 1.0f, (float)getWidth(), (float)getHeight() - 1.0f, 1.0f); // Bottom border line
        }

        void DynamicToolbarComponent::lookAndFeelChanged()
        {
            // Reload icons with new accent color when theme changes
            for (auto& buttonInfo : m_allActionButtons)
            {
                auto icon = dataActionToIcon(buttonInfo.action, &getLookAndFeel());
                if (icon && buttonInfo.button)
                {
                    buttonInfo.button->setImages(icon.get());
                }
            }
            
            for (auto& buttonInfo : m_alwaysVisibleButtons)
            {
                auto icon = dataActionToIcon(buttonInfo.action, &getLookAndFeel());
                if (icon && buttonInfo.button)
                {
                    buttonInfo.button->setImages(icon.get());
                }
            }
            
            repaint();
        }

        void DynamicToolbarComponent::resized()
        {
            auto bounds = getLocalBounds().reduced(5); // Add some padding

            // Simple horizontal layout: Label, Filter Box, then Action Buttons
            // A FlexBox would be more robust for complex layouts.

            const int labelWidth = 60;
            const int filterBoxWidth = 200;
            const int buttonSpacing = 5;
            // Make buttons similar size to transport buttons (70% of toolbar height minus some padding)
            const int buttonSize = static_cast<int>(getHeight() * 0.7f) - 4;

            m_filterLabel.setBounds(bounds.removeFromLeft(labelWidth));
            m_filterTextEditor.setBounds(bounds.removeFromLeft(filterBoxWidth).reduced(0, 2)); // Reduce vertical padding a bit

            // First, position the always-visible buttons on the right
            const int buttonY = (bounds.getHeight() - buttonSize) / 2 + bounds.getY();
            int rightX = bounds.getRight();
            
            // Layout always-visible buttons from right to left
            for (auto it = m_alwaysVisibleButtons.rbegin(); it != m_alwaysVisibleButtons.rend(); ++it)
            {
                if (it->button)
                {
                    rightX -= buttonSize;
                    it->button->setBounds(rightX, buttonY, buttonSize, buttonSize);
                    it->button->setEdgeIndent(buttonSize / 5);
                    rightX -= buttonSpacing;
                }
            }
            
            // Add separator space between regular buttons and always-visible buttons
            const int rightBoundary = rightX - buttonSpacing * 2;
            
            bounds.removeFromLeft(buttonSpacing * 2); // Space between filter and buttons
            
            // Layout regular action buttons (showing all, enabled/disabled based on context)
            int x = bounds.getX();
            
            for (auto &buttonInfo : m_allActionButtons)
            {
                if (buttonInfo.button)
                {
                    if (x + buttonSize > rightBoundary)
                        break; // Don't overflow into always-visible buttons area

                    buttonInfo.button->setBounds(x, buttonY, buttonSize, buttonSize);
                    buttonInfo.button->setEdgeIndent(buttonSize / 5); // Add some padding inside the button (less padding for larger buttons)
                    x += buttonSize + buttonSpacing;
                }
            }
        }

        void DynamicToolbarComponent::setCurrentNode(INavigationNode *node)
        {
            if (m_currentNode == node)
            {
                // If it's the same node, its available actions might have changed,
                // so it's usually safest to rebuild the buttons.
            }
            m_currentNode = node;
            updateActionButtons();
        }

        void DynamicToolbarComponent::setFilterText(const juce::String &text, juce::NotificationType notification)
        {
            m_filterTextEditor.setText(text, notification == juce::sendNotification);
            // If sendNotification is false, textEditorTextChanged won't be called automatically,
            // so we might need to manually trigger the filter update if desired.
            if (notification != juce::sendNotification && m_onFilterTextChanged)
            {
                // m_onFilterTextChanged(text); // Decide if programmatic set should also trigger filter
            }
        }

        // --- juce::TextEditor::Listener overrides ---
        void DynamicToolbarComponent::textEditorReturnKeyPressed(juce::TextEditor & /*editor*/)
        {
            // Typically, return key confirms the filter.
            if (m_onFilterTextChanged)
            {
                m_onFilterTextChanged(m_filterTextEditor.getText());
            }
            // Optionally, move focus away or to the next component (e.g., the data view)
            // giveAwayKeyboardFocus();
        }

        void DynamicToolbarComponent::textEditorFocusLost(juce::TextEditor & /*editor*/)
        {
            // Apply filter when focus is lost, if not doing live filtering.
            // If doing live filtering via textEditorTextChanged, this might be redundant
            // or could be a final confirmation.
            // For now, let's assume textEditorTextChanged is the primary mechanism if live.
            // If not live, this is where you'd trigger it.
            // if (m_onFilterTextChanged)
            // {
            //     m_onFilterTextChanged(m_filterTextEditor.getText());
            // }
        }

        // Uncomment this for live filtering as the user types:
        /*
        void DynamicToolbarComponent::textEditorTextChanged(juce::TextEditor& editor)
        {
            if (m_onFilterTextChanged)
            {
                m_onFilterTextChanged(editor.getText());
            }
        }
        */

        // --- Private Helper Methods ---
        void DynamicToolbarComponent::updateActionButtons()
        {
            // Get available actions from current node
            DataActions availableActions;
            if (m_currentNode)
            {
                availableActions = m_currentNode->getNodeActions();
            }

            // Enable/disable buttons based on available actions
            for (auto &buttonInfo : m_allActionButtons)
            {
                const bool isAvailable = std::find(availableActions.begin(), availableActions.end(), buttonInfo.action) != availableActions.end();
                buttonInfo.button->setEnabled(isAvailable);
                
                // Update tooltip with context-aware text if we have a node
                if (m_currentNode)
                {
                    buttonInfo.button->setTooltip(dataActionToString(buttonInfo.action, m_currentNode));
                }
                
                // Optionally adjust opacity for disabled buttons
                buttonInfo.button->setAlpha(isAvailable ? 1.0f : 0.4f);
            }
        }

        void DynamicToolbarComponent::handleActionButtonClicked(DataAction action)
        {
            if (m_onNodeActionClicked)
            {
                m_onNodeActionClicked(action);
            }
        }

    } // namespace ui
} // namespace jucyaudio