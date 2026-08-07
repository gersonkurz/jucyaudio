#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief Shows pre-formatted, selectable/copyable track detail text.
         *
         * The text lives in a read-only TextEditor, so it can be selected and copied
         * (Ctrl+C or the right-click menu). When more than one page is supplied, Prev/Next
         * buttons navigate between them with an "N of M" indicator.
         */
        class TrackDetailsDialog : public juce::Component
        {
        public:
            explicit TrackDetailsDialog(std::vector<juce::String> pages);

            void resized() override;

        private:
            void showPage();

            std::vector<juce::String> m_pages;
            size_t m_index{0};

            juce::TextEditor m_text;
            juce::Label m_position;
            juce::TextButton m_prev{"< Prev"};
            juce::TextButton m_next{"Next >"};
            juce::TextButton m_copy{"Copy"};
            juce::TextButton m_close{"Close"};

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackDetailsDialog)
        };
    } // namespace ui
} // namespace jucyaudio
