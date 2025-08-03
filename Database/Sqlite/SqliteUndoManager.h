#pragma once

#include <Database/Includes/IUndoManager.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <stack>
#include <unordered_map>
#include <atomic>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief SQLite-based implementation of the undo manager.
         * 
         * This class uses the MixUndoHistory table to persist undo operations,
         * allowing undo/redo to work across application restarts. It maintains
         * an in-memory redo stack per mix for efficient redo operations.
         */
        class SqliteUndoManager : public IUndoManager
        {
        public:
            explicit SqliteUndoManager(SqliteDatabase& db);
            ~SqliteUndoManager() override = default;

            int64_t beginOperation() override;

            void recordMixTrackChange(MixId mixId, TrackId trackId,
                                    const MixTrack* oldTrack,
                                    const MixTrack* newTrack,
                                    int64_t operationId) override;

            void recordMixInfoChange(MixId mixId,
                                   const MixInfo* oldInfo,
                                   const MixInfo* newInfo,
                                   int64_t operationId) override;

            bool undo(MixId mixId, std::function<void()> onComplete = nullptr) override;
            bool redo(MixId mixId, std::function<void()> onComplete = nullptr) override;

            bool canUndo(MixId mixId) const override;
            bool canRedo(MixId mixId) const override;

            void clearHistory(MixId mixId) override;

        private:
            /**
             * @brief Represents a single undoable operation.
             */
            struct UndoRecord
            {
                int64_t undoId;
                int64_t operationId;       // Groups related changes
                std::string operationType;  // INSERT, UPDATE, DELETE
                std::string tableName;      // MixTracks or Mixes
                int64_t recordId;          // track_id or mix_id
                std::string oldState;      // JSON
                std::string newState;      // JSON
            };

            /**
             * @brief Converts a MixTrack to JSON string for storage.
             */
            std::string mixTrackToJson(const MixTrack& track) const;

            /**
             * @brief Converts a MixInfo to JSON string for storage.
             */
            std::string mixInfoToJson(const MixInfo& info) const;

            /**
             * @brief Reconstructs a MixTrack from JSON string.
             */
            MixTrack mixTrackFromJson(const std::string& json) const;

            /**
             * @brief Reconstructs a MixInfo from JSON string.
             */
            MixInfo mixInfoFromJson(const std::string& json) const;

            /**
             * @brief Adds an undo record to the database.
             */
            bool addUndoRecord(MixId mixId, int64_t operationId, const std::string& operationType,
                             const std::string& tableName, int64_t recordId,
                             const std::string& oldState, const std::string& newState);

            /**
             * @brief Gets all records for a given operation.
             */
            std::vector<UndoRecord> getOperationRecords(int64_t operationId) const;

            /**
             * @brief Applies an undo record (performs the reverse operation).
             */
            bool applyUndoRecord(const UndoRecord& record);

            /**
             * @brief Applies a redo record (re-applies the original operation).
             */
            bool applyRedoRecord(const UndoRecord& record);

            SqliteDatabase& m_db;
            
            /**
             * @brief Gets the current stack position for a mix.
             */
            int64_t getCurrentStackPosition(MixId mixId) const;
            
            /**
             * @brief Updates the stack position for a mix.
             */
            void updateStackPosition(MixId mixId, int64_t operationId);
            
            /**
             * @brief Deletes all undo records beyond the current stack position.
             */
            void deleteRecordsBeyondStackPosition(MixId mixId);
            
            // Operation ID generator
            mutable std::atomic<int64_t> m_nextOperationId{0};
        };

    } // namespace database
} // namespace jucyaudio