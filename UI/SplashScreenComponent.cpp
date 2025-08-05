#include <UI/SplashScreenComponent.h>
#include <BinaryData.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        JucyAudioSplashScreen::JucyAudioSplashScreen()
            : SplashScreen("JucyAudio", 400, 300, true /* use drop shadow */)
        {
            // Load the logo image
            m_logo = juce::ImageCache::getFromMemory(BinaryData::orangejucyaudiologo_png, BinaryData::orangejucyaudiologo_pngSize);
            
            spdlog::info("JucyAudioSplashScreen constructor - logo size: {}x{}, valid: {}", 
                m_logo.getWidth(), m_logo.getHeight(), m_logo.isValid());
            
            // If we have a valid logo, resize to its dimensions
            if (m_logo.isValid() && m_logo.getWidth() > 0 && m_logo.getHeight() > 0)
            {
                setSize(m_logo.getWidth(), m_logo.getHeight());
            }
            
            // Center the splash screen on screen
            centreWithSize(getWidth(), getHeight());
            
            // Force a repaint
            repaint();
        }

        void JucyAudioSplashScreen::paint(juce::Graphics &g)
        {
            // Let's test with a simple gradient first
            juce::ColourGradient gradient(juce::Colours::orange, 0, 0, 
                                         juce::Colours::darkgrey, getWidth(), getHeight(), false);
            g.setGradientFill(gradient);
            g.fillRect(getLocalBounds());
            
            // Draw the logo on top if valid
            if (m_logo.isValid())
            {
                // Try centering it with some padding
                const int padding = 1;
                g.drawImageWithin(m_logo, padding, padding, 
                                 getWidth() - 2 * padding, 
                                 getHeight() - 2 * padding,
                                 juce::RectanglePlacement::centred | 
                                 juce::RectanglePlacement::onlyReduceInSize);
                
                spdlog::info("Drew logo with padding");
            }
            
            // Draw version number in bottom right
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(16.0f));
            
            juce::String versionText = "v" + juce::String(PROJECT_VERSION);
            const int textWidth = 100;
            const int textHeight = 30;
            const int margin = 10;
            
            juce::Rectangle<int> textArea(getWidth() - textWidth - margin, 
                                         getHeight() - textHeight - margin,
                                         textWidth, textHeight);
            
            g.drawText(versionText, textArea, juce::Justification::bottomRight);
        }
    } // namespace ui
} // namespace jucyaudio