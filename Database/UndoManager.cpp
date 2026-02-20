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

        UndoManager::~UndoManager()
        {
            std::lock_guard<std::mutex> lock{undoMutex};
            m_perMixStates.clear();  // unique_ptr handles cleanup automatically
        }

        bool UndoManager::canUndo(MixId mixId) const
        {
            auto item = m_perMixStates.find(mixId);
            if (item == m_perMixStates.end())
                return false;

            // must at least have one state to fallback to
            return item->second.undoOperations.size() > 1;
        }

        bool UndoManager::canRedo(MixId mixId) const
        {
            auto item = m_perMixStates.find(mixId);
            return item != m_perMixStates.end() && !item->second.redoOperations.empty();
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
            --it; // now at the previous state
            auto* previousState = it->get();
            if (previousState == nullptr)
            {
                spdlog::warn("UndoManager: No previous state to revert to for mix ID {}", mixId);
                return false;
            }
            // this is here to prevent recursion as createOrUpdateMix calls back into recordMixChange
            m_undoOperationInProgress = true;
            theTrackLibrary.getMixManager().createOrUpdateMix(previousState->mixInfo, previousState->tracks);
            m_undoOperationInProgress = false; //-V519

            // Move current state to redo stack, then remove from undo stack
            mixState.redoOperations.push_back(std::move(mixState.undoOperations.back()));
            mixState.undoOperations.pop_back();
            spdlog::info("After undo, state for mixId has {} undo ops, {} redo ops", mixState.undoOperations.size(), mixState.redoOperations.size());

            // done with this undo operation
            return true;
        }

        void UndoManager::recordMixChange(ExtendedMixInfo &&after)
        {
            // Check before locking — undo()/redo() already hold undoMutex when they
            // call createOrUpdateMix(), which calls back into this function.
            // std::mutex is not re-entrant, so locking again would deadlock.
            if (m_undoOperationInProgress.load())
            {
                spdlog::debug("UndoManager: Ignoring recordMixChange during undo/redo operation");
                return;
            }

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
                    mixState.undoOperations.push_back(std::make_unique<ExtendedMixInfo>(std::move(after)));

                    // trim undo stack if it gets too big
                    while (mixState.undoOperations.size() > MAX_UNDO_STACK_SIZE)
                    {
                        mixState.undoOperations.pop_front();  // unique_ptr handles cleanup
                    }

                    // any new operation clears the redo stack
                    mixState.redoOperations.clear();  // unique_ptr handles cleanup
                    spdlog::info(
                        "UndoManager: After recordMixChange, state for mixId has {} undo ops, {} redo ops", mixState.undoOperations.size(), mixState.redoOperations.size());
                }
                else
                {
                    // by definition, new stack is small enough
                    MixState newState;
                    newState.undoOperations.push_back(std::make_unique<ExtendedMixInfo>(std::move(after)));
                    m_perMixStates[mixId] = std::move(newState);
                    spdlog::info("UndoManager: After recordMixChange, state for mixId has 1 undo ops, 0 redo ops");
                }
            }
        }
        
        bool UndoManager::redo(MixId mixId)
        {
            assert(canRedo(mixId));

            spdlog::info("UndoManager: redo()");
            std::lock_guard<std::mutex> lock{undoMutex};
            auto item = m_perMixStates.find(mixId);
            if (item == m_perMixStates.end())
            {
                spdlog::warn("UndoManager: No recorded states found for mix ID {}", mixId);
                return false;
            }

            auto& mixState = item->second;
            if (mixState.redoOperations.empty())
            {
                spdlog::warn("UndoManager: No redo operations available for mix ID {}", mixId);
                return false;
            }

            // Get the state to restore from redo stack
            auto* redoState = mixState.redoOperations.back().get();
            if (redoState == nullptr)
            {
                spdlog::warn("UndoManager: Redo state is null for mix ID {}", mixId);
                return false;
            }

            // Restore the redo state
            m_undoOperationInProgress = true;
            theTrackLibrary.getMixManager().createOrUpdateMix(redoState->mixInfo, redoState->tracks);
            m_undoOperationInProgress = false; //-V519

            // Move redo state back to undo stack
            mixState.undoOperations.push_back(std::move(mixState.redoOperations.back()));
            mixState.redoOperations.pop_back();

            spdlog::info("After redo, state for mixId has {} undo ops, {} redo ops",
                mixState.undoOperations.size(), mixState.redoOperations.size());

            return true;
        }

        void UndoManager::clearHistory(MixId mixId)
        {
            std::lock_guard<std::mutex> lock{undoMutex};
            auto it = m_perMixStates.find(mixId);
            if (it != m_perMixStates.end())
            {
                m_perMixStates.erase(it);  // unique_ptr handles cleanup automatically
                spdlog::info("UndoManager: Cleared history for mix ID {}", mixId);
            }
        }

        UndoManager theUndoManager;
    } // namespace database
} // namespace jucyaudio