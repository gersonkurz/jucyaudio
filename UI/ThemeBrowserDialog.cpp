#include <UI/ThemeBrowserDialog.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        // ---- SwatchListModel --------------------------------------------------

        ThemeBrowserDialog::SwatchListModel::SwatchListModel(
            const std::vector<JucyTheme> &themes, std::vector<size_t> indices, std::function<void(int)> onSelect)
            : m_themes{themes},
              m_indices{std::move(indices)},
              m_onSelect{std::move(onSelect)}
        {
        }

        int ThemeBrowserDialog::SwatchListModel::getNumRows()
        {
            return static_cast<int>(m_indices.size());
        }

        size_t ThemeBrowserDialog::SwatchListModel::themeIndexForRow(int row) const
        {
            return m_indices.at(static_cast<size_t>(row));
        }

        int ThemeBrowserDialog::SwatchListModel::rowForThemeIndex(size_t themeIndex) const
        {
            for (size_t i = 0; i < m_indices.size(); ++i)
            {
                if (m_indices[i] == themeIndex)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        void ThemeBrowserDialog::SwatchListModel::paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected)
        {
            if (rowNumber < 0 || rowNumber >= getNumRows())
            {
                return;
            }
            const auto &theme = m_themes[m_indices[static_cast<size_t>(rowNumber)]];
            const auto &lf = juce::LookAndFeel::getDefaultLookAndFeel();

            if (rowIsSelected)
            {
                g.setColour(lf.findColour(juce::ListBox::textColourId).withAlpha(0.20f));
                g.fillRect(0, 0, width, height);
            }

            // 16-colour swatch strip on the right (base00..base0F).
            constexpr int swatchCount = 16;
            constexpr int swatchWidth = 9;
            constexpr int pad = 6;
            const int stripWidth = swatchCount * swatchWidth;
            const int stripX = width - pad - stripWidth;
            const int swatchHeight = juce::jmin(16, height - 6);
            const int swatchY = (height - swatchHeight) / 2;
            for (int i = 0; i < swatchCount; ++i)
            {
                g.setColour(theme.palette[static_cast<size_t>(i)]);
                g.fillRect(stripX + i * swatchWidth, swatchY, swatchWidth - 1, swatchHeight);
            }

            // Theme name on the left.
            g.setColour(lf.findColour(juce::ListBox::textColourId));
            g.setFont(static_cast<float>(juce::jmin(15, height - 8)));
            g.drawText(theme.name, pad, 0, stripX - pad - 4, height, juce::Justification::centredLeft, true);
        }

        void ThemeBrowserDialog::SwatchListModel::selectedRowsChanged(int lastRowSelected)
        {
            if (lastRowSelected >= 0 && m_onSelect)
            {
                m_onSelect(lastRowSelected);
            }
        }

        // ---- ThemeBrowserDialog ----------------------------------------------

        ThemeBrowserDialog::ThemeBrowserDialog(const std::vector<JucyTheme> &themes,
            size_t currentIndex,
            PreviewFn onPreview,
            CommitFn onCommit,
            RestoreFn onRestore)
            : m_themes{themes},
              m_openingIndex{currentIndex},
              m_selectedIndex{currentIndex},
              m_onPreview{std::move(onPreview)},
              m_onCommit{std::move(onCommit)},
              m_onRestore{std::move(onRestore)}
        {
            std::vector<size_t> lightIndices;
            std::vector<size_t> darkIndices;
            for (size_t i = 0; i < m_themes.size(); ++i)
            {
                (m_themes[i].isDark ? darkIndices : lightIndices).push_back(i);
            }

            m_lightModel = std::make_unique<SwatchListModel>(m_themes,
                lightIndices,
                [this](int row) { selectThemeFrom(m_lightList, *m_lightModel, m_darkList, row); });
            m_darkModel = std::make_unique<SwatchListModel>(m_themes,
                darkIndices,
                [this](int row) { selectThemeFrom(m_darkList, *m_darkModel, m_lightList, row); });

            m_lightLabel.setText("Light", juce::dontSendNotification);
            m_darkLabel.setText("Dark", juce::dontSendNotification);
            m_lightLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f)}.boldened());
            m_darkLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f)}.boldened());
            addAndMakeVisible(m_lightLabel);
            addAndMakeVisible(m_darkLabel);

            m_lightList.setModel(m_lightModel.get());
            m_darkList.setModel(m_darkModel.get());
            m_lightList.setRowHeight(24);
            m_darkList.setRowHeight(24);
            addAndMakeVisible(m_lightList);
            addAndMakeVisible(m_darkList);

            m_okButton.setButtonText("OK");
            m_cancelButton.setButtonText("Cancel");
            m_okButton.onClick = [this] { close(true); };
            m_cancelButton.onClick = [this] { close(false); };
            addAndMakeVisible(m_okButton);
            addAndMakeVisible(m_cancelButton);

            // Highlight the currently-active theme in whichever column it belongs to.
            if (const int lightRow = m_lightModel->rowForThemeIndex(currentIndex); lightRow >= 0)
            {
                m_lightList.selectRow(lightRow);
                m_lightList.scrollToEnsureRowIsOnscreen(lightRow);
            }
            else if (const int darkRow = m_darkModel->rowForThemeIndex(currentIndex); darkRow >= 0)
            {
                m_darkList.selectRow(darkRow);
                m_darkList.scrollToEnsureRowIsOnscreen(darkRow);
            }

            setSize(620, 460);
        }

        ThemeBrowserDialog::~ThemeBrowserDialog()
        {
            // Detach models before they are destroyed while the ListBoxes still reference them.
            m_lightList.setModel(nullptr);
            m_darkList.setModel(nullptr);

            // Any close path that isn't an explicit OK restores the opening theme. This covers the
            // Cancel button, Esc, and the window close button (all of which destroy the dialog).
            if (!m_committed && m_onRestore)
            {
                m_onRestore(m_openingIndex);
            }
        }

        void ThemeBrowserDialog::selectThemeFrom(juce::ListBox &source, SwatchListModel &model, juce::ListBox &other, int row)
        {
            juce::ignoreUnused(source);
            if (row < 0 || row >= model.getNumRows())
            {
                return;
            }
            // Single logical selection across both columns: clear the other column. Deselecting it
            // fires selectedRowsChanged(-1), which the model ignores, so there is no feedback loop.
            other.deselectAllRows();

            m_selectedIndex = model.themeIndexForRow(row);
            if (m_onPreview)
            {
                m_onPreview(m_selectedIndex);
            }
            // Re-theme the dialog's own chrome to match the previewed theme.
            sendLookAndFeelChange();
        }

        void ThemeBrowserDialog::close(bool commit)
        {
            if (commit)
            {
                m_committed = true;
                if (m_onCommit)
                {
                    m_onCommit(m_selectedIndex);
                }
            }
            if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                dw->exitModalState(commit ? 1 : 0);
            }
        }

        void ThemeBrowserDialog::resized()
        {
            auto area = getLocalBounds().reduced(12);

            auto buttonRow = area.removeFromBottom(34);
            m_okButton.setBounds(buttonRow.removeFromRight(90));
            buttonRow.removeFromRight(8);
            m_cancelButton.setBounds(buttonRow.removeFromRight(90));

            area.removeFromBottom(8);

            const int columnWidth = (area.getWidth() - 12) / 2;
            auto lightColumn = area.removeFromLeft(columnWidth);
            area.removeFromLeft(12);
            auto darkColumn = area;

            m_lightLabel.setBounds(lightColumn.removeFromTop(22));
            m_lightList.setBounds(lightColumn);

            m_darkLabel.setBounds(darkColumn.removeFromTop(22));
            m_darkList.setBounds(darkColumn);
        }

        void ThemeBrowserDialog::launch(juce::Component *parent,
            const std::vector<JucyTheme> &themes,
            size_t currentIndex,
            PreviewFn onPreview,
            CommitFn onCommit,
            RestoreFn onRestore)
        {
            auto *content = new ThemeBrowserDialog{themes, currentIndex, std::move(onPreview), std::move(onCommit), std::move(onRestore)};

            juce::DialogWindow::LaunchOptions options;
            options.content.setOwned(content);
            options.dialogTitle = "Select Theme";
            options.componentToCentreAround = parent;
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = true;
            options.launchAsync();
        }

    } // namespace ui
} // namespace jucyaudio
