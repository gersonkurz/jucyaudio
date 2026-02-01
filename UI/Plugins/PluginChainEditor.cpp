#include <UI/Plugins/PluginChainEditor.h>
#include <UI/Plugins/PluginScanDialog.h>
#include <UI/Plugins/PluginWindow.h>
#include <UI/CustomColourIds.h>
#include <Audio/Plugins/MasterPluginChainPersistence.h>
#include <algorithm>
#include <format>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        PluginChainEditor::PluginChainEditor()
            : m_titleLabel{"title", "Master Effects"}
        {
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(22.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(m_titleLabel);
            addAndMakeVisible(m_cpuLabel);
            addAndMakeVisible(m_globalBypassButton);
            m_cpuLabel.setJustificationType(juce::Justification::centredRight);

            m_availablePluginsCombo.setTextWhenNothingSelected("Select a VST3 plugin");
            addAndMakeVisible(m_availablePluginsCombo);

            addAndMakeVisible(m_refreshButton);
            addAndMakeVisible(m_scanButton);
            addAndMakeVisible(m_addButton);
            addAndMakeVisible(m_chainList);
            addAndMakeVisible(m_removeButton);
            addAndMakeVisible(m_moveUpButton);
            addAndMakeVisible(m_moveDownButton);
            addAndMakeVisible(m_bypassButton);
            addAndMakeVisible(m_openEditorButton);

            m_refreshButton.addListener(this);
            m_scanButton.addListener(this);
            m_addButton.addListener(this);
            m_removeButton.addListener(this);
            m_moveUpButton.addListener(this);
            m_moveDownButton.addListener(this);
            m_globalBypassButton.addListener(this);
            m_bypassButton.addListener(this);
            m_openEditorButton.addListener(this);

            m_chainList.setRowHeight(24);

            refreshAvailablePlugins();
            m_chain = audio::theMasterPluginChain.getChainSnapshot();
            m_globalBypassButton.setToggleState(audio::theMasterPluginChain.isGlobalBypassed(), juce::dontSendNotification);
            m_chainList.updateContent();
            updateButtons();
            setSize(700, 420);
            startTimerHz(4);
        }

        PluginChainEditor::~PluginChainEditor()
        {
            stopTimer();
            m_openEditors.clear();
        }

        void PluginChainEditor::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void PluginChainEditor::resized()
        {
            auto bounds = getLocalBounds().reduced(12);
            auto titleRow = bounds.removeFromTop(28);
            m_globalBypassButton.setBounds(titleRow.removeFromRight(120));
            m_cpuLabel.setBounds(titleRow.removeFromRight(110));
            m_titleLabel.setBounds(titleRow);
            bounds.removeFromTop(6);

            auto topRow = bounds.removeFromTop(30);
            m_availablePluginsCombo.setBounds(topRow.removeFromLeft(topRow.getWidth() - 240));
            m_refreshButton.setBounds(topRow.removeFromLeft(80).reduced(4, 0));
            m_scanButton.setBounds(topRow.removeFromLeft(80).reduced(4, 0));
            m_addButton.setBounds(topRow.removeFromLeft(60).reduced(4, 0));

            bounds.removeFromTop(8);
            auto listArea = bounds.removeFromTop(bounds.getHeight() - 40);
            m_chainList.setBounds(listArea);

            auto bottomRow = bounds.removeFromTop(32);
            m_removeButton.setBounds(bottomRow.removeFromLeft(90).reduced(4, 0));
            m_moveUpButton.setBounds(bottomRow.removeFromLeft(60).reduced(4, 0));
            m_moveDownButton.setBounds(bottomRow.removeFromLeft(70).reduced(4, 0));
            m_bypassButton.setBounds(bottomRow.removeFromLeft(80).reduced(4, 0));
            m_openEditorButton.setBounds(bottomRow.removeFromLeft(100).reduced(4, 0));
        }

        int PluginChainEditor::getNumRows()
        {
            return static_cast<int>(m_chain.size());
        }

        void PluginChainEditor::paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected)
        {
            if (rowIsSelected)
            {
                g.fillAll(getLookAndFeel().findColour(jucyaudio::ui::accentColourId).withAlpha(0.2f));
            }

            if (rowNumber < 0 || rowNumber >= static_cast<int>(m_chain.size()))
            {
                return;
            }

            const auto &plugin = m_chain[static_cast<size_t>(rowNumber)];
            const auto name = plugin ? plugin->getName() : juce::String{"<null>"};
            g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
            g.drawText(name, 8, 0, width - 16, height, juce::Justification::centredLeft);
        }

        void PluginChainEditor::selectedRowsChanged(int /*lastRowSelected*/)
        {
            updateButtons();
        }

        void PluginChainEditor::buttonClicked(juce::Button *button)
        {
            if (button == &m_globalBypassButton)
            {
                audio::theMasterPluginChain.setGlobalBypassed(m_globalBypassButton.getToggleState());
                updateButtons();
            }
            else if (button == &m_refreshButton)
            {
                refreshAvailablePlugins();
            }
            else if (button == &m_scanButton)
            {
                PluginScanDialog::launch(this);
            }
            else if (button == &m_addButton)
            {
                addSelectedPlugin();
            }
            else if (button == &m_removeButton)
            {
                removeSelectedPlugin();
            }
            else if (button == &m_moveUpButton)
            {
                moveSelectedPlugin(-1);
            }
            else if (button == &m_moveDownButton)
            {
                moveSelectedPlugin(1);
            }
            else if (button == &m_bypassButton)
            {
                const int row = m_chainList.getSelectedRow();
                if (row < 0 || row >= static_cast<int>(m_chain.size()))
                {
                    return;
                }

                const auto &plugin = m_chain[static_cast<size_t>(row)];
                if (!plugin)
                {
                    return;
                }

                const bool bypassed = m_bypassButton.getToggleState();
                plugin->suspendProcessing(bypassed);
                audio::MasterPluginChainPersistence::saveToDatabase(m_chain);
                updateButtons();
            }
            else if (button == &m_openEditorButton)
            {
                openSelectedPluginEditor();
            }
        }

        void PluginChainEditor::refreshAvailablePlugins()
        {
            m_availablePluginsCombo.clear();
            m_availablePlugins.clear();

            auto &knownList = audio::thePluginManagerService.getKnownPluginList();
            auto types = knownList.getTypes();

            std::vector<juce::PluginDescription> sorted;
            sorted.reserve(static_cast<size_t>(types.size()));
            for (const auto &desc : types)
            {
                if (desc.pluginFormatName == "VST3")
                {
                    sorted.push_back(desc);
                }
            }

            std::sort(sorted.begin(), sorted.end(),
                [](const auto &a, const auto &b)
                {
                    return a.name.toStdString() < b.name.toStdString();
                });

            int id = 1;
            for (const auto &desc : sorted)
            {
                m_availablePlugins.push_back(desc);
                m_availablePluginsCombo.addItem(desc.name, id++);
            }
        }

        void PluginChainEditor::addSelectedPlugin()
        {
            const int selectedId = m_availablePluginsCombo.getSelectedId();
            if (selectedId <= 0 || selectedId > static_cast<int>(m_availablePlugins.size()))
            {
                return;
            }

            const auto &description = m_availablePlugins[static_cast<size_t>(selectedId - 1)];
            auto &formatManager = audio::thePluginManagerService.getFormatManager();

            const auto prep = audio::theMasterPluginChain.getPreparationState();
            const double sampleRate = prep.prepared ? prep.sampleRate : 44100.0;
            const int blockSize = prep.prepared ? prep.blockSize : 512;

            juce::String errorMessage;
            auto instance = formatManager.createPluginInstance(description, sampleRate, blockSize, errorMessage);
            if (!instance)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Plugin Error",
                    "Unable to load plugin:\n" + errorMessage);
                return;
            }

            juce::AudioProcessor::BusesLayout layout;
            const auto inputBusCount = instance->getBusCount(true);
            const auto outputBusCount = instance->getBusCount(false);
            layout.inputBuses.clearQuick();
            layout.outputBuses.clearQuick();
            for (int i = 0; i < inputBusCount; ++i)
            {
                layout.inputBuses.add(juce::AudioChannelSet::disabled());
            }
            for (int i = 0; i < outputBusCount; ++i)
            {
                layout.outputBuses.add(juce::AudioChannelSet::disabled());
            }
            if (inputBusCount > 0)
            {
                layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
            }
            if (outputBusCount > 0)
            {
                layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
            }
            if (!instance->setBusesLayout(layout))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Plugin Error",
                    "This plugin does not support stereo processing and cannot be added.");
                return;
            }

            std::shared_ptr<juce::AudioPluginInstance> sharedInstance{std::move(instance)};
            m_chain.push_back(sharedInstance);
            updateChain();
            m_chainList.updateContent();
            m_chainList.selectRow(static_cast<int>(m_chain.size()) - 1);
        }

        void PluginChainEditor::removeSelectedPlugin()
        {
            const int row = m_chainList.getSelectedRow();
            if (row < 0 || row >= static_cast<int>(m_chain.size()))
            {
                return;
            }

            const auto *plugin = m_chain[static_cast<size_t>(row)].get();
            closeEditorsForPlugin(plugin);
            m_chain.erase(m_chain.begin() + row);
            updateChain();
            m_chainList.updateContent();
            updateButtons();
        }

        void PluginChainEditor::moveSelectedPlugin(int direction)
        {
            const int row = m_chainList.getSelectedRow();
            if (row < 0)
            {
                return;
            }

            const int newRow = row + direction;
            if (newRow < 0 || newRow >= static_cast<int>(m_chain.size()))
            {
                return;
            }

            std::swap(m_chain[static_cast<size_t>(row)], m_chain[static_cast<size_t>(newRow)]);
            updateChain();
            m_chainList.updateContent();
            m_chainList.selectRow(newRow);
        }

        void PluginChainEditor::openSelectedPluginEditor()
        {
            const int row = m_chainList.getSelectedRow();
            if (row < 0 || row >= static_cast<int>(m_chain.size()))
            {
                return;
            }

            const auto &plugin = m_chain[static_cast<size_t>(row)];
            if (!plugin)
            {
                return;
            }

            auto &entry = m_openEditors[plugin.get()];
            if (auto *existing = entry.getComponent())
            {
                existing->toFront(true);
                return;
            }

            auto *window = PluginWindow::showForPlugin(plugin);
            entry = window;
        }

        void PluginChainEditor::updateChain()
        {
            audio::theMasterPluginChain.setChain(m_chain);
            audio::MasterPluginChainPersistence::saveToDatabase(m_chain);
        }

        void PluginChainEditor::timerCallback()
        {
            const auto load = audio::theMasterPluginChain.getCpuLoad();
            const auto percent = juce::jlimit(0.0f, 9.99f, load) * 100.0f;
            m_cpuLabel.setText("CPU " + juce::String(percent, 1) + "%", juce::dontSendNotification);
        }

        void PluginChainEditor::updateButtons()
        {
            const int row = m_chainList.getSelectedRow();
            const bool hasSelection = row >= 0 && row < static_cast<int>(m_chain.size());
            m_removeButton.setEnabled(hasSelection);
            m_openEditorButton.setEnabled(hasSelection);
            m_moveUpButton.setEnabled(hasSelection && row > 0);
            m_moveDownButton.setEnabled(hasSelection && row + 1 < static_cast<int>(m_chain.size()));
            m_bypassButton.setEnabled(hasSelection);
            m_globalBypassButton.setEnabled(true);
            m_globalBypassButton.setToggleState(audio::theMasterPluginChain.isGlobalBypassed(), juce::dontSendNotification);

            if (hasSelection)
            {
                const auto &plugin = m_chain[static_cast<size_t>(row)];
                const bool bypassed = plugin ? plugin->isSuspended() : false;
                m_bypassButton.setToggleState(bypassed, juce::dontSendNotification);
            }
            else
            {
                m_bypassButton.setToggleState(false, juce::dontSendNotification);
            }
        }

        void PluginChainEditor::closeEditorsForPlugin(const juce::AudioPluginInstance *plugin)
        {
            if (plugin == nullptr)
            {
                return;
            }

            auto it = m_openEditors.find(plugin);
            if (it != m_openEditors.end())
            {
                if (auto *window = it->second.getComponent())
                {
                    window->closeButtonPressed();
                }
                m_openEditors.erase(it);
            }
        }
    } // namespace ui
} // namespace jucyaudio
