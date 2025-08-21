#pragma once

#include "SingletonDialog.h"
#include "EqualizerComponent.h"
#include <Database/Includes/ITrackDatabase.h>
#include <functional>

namespace jucyaudio::ui
{
    /**
     * @brief Dialog window for the equalizer
     */
    class EqualizerDialog : public SingletonDialog<EqualizerDialog>
    {
    public:
        EqualizerDialog(database::ITrackDatabase* trackDb,
                       std::function<void(const audio::model::EQSettings&)> onSettingsChanged);
        ~EqualizerDialog() override;
        
        /**
         * @brief Get current EQ settings from the dialog
         */
        audio::model::EQSettings getCurrentSettings() const;
        
        /**
         * @brief Load settings into the dialog
         */
        void loadSettings(const audio::model::EQSettings& settings);
        
        /**
         * @brief Shows the equalizer dialog
         * @param centreAroundComponent Component to center the dialog around
         * @param trackDb Database for preset management
         * @param onSettingsChanged Callback when settings change
         */
        static void showEqualizerDialog(juce::Component* centreAroundComponent,
                                       database::ITrackDatabase* trackDb,
                                       std::function<void(const audio::model::EQSettings&)> onSettingsChanged);
        
    private:
        std::unique_ptr<EqualizerComponent> m_equalizerComponent;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqualizerDialog)
    };
}