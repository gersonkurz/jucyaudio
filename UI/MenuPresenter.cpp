#include "MenuPresenter.h"

namespace jucyaudio::ui
{
    MenuPresenter::MenuPresenter(juce::ApplicationCommandManager& commandManager)
        : m_commandManager(commandManager) {}

    void MenuPresenter::registerCommands()
    {
        m_commandManager.registerAllCommandsForTarget(this);
    }

    // --- ApplicationCommandTarget Implementation ---

    juce::ApplicationCommandTarget* MenuPresenter::getNextCommandTarget()
    {
        return nullptr;
    }

    void MenuPresenter::getAllCommands(juce::Array<juce::CommandID>& commands)
    {
        for (const auto& [id, item] : m_menuManager.getCommandMap())
            commands.add(id);
    }

    void MenuPresenter::getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result)
    {
        const auto& commandMap = m_menuManager.getCommandMap();
        if (auto it = commandMap.find(commandID); it != commandMap.end())
        {
            const auto& item = it->second;
            result.setInfo(item.name, item.description, "Application", 0);
            
            if (item.keyPress)
                result.addDefaultKeypress(item.keyPress->key, item.keyPress->mods);
            
            // Here you could add another lambda to the MenuItem struct for dynamic enabling/disabling
            result.setActive(true);
            
            if (item.isRadioButton && item.isTicked)
                result.setTicked(item.isTicked());
        }
    }

    bool MenuPresenter::perform(const InvocationInfo& info)
    {
        const auto& commandMap = m_menuManager.getCommandMap();
        if (auto it = commandMap.find(info.commandID); it != commandMap.end())
        {
            if (it->second.action)
            {
                it->second.action(); // Execute the stored lambda
                return true;
            }
        }
        return false;
    }

    // --- MenuBarModel Implementation ---

    juce::StringArray MenuPresenter::getMenuBarNames()
    {
        juce::StringArray names;
        for (const auto& menu : m_menuManager.getMenus())
            names.add(menu.name);
        return names;
    }

    juce::PopupMenu MenuPresenter::getMenuForIndex(int topLevelMenuIndex, const juce::String& /*menuName*/)
    {
        juce::PopupMenu menu;
        const auto& menus = m_menuManager.getMenus();
        if (topLevelMenuIndex >= 0 && static_cast<size_t>(topLevelMenuIndex) < menus.size())
        {
            const auto& menuModel = menus[topLevelMenuIndex];
            addItemsToMenu(menu, menuModel.items);
            addSubMenusToMenu(menu, menuModel.subMenus);
        }
        return menu;
    }

    void MenuPresenter::menuItemSelected(int /*menuItemID*/, int /*topLevelMenuIndex*/)
    {
        // This should not be called if all items are command-managed.
    }

    // --- Private Helper Methods ---

    void MenuPresenter::addItemsToMenu(juce::PopupMenu& menu, const std::vector<MenuItem>& items)
    {
        for (const auto& item : items)
        {
            // A name of "-" is a convention for a separator.
            if (item.name == "-")
                menu.addSeparator();
            else
                menu.addCommandItem(&m_commandManager, item.commandId);
        }
    }

    void MenuPresenter::addSubMenusToMenu(juce::PopupMenu& menu, const std::vector<Menu>& subMenus)
    {
        for (const auto& subMenuModel : subMenus)
        {
            juce::PopupMenu subMenu;
            addItemsToMenu(subMenu, subMenuModel.items);
            addSubMenusToMenu(subMenu, subMenuModel.subMenus); // Recursive call for nested submenus
            menu.addSubMenu(subMenuModel.name, subMenu);
        }
    }
}