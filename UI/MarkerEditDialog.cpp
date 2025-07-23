#include <UI/MarkerEditDialog.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        MarkerEditDialog::MarkerEditDialog()
        {
            // Title label
            m_titleLabel.setText("Marker", juce::dontSendNotification);
            m_titleLabel.setFont(juce::Font(16.0f).withStyle(juce::Font::bold));
            m_titleLabel.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(m_titleLabel);
            
            // Position label
            m_positionLabel.setJustificationType(juce::Justification::centred);
            m_positionLabel.setColour(juce::Label::textColourId, 
                getLookAndFeel().findColour(juce::Label::textColourId).withAlpha(0.7f));
            addAndMakeVisible(m_positionLabel);
            
            // Comment editor
            m_commentEditor.setMultiLine(true);
            m_commentEditor.setReturnKeyStartsNewLine(true);
            m_commentEditor.setPopupMenuEnabled(true);
            m_commentEditor.setScrollbarsShown(true);
            m_commentEditor.setFont(juce::Font(14.0f));
            addAndMakeVisible(m_commentEditor);
            
            // Buttons
            m_saveButton.onClick = [this] { 
                if (onSave) onSave(); 
            };
            addAndMakeVisible(m_saveButton);
            
            m_deleteButton.onClick = [this] { 
                if (onDelete) onDelete(); 
            };
            addAndMakeVisible(m_deleteButton);
            
            m_cancelButton.onClick = [this] { 
                if (onCancel) onCancel(); 
            };
            addAndMakeVisible(m_cancelButton);
            
            setSize(400, 300);
        }
        
        void MarkerEditDialog::paint(juce::Graphics& g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
            
            // Draw a border
            g.setColour(getLookAndFeel().findColour(juce::ComboBox::outlineColourId));
            g.drawRect(getLocalBounds(), 1);
        }
        
        void MarkerEditDialog::resized()
        {
            auto bounds = getLocalBounds().reduced(10);
            
            // Title
            m_titleLabel.setBounds(bounds.removeFromTop(25));
            
            // Position label
            m_positionLabel.setBounds(bounds.removeFromTop(20));
            
            bounds.removeFromTop(10); // Spacing
            
            // Button area at bottom
            auto buttonArea = bounds.removeFromBottom(30);
            const int buttonWidth = 80;
            const int buttonSpacing = 10;
            
            // Comment editor takes remaining space
            m_commentEditor.setBounds(bounds.reduced(0, 5));
            
            // Layout buttons (right-aligned)
            if (m_isNewMarker)
            {
                // For new markers: Cancel and Save
                m_cancelButton.setBounds(buttonArea.removeFromRight(buttonWidth));
                buttonArea.removeFromRight(buttonSpacing);
                m_saveButton.setBounds(buttonArea.removeFromRight(buttonWidth));
                m_deleteButton.setVisible(false);
            }
            else
            {
                // For existing markers: Delete, Cancel, and Save
                m_cancelButton.setBounds(buttonArea.removeFromRight(buttonWidth));
                buttonArea.removeFromRight(buttonSpacing);
                m_saveButton.setBounds(buttonArea.removeFromRight(buttonWidth));
                buttonArea.removeFromRight(buttonSpacing);
                m_deleteButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
                m_deleteButton.setVisible(true);
            }
        }
        
        void MarkerEditDialog::setupForNewMarker(std::chrono::milliseconds position)
        {
            m_isNewMarker = true;
            m_position = position;
            m_markerId.reset();
            
            m_titleLabel.setText("New Marker", juce::dontSendNotification);
            m_positionLabel.setText(formatPosition(position), juce::dontSendNotification);
            m_commentEditor.clear();
            
            // Update button visibility
            resized();
        }
        
        void MarkerEditDialog::setupForExistingMarker(const database::TrackMarker& marker)
        {
            m_isNewMarker = false;
            m_position = marker.position;
            m_markerId = marker.markerId;
            
            m_titleLabel.setText("Edit Marker", juce::dontSendNotification);
            m_positionLabel.setText(formatPosition(marker.position), juce::dontSendNotification);
            m_commentEditor.setText(marker.comment, false);
            
            // Update button visibility
            resized();
        }
        
        void MarkerEditDialog::parentHierarchyChanged()
        {
            if (isShowing() && !isTimerRunning())
            {
                // Start a short timer to grab focus after the dialog window is active
                startTimer(100);
            }
        }
        
        void MarkerEditDialog::timerCallback()
        {
            stopTimer();
            if (auto* dialogWindow = findParentComponentOfClass<juce::DialogWindow>())
            {
                if (dialogWindow->isActiveWindow())
                {
                    m_commentEditor.grabKeyboardFocus();
                    if (!m_isNewMarker)
                    {
                        m_commentEditor.selectAll();
                    }
                }
                else
                {
                    // If window isn't active yet, try again
                    startTimer(50);
                }
            }
        }
        
        juce::String MarkerEditDialog::formatPosition(std::chrono::milliseconds ms) const
        {
            const auto totalSeconds = ms.count() / 1000;
            const auto minutes = totalSeconds / 60;
            const auto seconds = totalSeconds % 60;
            const auto milliseconds = ms.count() % 1000;
            
            return juce::String::formatted("%d:%02d.%03d", 
                static_cast<int>(minutes), 
                static_cast<int>(seconds),
                static_cast<int>(milliseconds));
        }
    }
}