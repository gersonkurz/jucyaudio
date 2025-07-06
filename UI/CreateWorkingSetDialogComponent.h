#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <functional>

namespace jucyaudio
{
    namespace ui
    {
        using OnCreateWorkingSetCallback = std::function<void(const juce::String &)>;
        class CreateWorkingSetDialogComponent : public juce::Component, public juce::Button::Listener, public juce::TextEditor::Listener
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
            juce::Label m_nameLabel;
            juce::TextEditor m_nameEditor;
            juce::TextButton m_okButton;
            juce::TextButton m_cancelButton;
            juce::LookAndFeel_V4 m_lookAndFeel; // Custom LookAndFeel instance

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreateWorkingSetDialogComponent)
        };

    } // namespace ui
} // namespace jucyaudio
