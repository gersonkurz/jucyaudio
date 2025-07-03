#include <UI/DividerComponent.h>
#include <UI/MainComponent.h>

namespace jucyaudio
{
    namespace ui
    {

        DividerComponent::DividerComponent(MainComponent &owner, bool isVerticalLayout)
            : m_ownerMainComponent{owner},
              m_isVerticalLayout{isVerticalLayout}
        {
            // Set the cursor that should be shown when the mouse is over this component
            setMouseCursor(m_isVerticalLayout ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
            setRepaintsOnMouseActivity(true); // Optional: repaint on mouse enter/exit if paint changes
        }

        DividerComponent::~DividerComponent()
        {
        }

        void DividerComponent::paint(juce::Graphics &g)
        {
            // Simple visual for the divider
            auto &lf = getLookAndFeel();
            g.fillAll(lf.findColour(juce::Slider::thumbColourId).darker(0.2f)); // Use a slider-like color

            // Optional: a little line in the middle for visual cue
            g.setColour(lf.findColour(juce::Slider::trackColourId));
            if (m_isVerticalLayout)
            {
                // Draw a subtle vertical line or dots
                g.drawVerticalLine(getWidth() / 2, 0.0f, (float)getHeight());
            }
            else
            {
                // Draw a subtle horizontal line or dots
                g.drawHorizontalLine(getHeight() / 2, 0.0f, (float)getWidth());
            }

            juce::Colour baseColour = lf.findColour(juce::Slider::thumbColourId).darker(0.2f);

            if (isMouseButtonDown() || isMouseOver()) // Highlight if dragging or hovered
            {
                baseColour = baseColour.brighter(0.3f);
            }
            g.fillAll(baseColour); 
        }

        // Mouse interaction methods - Stubs for Step 1, will be implemented in Step 2
        void DividerComponent::mouseEnter([[maybe_unused]] const juce::MouseEvent& event)
        {
            // Cursor is already set in constructor. Repaint if paint() depends on isMouseOver().
            // repaint(); // If paint changes on hover
        }

        void DividerComponent::mouseExit([[maybe_unused]] const juce::MouseEvent &event)
        {
            // Cursor remains resize cursor. Repaint if paint() depends on isMouseOver().
            // repaint(); // If paint changes on hover
        }

        void DividerComponent::mouseDown([[maybe_unused]] const juce::MouseEvent &event)
        {
            m_isMouseDown = true;
            // m_componentDragger.startDraggingComponent(this, event);
            // Instead of using ComponentDragger to move *this* component directly (which would require
            // complex constraints on this small bar), we'll use its position to tell MainComponent
            // how to adjust m_navPanelWidth.

            // We need to know the *original* width of the nav panel at the start of this drag operation
            // to correctly calculate the new width based on mouse movement.
            // MainComponent will provide this.
            m_originalNavPanelWidthAtDragStart = m_ownerMainComponent.getCurrentNavPanelWidth();
            repaint(); // Update visual if paint() changes for m_isMouseDown
        }

        void DividerComponent::mouseDrag([[maybe_unused]] const juce::MouseEvent &event)
        {
            if (m_isMouseDown)
            {
                // m_componentDragger.dragComponent(this, event, nullptr);
                // We are not dragging the component itself using ComponentDragger in a free way.
                // We are calculating how much the nav panel width should change.

                // event.getScreenX() is the current mouse X in screen coordinates.
                // event.getMouseDownScreenX() is the screen X where mouse was pressed.
                int deltaX = event.getScreenX() - event.getMouseDownScreenX();

                // Tell MainComponent the original nav panel width and the delta from mouse drag
                if (m_isVerticalLayout)
                {
                    m_ownerMainComponent.updateNavPanelWidthFromDrag(m_originalNavPanelWidthAtDragStart, deltaX);
                }
                // else if horizontal divider, use deltaY and a different MainComponent method.
            }
        }

        void DividerComponent::mouseUp([[maybe_unused]] const juce::MouseEvent &event)
        {
            if (m_isMouseDown)
            {
                m_isMouseDown = false;
                repaint(); // Update visual if paint() changes for m_isMouseDown
                // No need to call m_ownerMainComponent here typically, mouseDrag did the work.
                // Or, this could be a "commit" point if mouseDrag was only provisional.
            }
        }
        

    } // namespace ui
} // namespace jucyaudio