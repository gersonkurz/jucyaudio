#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/MixInfo.h>
#include <stack>

namespace jucyaudio
{
    namespace database
    {        
        struct MixState
        {
            std::list<ExtendedMixInfo *> undoOperations;

            // cleared after each new action
            std::list<ExtendedMixInfo *> redoOperations;
        };

        class UndoManager final
        {
        public:
            UndoManager() = default;
            ~UndoManager();
            UndoManager(const UndoManager &) = delete;
            UndoManager &operator=(const UndoManager &) = delete;
            UndoManager(UndoManager &&) = delete;
            UndoManager &operator=(UndoManager &&) = delete;

        public:
            bool canUndo(MixId mixId) const;
            bool canRedo(MixId mixId) const;
            bool undo(MixId mixId);
            bool redo(MixId mixId);

            // change recording operations
            void recordMixChange(ExtendedMixInfo &&after);

            // clear history for a specific mix
            void clearHistory(MixId mixId);

            // ensure we have a state recorded for this mix, if not, record it now
            bool isMixKnown(MixId mixId) const
            {
                return m_perMixStates.find(mixId) != m_perMixStates.end();
            }

        private:
            bool m_undoOperationInProgress{false};

            std::unordered_map<MixId, MixState> m_perMixStates; // Current known states of mixes
        };

        extern UndoManager theUndoManager;
    } // namespace database
} // namespace jucyaudio