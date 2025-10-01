#pragma once

#include <Database/UndoManager.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>

namespace
{
    std::mutex undoMutex;
}

namespace jucyaudio
{
    namespace database
    {
        constexpr size_t MAX_UNDO_STACK_SIZE = 100;

        bool UndoManager::canUndo(MixId mixId) const
        {
            auto item = m_perMixStates.find(mixId);
            if (item == m_perMixStates.end())
                return false;

            // must at least have one state to fallback to
            return item->second.undoOperations.size() > 1;
        }

        bool UndoManager::canRedo() const
        {
            return false;
        }

        bool UndoManager::undo(MixId mixId)
        {
            assert(canUndo(mixId));

            spdlog::info("UndoManager: undo()");
            std::lock_guard<std::mutex> lock{undoMutex};
            auto item = m_perMixStates.find(mixId);
            if (item == m_perMixStates.end())
            {
                spdlog::warn("UndoManager: No recorded states found for mix ID {}", mixId);
                return false;
            }
            // the last state is the one we are currently at, we need to go back one more
            auto& mixState = item->second;
            auto it = mixState.undoOperations.end();
            --it; // now at the current state
            auto currentState = *it;
            --it; // now at the previous state
            auto previousState = *it;
            if (previousState == nullptr)
            {
                spdlog::warn("UndoManager: No previous state to revert to for mix ID {}", mixId);
                return false;
            }
            // this is here to prevent recursion as createOrUpdateMix calls back into recordMixChange
            m_undoOperationInProgress = true;
            theTrackLibrary.getMixManager().createOrUpdateMix(previousState->mixInfo, previousState->tracks);
            m_undoOperationInProgress = false;
            mixState.redoOperations.push_back(currentState); // move current to redo stack
            mixState.undoOperations.pop_back();              // remove current state from undo stack
            spdlog::info("After undo, state for mixId has {} undo ops, {} redo ops", mixState.undoOperations.size(), mixState.redoOperations.size());

            // done with this undo operation
            return true;
        }

        void UndoManager::recordMixChange(ExtendedMixInfo &&after)
        {
            if (m_undoOperationInProgress)
            {
                spdlog::warn("UndoManager: Ignoring recordMixChange during an undo operation");
            }
            else
            {
                spdlog::info("UndoManager: recordMixChange()");

                std::lock_guard<std::mutex> lock{undoMutex};

                // in the stack, record a new state of the mix after an operation is complete.
                const auto mixId = after.mixInfo.mixId;
                if (after.mixInfo.mixId)
                {
                    auto item = m_perMixStates.find(mixId);
                    if (item != m_perMixStates.end())
                    {
                        auto &mixState = item->second;
                        mixState.undoOperations.push_back(new ExtendedMixInfo{std::move(after)});

                        // trim undo stack if it gets too big
                        while (mixState.undoOperations.size() > MAX_UNDO_STACK_SIZE)
                        {
                            delete mixState.undoOperations.front();
                            mixState.undoOperations.pop_front();
                        }
                        
                        // any new operation clears the redo stack
                        for(const auto p: item->second.redoOperations)
                        {
                            delete p;
                        }
                        item->second.redoOperations.clear();
                        spdlog::info(
                            "UndoManager: After recordMixChange, state for mixId has {} undo ops, {} redo ops", mixState.undoOperations.size(), mixState.redoOperations.size());
                    }
                    else
                    {
                        // by definition, new stack is small enough
                        m_perMixStates[mixId] = {{new ExtendedMixInfo{std::move(after)}}, {}};
                        spdlog::info("UndoManager: After recordMixChange, state for mixId has 1 undo ops, 0 redo ops");
                    }
                }
            }
        }
        
        bool UndoManager::redo()
        {
            return false;
        }

        UndoManager theUndoManager;
    } // namespace database
} // namespace jucyaudio