#pragma once

#include <Database/Includes/Constants.h>
#include <Database/TrackLibrary.h>
#include <juce_gui_basics/juce_gui_basics.h>

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
        class EditWorkingSetMetaDataDialog : public juce::Component, private juce::Timer
        {
        public:
            /**
             * @brief Callback function type for when the dialog is closed.
             * @param nameChanged True if the name was changed and saved, false otherwise.
             */
            using OnDialogFinished = std::function<void(bool nameChanged, std::string_view newName)>;

            /**
             * @brief Construct a new Working Set Meta Data Editor Dialog object
             *
             * @param workingSetInfo The info object for the working set to be edited.
             * @param onFinishedCallback The callback to be invoked when the dialog is closed.
             */
            EditWorkingSetMetaDataDialog(const WorkingSetInfo &workingSetInfo, OnDialogFinished onFinishedCallback);
            ~EditWorkingSetMetaDataDialog() override = default;

            void paint(juce::Graphics &g) override;
            void resized() override;
            void parentHierarchyChanged() override;
            void timerCallback() override;

        private:
            //==============================================================================
            // Data
            WorkingSetInfo m_workingSetInfo;
            OnDialogFinished m_onFinishedCallback;

            // UI Components
            juce::Label m_titleLabel;
            juce::Label m_nameLabel;
            juce::TextEditor m_nameEditor;

            juce::Label m_statsLabel;
            juce::Label m_statsValueLabel;

            juce::TextButton m_saveButton;
            juce::TextButton m_cancelButton;

            // Private Methods
            void saveChanges();
            void closeDialog(bool changed);

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditWorkingSetMetaDataDialog)
        };
    } // namespace ui
} // namespace jucyaudio