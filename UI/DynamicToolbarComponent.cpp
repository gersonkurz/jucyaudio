#include <UI/DynamicToolbarComponent.h>
#include <UI/MainComponent.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace ui
    {

        DynamicToolbarComponent::DynamicToolbarComponent(MainComponent &owner)
            : m_ownerMainComponent{owner}
        {
            // Configure Filter Label
            m_filterLabel.setText("Filter:", juce::dontSendNotification);
            m_filterLabel.setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(m_filterLabel);

            // Configure Filter Text Editor
            m_filterTextEditor.setMultiLine(false);
            m_filterTextEditor.setReturnKeyStartsNewLine(false);
            m_filterTextEditor.addListener(this); // Listen for text changes, return key, focus loss
            m_filterTextEditor.setTextToShowWhenEmpty("Type to filter...", juce::Colours::grey);
            addAndMakeVisible(m_filterTextEditor);

            // Initial state: no node, so no actions
            updateActionButtons();
        }

        DynamicToolbarComponent::~DynamicToolbarComponent()
        {
            m_filterTextEditor.removeListener(this);
            // m_actionButtons (OwnedArray) will automatically delete its elements
        }

        void DynamicToolbarComponent::paint(juce::Graphics &g)
        {
            // Fill background, draw a border, etc.
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).darker(0.1f)); // Example background
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawLine(0, (float)getHeight() - 1.0f, (float)getWidth(), (float)getHeight() - 1.0f, 1.0f); // Bottom border line
        }

        void DynamicToolbarComponent::resized()
        {
            auto bounds = getLocalBounds().reduced(5); // Add some padding

            // Simple horizontal layout: Label, Filter Box, then Action Buttons
            // A FlexBox would be more robust for complex layouts.

            int labelWidth = 60;
            int filterBoxWidth = 200;
            int buttonSpacing = 5;
            //int availableWidthForButtons = bounds.getWidth() - labelWidth - filterBoxWidth - buttonSpacing;

            m_filterLabel.setBounds(bounds.removeFromLeft(labelWidth));
            m_filterTextEditor.setBounds(bounds.removeFromLeft(filterBoxWidth).reduced(0, 2)); // Reduce vertical padding a bit

            bounds.removeFromLeft(buttonSpacing); // Space between filter and buttons

            // Layout action buttons
            int x = bounds.getX();
            for (auto *button : m_actionButtons)
            {
                if (button)
                {
                    // Simple layout: give each button a fixed width for now, or calculate based on text
                    int buttonWidth = 80; // Example fixed width
                    if (x + buttonWidth > bounds.getRight())
                        break; // Don't overflow

                    button->setBounds(x, bounds.getY(), buttonWidth, bounds.getHeight());
                    x += buttonWidth + buttonSpacing;
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
            m_actionButtons.clear(); // Removes and deletes existing buttons

            if (m_currentNode)
            {
                for (const auto &action : m_currentNode->getNodeActions())
                {
                    if (action == DataAction::None)
                        continue; // Skip "None" action

                    auto *button = m_actionButtons.add(new juce::TextButton{});
                    button->setButtonText(dataActionToString(action));
                    // Capture 'action' by value for the lambda
                    button->onClick = [this, action]
                    {
                        handleActionButtonClicked(action);
                    };
                    addAndMakeVisible(button);
                }
            }
            resized(); // Trigger a re-layout after buttons change
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