#pragma once

#include <Database/Includes/INavigationNode.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace jucyaudio
{
    namespace ui
    {
        // Forward declaration
        class MainComponent;

        // Using directives for convenience
        using database::DataAction;
        using database::INavigationNode;

        // Alias for the action callback function type
        using ToolbarNodeActionCallback = std::function<void(DataAction action)>;
        // Alias for the filter text changed callback
        using FilterTextChangedCallback = std::function<void(const juce::String &newFilterText)>;

        class DynamicToolbarComponent : public juce::Component,
                                        private juce::TextEditor::Listener // To listen for filter text changes
        {
        public:
            explicit DynamicToolbarComponent(MainComponent &owner);
            ~DynamicToolbarComponent() override;

            void resized() override;
            void paint(juce::Graphics &g) override; // For background, borders, etc.

            // Sets the current INavigationNode whose actions should be displayed.
            // The node's lifetime is managed externally (e.g., by MainComponent).
            // This component does NOT retain/release the node passed here.
            void setCurrentNode(INavigationNode *node);

            // Callbacks to MainComponent
            ToolbarNodeActionCallback m_onNodeActionClicked;
            FilterTextChangedCallback m_onFilterTextChanged;

            // Method for MainComponent to set the filter text programmatically if needed
            void setFilterText(const juce::String &text, juce::NotificationType notification = juce::sendNotification);

        private:
            // juce::TextEditor::Listener override
            void textEditorReturnKeyPressed(juce::TextEditor &editor) override;
            void textEditorFocusLost(juce::TextEditor &editor) override;
            // void textEditorTextChanged(juce::TextEditor& editor) override; // Use this for live filtering

            // Helper to rebuild action buttons based on the current INavigationNode
            void updateActionButtons();
            // Helper to handle when an action button is clicked
            void handleActionButtonClicked(DataAction action);

            MainComponent &m_ownerMainComponent;

            // Non-owning pointer to the INavigationNode currently providing actions.
            INavigationNode *m_currentNode{nullptr};

            juce::TextEditor m_filterTextEditor;
            juce::Label m_filterLabel; // Optional label for the filter box

            // Store action buttons. juce::OwnedArray manages their lifetime.
            juce::OwnedArray<juce::TextButton> m_actionButtons;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicToolbarComponent)
        };

    } // namespace ui
} // namespace jucyaudio