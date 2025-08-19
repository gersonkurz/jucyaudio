#pragma once
#include <Database/Includes/WorkingSetInfo.h>
#include <functional>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        using OnCreateWorkingSetCallback = std::function<void(const juce::String &, WorkingSetId)>;

        class CreateWorkingSetDialogComponent : public juce::Component,
                                                public juce::Button::Listener,
                                                public juce::TextEditor::Listener,
                                                public juce::ComboBox::Listener
        {
        public:
            CreateWorkingSetDialogComponent(int64_t trackCount, OnCreateWorkingSetCallback onOkCallback);
            ~CreateWorkingSetDialogComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            // Button::Listener
            void buttonClicked(juce::Button *button) override;

            // TextEditor::Listener
            void textEditorReturnKeyPressed(juce::TextEditor &editor) override;
            void textEditorFocusLost([[maybe_unused]] juce::TextEditor &editor) override
            {
            } // Required by TextEditor::Listener

            // ComboBox::Listener
            void comboBoxChanged(juce::ComboBox *comboBox) override;

            bool keyPressed(const juce::KeyPress &key) override;

        private:
            void handleOk();
            void handleCancel();
            juce::String generateDefaultName();

            int64_t m_trackCount;
            OnCreateWorkingSetCallback m_onOkCallback;

            // UI Elements
            juce::Label m_titleLabel;
            juce::Label m_countLabel;
            juce::Label m_wsSelectLabel;
            juce::ComboBox m_wsSelectCombo;
            juce::Label m_nameLabel;
            juce::TextEditor m_nameEditor;
            juce::TextButton m_okButton;
            juce::TextButton m_cancelButton;
            juce::LookAndFeel_V4 m_lookAndFeel; // Custom LookAndFeel instance

            // Store available working sets
            std::vector<database::WorkingSetInfo> m_availableWorkingSets;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreateWorkingSetDialogComponent)
        };

    } // namespace ui
} // namespace jucyaudio
