#pragma once

#include <UI/MetaDataEditorDialogBase.h>
#include <Database/Includes/WorkingSetInfo.h>
#include <Database/TrackLibrary.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        /**
         * @class EditWorkingSetMetaDataDialog
         * @brief A dialog for viewing and editing the metadata of a Working Set.
         *
         * This component displays statistics about a working set (track count, total duration, creation date)
         * and allows the user to rename it.
         */
        class EditWorkingSetMetaDataDialog : public MetaDataEditorDialogBase
        {
        public:
            /**
             * @brief Construct a new Working Set Meta Data Editor Dialog object
             *
             * @param workingSetInfo The info object for the working set to be edited.
             * @param onFinishedCallback The callback to be invoked when the dialog is closed.
             */
            EditWorkingSetMetaDataDialog(const WorkingSetInfo &workingSetInfo, OnDialogFinished onFinishedCallback);
            ~EditWorkingSetMetaDataDialog() override = default;

            void resized() override;

        protected:
            bool performRename(const std::string& newName) override;
            std::string getErrorMessage() const override;
            bool hasAdditionalChanges() const override;

        private:
            WorkingSetInfo m_workingSetInfo;
            int m_initialMixNumber;

            // Additional UI components for mix number
            juce::Label m_mixNumberLabel;
            juce::TextEditor m_mixNumberEditor;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditWorkingSetMetaDataDialog)
        };
    } // namespace ui
} // namespace jucyaudio