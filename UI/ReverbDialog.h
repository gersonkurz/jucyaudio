#pragma once

#include "ReverbComponent.h"
#include "SingletonDialog.h"
#include <Database/Includes/ITrackDatabase.h>
#include <functional>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief Dialog window for the reverb effect
         */
        class ReverbDialog : public SingletonDialog<ReverbDialog>
        {
        public:
            ReverbDialog(database::ITrackDatabase *trackDb,
                std::function<void(const audio::model::ReverbSettings &)> onSettingsChanged,
                const audio::model::ReverbSettings &initialSettings = {});
            ~ReverbDialog() override;

            /**
             * @brief Get current reverb settings from the dialog
             */
            audio::model::ReverbSettings getCurrentSettings() const;

            /**
             * @brief Load settings into the dialog
             */
            void loadSettings(const audio::model::ReverbSettings &settings);

            /**
             * @brief Shows the reverb dialog
             * @param centreAroundComponent Component to center the dialog around
             * @param trackDb Database for preset management
             * @param onSettingsChanged Callback when settings change
             */
            static void showReverbDialog(juce::Component *centreAroundComponent,
                database::ITrackDatabase *trackDb,
                std::function<void(const audio::model::ReverbSettings &)> onSettingsChanged,
                const audio::model::ReverbSettings &initialSettings = {});

        protected:
            void closeButtonPressed() override;
            bool escapeKeyPressed() override;

        private:
            std::unique_ptr<ReverbComponent> m_reverbComponent;
            std::function<void(const audio::model::ReverbSettings &)> m_onSettingsChanged;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbDialog)
        };
    } // namespace ui
} // namespace jucyaudio