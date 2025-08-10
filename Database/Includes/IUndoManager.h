#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/MixInfo.h>
#include <functional>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief Interface for managing undo/redo operations for mix editing.
         * 
         * This interface provides methods to record changes to mix data and
         * perform undo/redo operations. The implementation uses the database
         * to persist undo history, allowing undo to work across application
         * restarts.
         */
        class IUndoManager
        {
        public:
            virtual ~IUndoManager() = default;

            /**
             * @brief Begins a new operation group.
             * 
             * All subsequent record calls with this operation ID will be grouped
             * together and undone/redone as a single unit.
             * 
             * @return A unique operation ID to use for subsequent record calls
             */
            virtual int64_t beginOperation() = 0;

            /**
             * @brief Records a change to a MixTrack for undo purposes.
             * 
             * @param mixId The ID of the mix being edited
             * @param trackId The ID of the track being changed
             * @param oldTrack The track data before the change (for UPDATE/DELETE)
             * @param newTrack The track data after the change (for INSERT/UPDATE)
             * @param operationId The operation ID from beginOperation()
             */
            virtual void recordMixTrackChange(MixId mixId, TrackId trackId,
                                            const MixTrack* oldTrack,
                                            const MixTrack* newTrack,
                                            int64_t operationId) = 0;

            /**
             * @brief Records a change to Mix metadata for undo purposes.
             * 
             * @param mixId The ID of the mix being changed
             * @param oldInfo The mix info before the change (for UPDATE/DELETE)
             * @param newInfo The mix info after the change (for INSERT/UPDATE)
             * @param operationId The operation ID from beginOperation()
             */
            virtual void recordMixInfoChange(MixId mixId,
                                           const MixInfo* oldInfo,
                                           const MixInfo* newInfo,
                                           int64_t operationId) = 0;

            /**
             * @brief Performs an undo operation for the specified mix.
             * 
             * @param mixId The ID of the mix to undo changes for
             * @param onComplete Callback invoked after undo completes (for UI refresh)
             * @return true if undo was successful, false if no undo available or error
             */
            virtual bool undo(MixId mixId, std::function<void()> onComplete = nullptr) = 0;

            /**
             * @brief Performs a redo operation for the specified mix.
             * 
             * @param mixId The ID of the mix to redo changes for
             * @param onComplete Callback invoked after redo completes (for UI refresh)
             * @return true if redo was successful, false if no redo available or error
             */
            virtual bool redo(MixId mixId, std::function<void()> onComplete = nullptr) = 0;

            /**
             * @brief Checks if undo is available for the specified mix.
             * 
             * @param mixId The ID of the mix to check
             * @return true if at least one undo operation is available
             */
            virtual bool canUndo(MixId mixId) const = 0;

            /**
             * @brief Checks if redo is available for the specified mix.
             * 
             * @param mixId The ID of the mix to check
             * @return true if at least one redo operation is available
             */
            virtual bool canRedo(MixId mixId) const = 0;

            /**
             * @brief Clears all undo history for a specific mix.
             * 
             * @param mixId The ID of the mix to clear history for
             */
            virtual void clearHistory(MixId mixId) = 0;

            /**
             * @brief Sets the maximum number of undo operations to store for a mix.
             *        Older operations will be purged when the limit is exceeded.
             * @param limit The maximum number of operations. A value of 0 means no limit.
             */
            virtual void setMaxOperations(int limit) = 0;
        };

    } // namespace database
} // namespace jucyaudio