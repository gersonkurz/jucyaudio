#include "MenuManager.h"

namespace jucyaudio::ui
{
    void MenuManager::registerMenu(const std::string& menuName, std::vector<MenuItem> items)
    {
        Menu newMenu;
        newMenu.name = menuName;

        for (auto& item : items)
        {
            item.commandId = getNextCommandId();
            m_commandMap[item.commandId] = item;
            newMenu.items.push_back(std::move(item));
        }
        
        m_menus.push_back(std::move(newMenu));
    }

    void MenuManager::addSubMenu(const std::string& parentMenuName, const std::string& subMenuName, std::vector<MenuItem> items)
    {
        if (auto* parentMenu = findMenu(parentMenuName, m_menus))
        {
            Menu newSubMenu;
            newSubMenu.name = subMenuName;
            
            for (auto& item : items)
            {
                item.commandId = getNextCommandId();
                m_commandMap[item.commandId] = item;
                newSubMenu.items.push_back(std::move(item));
            }
            
            parentMenu->subMenus.push_back(std::move(newSubMenu));
        }
    }

    Menu* MenuManager::findMenu(const std::string& menuName, std::vector<Menu>& menus)
    {
        for (auto& menu : menus)
        {
            if (menu.name == menuName)
                return &menu;
            
            if (auto* foundInSub = findMenu(menuName, menu.subMenus))
                return foundInSub;
        }
        return nullptr;
    }
}