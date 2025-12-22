#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        class AboutDialog : public juce::Component,
                           public juce::Button::Listener
        {
        public:
            AboutDialog();
            ~AboutDialog() override;
            
            void paint(juce::Graphics& g) override;
            void resized() override;
            
            // Button::Listener
            void buttonClicked(juce::Button* button) override;
            
        private:
            void closeDialog();
            
            // Logo image
            juce::Image m_logoImage;
            
            // Labels
            juce::Label m_titleLabel;
            juce::Label m_versionLabel;
            juce::Label m_copyrightLabel;
            juce::Label m_licenseLabel;
            juce::TextEditor m_licenseEditor;
            
            // Hyperlink button for website
            juce::HyperlinkButton m_websiteButton;
            
            // Close button
            juce::TextButton m_closeButton;
            
            juce::LookAndFeel_V4 m_lookAndFeel;
            
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutDialog)
        };
        
    } // namespace ui
} // namespace jucyaudio
