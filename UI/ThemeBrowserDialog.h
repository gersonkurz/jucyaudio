#pragma once

#include <UI/ThemeManager.h>
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        // Theme picker. Lists the available themes split into Light and Dark columns, each row
        // showing the theme name plus its full 16-colour base16 swatch. Selecting a theme
        // live-previews it (non-persisting); OK persists the choice; Cancel / Esc / closing the
        // window restores the theme that was active when the dialog opened (also non-persisting).
        class ThemeBrowserDialog final : public juce::Component
        {
        public:
            using PreviewFn = std::function<void(size_t themeIndex)>; // apply live, do not persist
            using CommitFn = std::function<void(size_t themeIndex)>;  // apply and persist
            using RestoreFn = std::function<void(size_t themeIndex)>; // restore opening theme, no persist

            ThemeBrowserDialog(const std::vector<JucyTheme> &themes,
                size_t currentIndex,
                PreviewFn onPreview,
                CommitFn onCommit,
                RestoreFn onRestore);
            ~ThemeBrowserDialog() override;

            void resized() override;

            // Builds the dialog and launches it asynchronously, centred on parent.
            static void launch(juce::Component *parent,
                const std::vector<JucyTheme> &themes,
                size_t currentIndex,
                PreviewFn onPreview,
                CommitFn onCommit,
                RestoreFn onRestore);

        private:
            // ListBoxModel for one column (Light or Dark): renders name + 16-colour swatch.
            class SwatchListModel final : public juce::ListBoxModel
            {
            public:
                SwatchListModel(const std::vector<JucyTheme> &themes, std::vector<size_t> indices, std::function<void(int)> onSelect);

                int getNumRows() override;
                void paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected) override;
                void selectedRowsChanged(int lastRowSelected) override;

                size_t themeIndexForRow(int row) const;
                int rowForThemeIndex(size_t themeIndex) const;

            private:
                const std::vector<JucyTheme> &m_themes;
                std::vector<size_t> m_indices;
                std::function<void(int)> m_onSelect;
            };

            void selectThemeFrom(juce::ListBox &source, SwatchListModel &model, juce::ListBox &other, int row);
            void close(bool commit);

            const std::vector<JucyTheme> &m_themes;
            const size_t m_openingIndex;
            size_t m_selectedIndex;
            bool m_committed{false};

            PreviewFn m_onPreview;
            CommitFn m_onCommit;
            RestoreFn m_onRestore;

            juce::Label m_lightLabel;
            juce::Label m_darkLabel;
            juce::ListBox m_lightList;
            juce::ListBox m_darkList;
            std::unique_ptr<SwatchListModel> m_lightModel;
            std::unique_ptr<SwatchListModel> m_darkModel;
            juce::TextButton m_okButton;
            juce::TextButton m_cancelButton;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemeBrowserDialog)
        };

    } // namespace ui
} // namespace jucyaudio
