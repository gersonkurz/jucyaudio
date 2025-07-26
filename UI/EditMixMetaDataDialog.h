#pragma once

#include <UI/MetaDataEditorDialogBase.h>
#include <Database/Includes/MixInfo.h>
#include <Database/TrackLibrary.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        /**
         * @class EditMixMetaDataDialog
         * @brief A dialog for viewing and editing the metadata of a Mix.
         *
         * This component displays statistics about a mix (track count, total duration, creation date)
         * and allows the user to rename it.
         */
        class EditMixMetaDataDialog : public MetaDataEditorDialogBase
        {
        public:
            /**
             * @brief Construct a new Mix Meta Data Editor Dialog object
             *
             * @param mixInfo The info object for the mix to be edited.
             * @param onFinishedCallback The callback to be invoked when the dialog is closed.
             */
            EditMixMetaDataDialog(const MixInfo &mixInfo, OnDialogFinished onFinishedCallback);
            ~EditMixMetaDataDialog() override = default;

        protected:
            bool performRename(const std::string& newName) override;
            std::string getErrorMessage() const override;
            std::string getCurrentName() const override;

        private:
            MixInfo m_mixInfo;
            
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditMixMetaDataDialog)
        };
    } // namespace ui
} // namespace jucyaudio