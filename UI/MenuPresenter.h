#pragma once

#include <UI/MenuManager.h>

namespace jucyaudio
{
    namespace ui
    {
        // This class acts as the "Adapter" between our clean MenuManager model
        // and the inheritance-based JUCE framework APIs.
        class MenuPresenter : public juce::ApplicationCommandTarget, public juce::MenuBarModel
        {
        public:
            MenuPresenter(juce::ApplicationCommandManager &commandManager);

            juce::MenuBarModel *getMenuBarModel() 
            {
                return this;
            }

            MenuManager &getManager()
            {
                return m_menuManager;
            }

            // The deferred initialization step.
            void registerCommands();

            // --- ApplicationCommandTarget Overrides ---
            ApplicationCommandTarget *getNextCommandTarget() override;
            void getAllCommands(juce::Array<juce::CommandID> &commands) override;
            void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo &result) override;
            bool perform(const InvocationInfo &info) override;

            // --- MenuBarModel Overrides ---
            juce::StringArray getMenuBarNames() override;
            juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String &menuName) override;
            void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

        private:
            void addItemsToMenu(juce::PopupMenu &menu, const std::vector<MenuItem> &items);
            void addSubMenusToMenu(juce::PopupMenu &menu, const std::vector<Menu> &subMenus);

            juce::ApplicationCommandManager &m_commandManager;
            MenuManager m_menuManager;
        };
    } // namespace ui
} // namespace jucyaudio