#pragma once

#include <Database/Includes/TrackMarker.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <chrono>
#include <optional>

namespace jucyaudio
{
    namespace ui
    {
        class MarkerEditDialog : public juce::Component, private juce::Timer
        {
        public:
            MarkerEditDialog();
            ~MarkerEditDialog() override = default;
            
            void paint(juce::Graphics& g) override;
            void resized() override;
            void parentHierarchyChanged() override;
            void timerCallback() override;
            
            // Set up for new marker
            void setupForNewMarker(std::chrono::milliseconds position);
            
            // Set up for editing existing marker
            void setupForExistingMarker(const database::TrackMarker& marker);
            
            // Get the edited comment
            juce::String getComment() const { return m_commentEditor.getText(); }
            
            // Callbacks
            std::function<void()> onSave;
            std::function<void()> onDelete;
            std::function<void()> onCancel;
            
        private:
            juce::Label m_titleLabel;
            juce::Label m_positionLabel;
            juce::TextEditor m_commentEditor;
            juce::TextButton m_saveButton{"Save"};
            juce::TextButton m_deleteButton{"Delete"};
            juce::TextButton m_cancelButton{"Cancel"};
            
            bool m_isNewMarker{true};
            std::chrono::milliseconds m_position{0};
            std::optional<database::MarkerId> m_markerId;
            
            juce::String formatPosition(std::chrono::milliseconds ms) const;
            
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkerEditDialog)
        };
    }
}