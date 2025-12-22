#include <BinaryData.h>
#include <UI/AboutDialog.h>
#include <UI/ThemeManager.h>
#include <spdlog/spdlog.h>

// PROJECT_VERSION is defined by CMake
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "Unknown"
#endif

namespace jucyaudio
{
    namespace ui
    {
        static juce::File getThirdPartyNoticesFile()
        {
            auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
#if JUCE_MAC
            // currentApplicationFile points to .../Contents/MacOS/jucyaudio
            auto resourcesDir = appFile.getParentDirectory().getParentDirectory().getChildFile("Resources");
            return resourcesDir.getChildFile("licenses").getChildFile("THIRD_PARTY_NOTICES.txt");
#else
            // Windows: licenses folder next to the executable
            return appFile.getParentDirectory().getChildFile("licenses").getChildFile("THIRD_PARTY_NOTICES.txt");
#endif
        }

        AboutDialog::AboutDialog()
            : m_titleLabel{"titleLabel", "JucyAudio"},
              m_versionLabel{"versionLabel", "Version " PROJECT_VERSION},
              m_copyrightLabel{"copyrightLabel",
                  juce::CharPointer_UTF8("\xc2\xa9 2025 JucyAudio\n\nThis is free software, licensed under the\nGNU General Public License v3.0 or later")},
              m_licenseLabel{"licenseLabel", "Third-party notices"},
              m_websiteButton{"https://jucyaudio.com", juce::URL("https://jucyaudio.com")},
              m_closeButton{"Close"}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);

            // Load logo from binary data
            m_logoImage = juce::ImageCache::getFromMemory(BinaryData::orangejucyaudiosmall_png, BinaryData::orangejucyaudiosmall_pngSize);

            // Title is part of the logo, so we don't need to show it separately
            // But we still need to create it since it's declared in the header
            // Just don't make it visible

            // Version - white text
            addAndMakeVisible(m_versionLabel);
            m_versionLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f)});
            m_versionLabel.setJustificationType(juce::Justification::centred);
            m_versionLabel.setColour(juce::Label::textColourId, juce::Colours::white);

            // Copyright and License - white text, smaller font for license info
            addAndMakeVisible(m_copyrightLabel);
            m_copyrightLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(12.0f)});
            m_copyrightLabel.setJustificationType(juce::Justification::centred);
            m_copyrightLabel.setColour(juce::Label::textColourId, juce::Colours::white);

            // Third-party notices
            addAndMakeVisible(m_licenseLabel);
            m_licenseLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(12.0f)}.boldened());
            m_licenseLabel.setJustificationType(juce::Justification::centred);
            m_licenseLabel.setColour(juce::Label::textColourId, juce::Colours::white);

            addAndMakeVisible(m_licenseEditor);
            m_licenseEditor.setReadOnly(true);
            m_licenseEditor.setMultiLine(true);
            m_licenseEditor.setScrollbarsShown(true);
            m_licenseEditor.setCaretVisible(false);
            m_licenseEditor.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
            m_licenseEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
            m_licenseEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFFF96E00).darker(0.25f));
            m_licenseEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFFF96E00).darker(0.35f));

            const auto noticesFile = getThirdPartyNoticesFile();
            if (noticesFile.existsAsFile())
            {
                m_licenseEditor.setText(noticesFile.loadFileAsString());
            }
            else
            {
                m_licenseEditor.setText("Third-party notices file not found.\nExpected: " + noticesFile.getFullPathName());
            }

            // Website hyperlink - white with underline
            addAndMakeVisible(m_websiteButton);
            m_websiteButton.setFont(juce::Font{juce::FontOptions{}.withHeight(14.0f).withUnderline()}, false);
            m_websiteButton.setColour(juce::HyperlinkButton::textColourId, juce::Colours::white);

            // Close button
            addAndMakeVisible(m_closeButton);
            m_closeButton.addListener(this);

            // Set dialog size (increased height for license text)
            setSize(420, 520);

            // Set initial focus to close button
            juce::MessageManager::callAsync(
                [this]()
                {
                    if (isShowing())
                    {
                        m_closeButton.grabKeyboardFocus();
                    }
                });
        }

        AboutDialog::~AboutDialog()
        {
            setLookAndFeel(nullptr);
        }

        void AboutDialog::paint(juce::Graphics &g)
        {
            // Use solid orange background #F96E00
            const juce::Colour orangeBackground(0xFFF96E00);
            g.fillAll(orangeBackground);

            // Draw subtle border
            g.setColour(orangeBackground.darker(0.2f));
            g.drawRect(getLocalBounds(), 2);

            // Draw logo if loaded (maintaining aspect ratio 280x220)
            if (m_logoImage.isValid())
            {
                // Original aspect ratio: 280x220 = 1.27:1
                const int logoWidth = 140;  // Scale down to fit dialog
                const int logoHeight = 110; // Maintain 280:220 ratio
                const int logoX = (getWidth() - logoWidth) / 2;
                const int logoY = 20;

                g.drawImage(m_logoImage, logoX, logoY, logoWidth, logoHeight, 0, 0, m_logoImage.getWidth(), m_logoImage.getHeight());
            }
        }

        void AboutDialog::resized()
        {
            auto area = getLocalBounds().reduced(20);

            // Space for logo at top (logo is 110px high + 20px margin)
            const int logoSpace = m_logoImage.isValid() ? 140 : 20;
            area.removeFromTop(logoSpace);

            // Title is part of the logo, so skip it

            // Version
            m_versionLabel.setBounds(area.removeFromTop(25));
            area.removeFromTop(10);

            // Copyright and License (needs more height for multi-line text)
            m_copyrightLabel.setBounds(area.removeFromTop(60));
            area.removeFromTop(10);

            // Third-party notices
            m_licenseLabel.setBounds(area.removeFromTop(20));
            area.removeFromTop(5);
            m_licenseEditor.setBounds(area.removeFromTop(170));
            area.removeFromTop(10);

            // Website link
            auto linkArea = area.removeFromTop(25);
            const int linkWidth = 150;
            m_websiteButton.setBounds(linkArea.withSizeKeepingCentre(linkWidth, 25));

            // Close button at bottom
            auto buttonArea = getLocalBounds().removeFromBottom(40).reduced(20, 5);
            const int buttonWidth = 80;
            m_closeButton.setBounds(buttonArea.withSizeKeepingCentre(buttonWidth, 30));
        }

        void AboutDialog::buttonClicked(juce::Button *button)
        {
            if (button == &m_closeButton)
            {
                closeDialog();
            }
        }

        void AboutDialog::closeDialog()
        {
            if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                dw->exitModalState(0);
            }
        }

    } // namespace ui
} // namespace jucyaudio
