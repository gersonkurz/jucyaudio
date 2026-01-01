#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <typeindex>
#include <unordered_map>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * Base class for dialogs that should only have one instance open at a time.
         * Provides non-blocking behavior while preventing multiple instances.
         *
         * Usage:
         * 1. Derive your dialog from SingletonDialog<YourDialogClass>
         * 2. Call showSingletonDialog() to show the dialog
         * 3. The dialog will automatically handle singleton behavior and cleanup
         */
        template <typename Derived> class SingletonDialog : public juce::DialogWindow
        {
        public:
            /**
             * Shows the dialog, or brings existing instance to front if already open
             * @param centreAroundComponent Optional component to center around
             * @param args Additional constructor arguments for the derived class
             */
            template <typename... Args> static void showSingletonDialog(juce::Component *centreAroundComponent, Args &&...args)
            {
                // Check if an instance already exists
                if (auto *existing = getCurrentInstance())
                {
                    existing->toFront(true);
                    existing->grabKeyboardFocus();
                    return;
                }

                // Create new instance but apply look and feel FIRST
                auto *dialog = new Derived{std::forward<Args>(args)...};
                setCurrentInstance(dialog);

                // Apply the look and feel from the parent component BEFORE any children are created
                if (centreAroundComponent)
                {
                    dialog->setLookAndFeel(&centreAroundComponent->getLookAndFeel());
                }

                // Configure common settings
                dialog->setAlwaysOnTop(true);
                dialog->setUsingNativeTitleBar(true);

                if (centreAroundComponent)
                {
                    dialog->centreAroundComponent(centreAroundComponent, dialog->getWidth(), dialog->getHeight());
                }
                else
                {
                    dialog->centreWithSize(dialog->getWidth(), dialog->getHeight());
                }

                // Show without blocking
                dialog->setVisible(true);
                dialog->toFront(true);
                dialog->grabKeyboardFocus();
            }

            /**
             * Shows the dialog with default centering (screen center)
             */
            template <typename... Args> static void showSingletonDialog(Args &&...args)
            {
                showSingletonDialog(nullptr, std::forward<Args>(args)...);
            }

        protected:
            SingletonDialog(const juce::String &title, const juce::Colour &backgroundColour, bool hasCloseButton)
                : DialogWindow{title, backgroundColour, hasCloseButton}
            {
            }

            ~SingletonDialog() override
            {
                // Clear the singleton reference for this type
                clearCurrentInstance();
            }

            void closeButtonPressed() override
            {
                setVisible(false);

                // Delete after message loop processes this event
                // This ensures any pending events are handled before deletion
                juce::MessageManager::callAsync(
                    [this]()
                    {
                        // Restore focus to main window
                        restoreFocusToMainWindow();
                        delete this;
                    });
            }

            bool escapeKeyPressed() override
            {
                closeButtonPressed();
                return true; // Indicate that we've handled the key press
            }

        protected:
            static Derived *getCurrentInstance()
            {
                const juce::ScopedLock lock{s_instanceLock};
                auto it = s_instances.find(std::type_index(typeid(Derived)));
                if (it != s_instances.end())
                {
                    return static_cast<Derived *>(it->second);
                }
                return nullptr;
            }

        private:
            // Static storage for singleton instances, one per derived type
            static inline std::unordered_map<std::type_index, void *> s_instances;
            static inline juce::CriticalSection s_instanceLock;

            static void setCurrentInstance(Derived *instance)
            {
                const juce::ScopedLock lock{s_instanceLock};
                s_instances[std::type_index(typeid(Derived))] = instance;
            }

            void clearCurrentInstance()
            {
                const juce::ScopedLock lock{s_instanceLock};
                auto it = s_instances.find(std::type_index(typeid(Derived)));
                if (it != s_instances.end() && it->second == this)
                {
                    s_instances.erase(it);
                }
            }

            void restoreFocusToMainWindow()
            {
                // Find the main application window (non-dialog window)
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                {
                    if (auto *window = juce::TopLevelWindow::getTopLevelWindow(i))
                    {
                        // Skip dialog windows, find the main window
                        if (!dynamic_cast<juce::DialogWindow *>(window))
                        {
                            window->toFront(true);
                            window->grabKeyboardFocus();
                            break;
                        }
                    }
                }
            }
        };

        /**
         * Alternative base class for modal-style dialogs that block interaction
         * with the main window but don't freeze the UI
         */
        template <typename Derived> class ModalStyleDialog : public SingletonDialog<Derived>
        {
        protected:
            using Base = SingletonDialog<Derived>;

            ModalStyleDialog(const juce::String &title, const juce::Colour &backgroundColour, bool hasCloseButton)
                : Base{title, backgroundColour, hasCloseButton}
            {
            }

            void visibilityChanged() override
            {
                if (this->isVisible())
                {
                    // When becoming visible, disable main window interaction
                    setMainWindowEnabled(false);
                }
                else
                {
                    // When hiding, re-enable main window interaction
                    setMainWindowEnabled(true);
                }
                Base::visibilityChanged();
            }

        private:
            void setMainWindowEnabled(bool enabled)
            {
                // Find and enable/disable the main window
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                {
                    if (auto *window = juce::TopLevelWindow::getTopLevelWindow(i))
                    {
                        // Skip dialog windows
                        if (!dynamic_cast<juce::DialogWindow *>(window))
                        {
                            window->setEnabled(enabled);
                            if (enabled)
                            {
                                window->toFront(true);
                                window->grabKeyboardFocus();
                            }
                            break;
                        }
                    }
                }
            }
        };

        /**
         * Helper class for showing component-based dialogs with singleton behavior.
         * This is for dialogs that are just Components shown in a DialogWindow.
         */
        class SingletonComponentDialog
        {
        public:
            /**
             * Shows a component in a dialog window with singleton behavior
             * @param dialogId Unique identifier for this dialog type
             * @param title Dialog window title
             * @param component Component to show (will be owned by the dialog)
             * @param launchOptions Additional launch options to configure
             * @param modal If true, disables main window interaction while open
             */
            static void showComponent(const juce::String &dialogId,
                const juce::String &title,
                juce::Component *component,
                juce::DialogWindow::LaunchOptions &launchOptions,
                bool modal = false)
            {
                // Check if this dialog is already open AND still valid
                if (auto *window = getValidDialogWindow(dialogId))
                {
                    // Bring existing dialog to front
                    window->toFront(true);
                    window->grabKeyboardFocus();
                    delete component; // Clean up the component we were going to show
                    return;
                }

                // Configure launch options
                launchOptions.content.setOwned(component);
                launchOptions.dialogTitle = title;
                launchOptions.escapeKeyTriggersCloseButton = true;
                launchOptions.useNativeTitleBar = true;

                auto *dialogWindow = launchOptions.launchAsync();
                if (!dialogWindow)
                {
                    delete component; // Clean up if launch fails
                    return;
                }

                registerDialog(dialogId, dialogWindow);
                dialogWindow->setAlwaysOnTop(true);

                if (modal)
                {
                    // For modal dialogs, use enterModalState with a callback for cleanup.
                    // This is the robust JUCE way to handle modality and cleanup.
                    struct DialogCleanupCallback : public juce::ModalComponentManager::Callback
                    {
                        juce::String id;
                        DialogCleanupCallback(const juce::String &dialogId)
                            : id(dialogId)
                        {
                        }
                        void modalStateFinished(int /*returnCode*/) override
                        {
                            SingletonComponentDialog::unregisterDialog(id);
                        }
                    };

                    dialogWindow->enterModalState(true, new DialogCleanupCallback{dialogId}, true);
                }
                else
                {
                    // For non-modal dialogs, we still need to know when they close.
                    // A ComponentListener is appropriate here.
                    struct DialogCleanupListener : public juce::ComponentListener
                    {
                        juce::String m_id;
                        DialogCleanupListener(const juce::String &dialogId)
                            : m_id{dialogId}
                        {
                        }
                        void componentBeingDeleted(juce::Component &) override
                        {
                            SingletonComponentDialog::unregisterDialog(m_id);
                            delete this; // The listener must delete itself.
                        }
                    };
                    dialogWindow->addComponentListener(new DialogCleanupListener{dialogId});
                }
            }

            /**
             * Convenience overload for simple dialogs
             */
            static void showComponent(const juce::String &dialogId,
                const juce::String &title,
                juce::Component *component,
                juce::Component *centreAroundComponent = nullptr,
                bool modal = false)
            {
                juce::DialogWindow::LaunchOptions options;
                options.componentToCentreAround = centreAroundComponent;
                showComponent(dialogId, title, component, options, modal);
            }

        private:
            static inline std::unordered_map<juce::String, juce::Component::SafePointer<juce::DialogWindow>> s_openDialogs;
            static inline juce::CriticalSection s_dialogLock;

            static bool isDialogOpen(const juce::String &dialogId)
            {
                const juce::ScopedLock lock{s_dialogLock};
                return s_openDialogs.find(dialogId) != s_openDialogs.end();
            }

            static juce::DialogWindow *getDialogWindow(const juce::String &dialogId)
            {
                const juce::ScopedLock lock{s_dialogLock};
                auto it = s_openDialogs.find(dialogId);
                return it != s_openDialogs.end() ? it->second.getComponent() : nullptr;
            }

            static juce::DialogWindow *getValidDialogWindow(const juce::String &dialogId)
            {
                const juce::ScopedLock lock{s_dialogLock};
                auto it = s_openDialogs.find(dialogId);
                if (it == s_openDialogs.end())
                {
                    return nullptr;
                }

                // SafePointer automatically handles validity checking
                auto *window = it->second.getComponent();

                if (window == nullptr)
                {
                    // Window is dead but still in our map - clean it up
                    s_openDialogs.erase(it);
                    return nullptr;
                }

                return window;
            }

            static void registerDialog(const juce::String &dialogId, juce::DialogWindow *window)
            {
                const juce::ScopedLock lock{s_dialogLock};
                s_openDialogs[dialogId] = window;
            }

            static void unregisterDialog(const juce::String &dialogId)
            {
                const juce::ScopedLock lock{s_dialogLock};
                s_openDialogs.erase(dialogId);
            }

            static void disableMainWindow()
            {
                // Find and disable the main window
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                {
                    if (auto *window = juce::TopLevelWindow::getTopLevelWindow(i))
                    {
                        if (!dynamic_cast<juce::DialogWindow *>(window))
                        {
                            window->setEnabled(false);
                            break;
                        }
                    }
                }
            }

            static void enableMainWindow()
            {
                // Find and enable the main window
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                {
                    if (auto *window = juce::TopLevelWindow::getTopLevelWindow(i))
                    {
                        if (!dynamic_cast<juce::DialogWindow *>(window))
                        {
                            window->setEnabled(true);
                            window->toFront(true);
                            window->grabKeyboardFocus();
                            break;
                        }
                    }
                }
            }
        };
    } // namespace ui
} // namespace jucyaudio