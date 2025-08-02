#include <Database/Sqlite/SqliteUndoManager.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/Sqlite/SqliteMixManager.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace jucyaudio
{
    namespace database
    {
        using json = nlohmann::json;

        SqliteUndoManager::SqliteUndoManager(SqliteDatabase& db)
            : m_db{db}
        {
            // Initialize operation ID from database max + 1
            SqliteStatement stmt{m_db, "SELECT MAX(operation_id) FROM MixUndoHistory;"};
            if (stmt.getNextResult() && !stmt.isNull(0))
            {
                m_nextOperationId = stmt.getInt64(0) + 1;
            }
            else
            {
                m_nextOperationId = 1;
            }
        }

        int64_t SqliteUndoManager::beginOperation()
        {
            return m_nextOperationId.fetch_add(1);
        }

        void SqliteUndoManager::recordMixTrackChange(MixId mixId, TrackId trackId,
                                                    const MixTrack* oldTrack,
                                                    const MixTrack* newTrack,
                                                    int64_t operationId)
        {
            spdlog::info("SqliteUndoManager::recordMixTrackChange called for mix {} track {} operation {}", 
                        mixId, trackId, operationId);
            
            std::string operationType;
            std::string oldState;
            std::string newState;

            if (!oldTrack && newTrack)
            {
                operationType = "INSERT";
                newState = mixTrackToJson(*newTrack);
            }
            else if (oldTrack && newTrack)
            {
                operationType = "UPDATE";
                oldState = mixTrackToJson(*oldTrack);
                newState = mixTrackToJson(*newTrack);
            }
            else if (oldTrack && !newTrack)
            {
                operationType = "DELETE";
                oldState = mixTrackToJson(*oldTrack);
            }
            else
            {
                spdlog::error("Invalid undo record: both old and new tracks are null");
                return;
            }

            // Clear redo stack for this mix when a new operation is recorded
            m_redoStacks[mixId] = std::stack<int64_t>();

            if (!addUndoRecord(mixId, operationId, operationType, "MixTracks", trackId, oldState, newState))
            {
                spdlog::error("Failed to record MixTrack change for undo");
            }
            else
            {
                spdlog::info("Successfully recorded {} operation for track {} in mix {}", operationType, trackId, mixId);
            }
        }

        void SqliteUndoManager::recordMixInfoChange(MixId mixId,
                                                   const MixInfo* oldInfo,
                                                   const MixInfo* newInfo,
                                                   int64_t operationId)
        {
            std::string operationType;
            std::string oldState;
            std::string newState;

            if (!oldInfo && newInfo)
            {
                operationType = "INSERT";
                newState = mixInfoToJson(*newInfo);
            }
            else if (oldInfo && newInfo)
            {
                operationType = "UPDATE";
                oldState = mixInfoToJson(*oldInfo);
                newState = mixInfoToJson(*newInfo);
            }
            else if (oldInfo && !newInfo)
            {
                operationType = "DELETE";
                oldState = mixInfoToJson(*oldInfo);
            }
            else
            {
                spdlog::error("Invalid undo record: both old and new mix info are null");
                return;
            }

            // Clear redo stack for this mix when a new operation is recorded
            m_redoStacks[mixId] = std::stack<int64_t>();

            if (!addUndoRecord(mixId, operationId, operationType, "Mixes", mixId, oldState, newState))
            {
                spdlog::error("Failed to record MixInfo change for undo");
            }
        }

        bool SqliteUndoManager::undo(MixId mixId, std::function<void()> onComplete)
        {
            const auto lastOpId = getLastOperationId(mixId);
            if (lastOpId == 0)
            {
                spdlog::info("No undo available for mix {}", mixId);
                return false;
            }

            spdlog::info("Undoing operation {} for mix {}", lastOpId, mixId);
            const auto records = getOperationRecords(lastOpId);
            if (records.empty())
            {
                spdlog::error("No records found for operation {}", lastOpId);
                return false;
            }

            SqliteTransaction transaction{m_db};
            if (!transaction)
            {
                spdlog::error("Failed to begin undo transaction");
                return false;
            }

            // Apply all undo records in reverse order
            for (auto it = records.rbegin(); it != records.rend(); ++it)
            {
                if (!applyUndoRecord(*it))
                {
                    spdlog::error("Failed to apply undo record {}", it->undoId);
                    transaction.rollback();
                    return false;
                }
            }

            // Remove all records for this operation from undo history
            SqliteStatement deleteStmt{m_db, "DELETE FROM MixUndoHistory WHERE operation_id = ?;"};
            deleteStmt.addParam(lastOpId);
            if (!deleteStmt.execute())
            {
                spdlog::error("Failed to delete undo records for operation {}", lastOpId);
                transaction.rollback();
                return false;
            }

            // Add operation ID to redo stack
            m_redoStacks[mixId].push(lastOpId);

            if (!transaction.commit())
            {
                spdlog::error("Failed to commit undo transaction");
                return false;
            }

            spdlog::info("Successfully undid operation {} for mix {} ({} changes)", lastOpId, mixId, records.size());
            
            if (onComplete)
            {
                onComplete();
            }

            return true;
        }

        bool SqliteUndoManager::redo(MixId mixId, std::function<void()> onComplete)
        {
            auto it = m_redoStacks.find(mixId);
            if (it == m_redoStacks.end() || it->second.empty())
            {
                spdlog::info("No redo available for mix {}", mixId);
                return false;
            }

            auto& redoStack = it->second;
            const auto operationId = redoStack.top();
            redoStack.pop();

            spdlog::info("Redoing operation {} for mix {}", operationId, mixId);
            
            // Note: We need to store the redo records somewhere when we undo
            // For now, let's re-read them from a temporary storage
            // This is a limitation - we'd need to enhance this to store redo data
            
            spdlog::error("Redo not fully implemented yet - needs redo record storage");
            return false;
        }

        bool SqliteUndoManager::canUndo(MixId mixId) const
        {
            return getLastOperationId(mixId) != 0;
        }

        bool SqliteUndoManager::canRedo(MixId mixId) const
        {
            auto it = m_redoStacks.find(mixId);
            return it != m_redoStacks.end() && !it->second.empty();
        }

        void SqliteUndoManager::clearHistory(MixId mixId)
        {
            SqliteStatement stmt{m_db, "DELETE FROM MixUndoHistory WHERE mix_id = ?;"};
            stmt.addParam(mixId);
            if (!stmt.execute())
            {
                spdlog::error("Failed to clear undo history for mix {}", mixId);
            }

            // Clear redo stack
            m_redoStacks.erase(mixId);
        }

        std::string SqliteUndoManager::mixTrackToJson(const MixTrack& track) const
        {
            json j;
            // Store the identity fields separately
            j["track_id"] = track.trackId;
            j["order_in_mix"] = track.orderInMix;
            j["mix_id"] = track.mixId;
            
            // Use the standard serializer format for the mix_data portion
            json mixData = track;  // This uses the standard to_json from MixInfo.h
            j["mix_data"] = mixData;
            
            return j.dump();
        }

        std::string SqliteUndoManager::mixInfoToJson(const MixInfo& info) const
        {
            json j;
            j["mix_id"] = info.mixId;
            j["name"] = info.name;
            j["timestamp"] = info.timestamp.time_since_epoch().count();
            j["track_count"] = info.numberOfTracks;
            j["total_duration"] = info.totalDuration.count();
            j["source_ws_id"] = info.source_ws_id;
            j["status"] = info.status;
            return j.dump();
        }

        MixTrack SqliteUndoManager::mixTrackFromJson(const std::string& jsonStr) const
        {
            MixTrack track;
            auto j = json::parse(jsonStr);
            
            // Extract identity fields
            track.trackId = j["track_id"];
            track.orderInMix = j["order_in_mix"];
            track.mixId = j["mix_id"];
            
            // Extract mix_data using the standard from_json deserializer
            if (j.contains("mix_data"))
            {
                json mixData = j["mix_data"];
                mixData.get_to(track);  // This uses the standard from_json from MixInfo.h
            }
            
            return track;
        }

        MixInfo SqliteUndoManager::mixInfoFromJson(const std::string& jsonStr) const
        {
            MixInfo info;
            auto j = json::parse(jsonStr);
            
            info.mixId = j["mix_id"];
            info.name = j["name"];
            info.timestamp = Timestamp_t{std::chrono::system_clock::duration{j["timestamp"]}};
            info.numberOfTracks = j["track_count"];
            info.totalDuration = Duration_t{j["total_duration"]};
            info.source_ws_id = j["source_ws_id"];
            info.status = j["status"];
            
            return info;
        }

        bool SqliteUndoManager::addUndoRecord(MixId mixId, int64_t operationId, const std::string& operationType,
                                            const std::string& tableName, int64_t recordId,
                                            const std::string& oldState, const std::string& newState)
        {
            SqliteStatement stmt{m_db, R"SQL(
                INSERT INTO MixUndoHistory (mix_id, operation_id, operation_type, table_name, record_id, old_state, new_state)
                VALUES (?, ?, ?, ?, ?, ?, ?);
            )SQL"};
            
            stmt.addParam(mixId);
            stmt.addParam(operationId);
            stmt.addParam(operationType);
            stmt.addParam(tableName);
            stmt.addParam(recordId);
            stmt.addNullableParam(oldState);
            stmt.addNullableParam(newState);
            
            return stmt.execute();
        }

        int64_t SqliteUndoManager::getLastOperationId(MixId mixId) const
        {
            SqliteStatement stmt{m_db, R"SQL(
                SELECT DISTINCT operation_id
                FROM MixUndoHistory
                WHERE mix_id = ?
                ORDER BY operation_id DESC
                LIMIT 1;
            )SQL"};
            
            stmt.addParam(mixId);
            
            if (stmt.getNextResult())
            {
                return stmt.getInt64(0);
            }
            
            return 0;
        }
        
        std::vector<SqliteUndoManager::UndoRecord> SqliteUndoManager::getOperationRecords(int64_t operationId) const
        {
            std::vector<UndoRecord> records;
            
            SqliteStatement stmt{m_db, R"SQL(
                SELECT undo_id, operation_id, operation_type, table_name, record_id, old_state, new_state
                FROM MixUndoHistory
                WHERE operation_id = ?
                ORDER BY undo_id ASC;
            )SQL"};
            
            stmt.addParam(operationId);
            
            while (stmt.getNextResult())
            {
                UndoRecord record;
                record.undoId = stmt.getInt64(0);
                record.operationId = stmt.getInt64(1);
                record.operationType = stmt.getText(2);
                record.tableName = stmt.getText(3);
                record.recordId = stmt.getInt64(4);
                if (!stmt.isNull(5))
                    record.oldState = stmt.getText(5);
                if (!stmt.isNull(6))
                    record.newState = stmt.getText(6);
                records.push_back(record);
            }
            
            return records;
        }

        bool SqliteUndoManager::applyUndoRecord(const UndoRecord& record)
        {
            spdlog::info("Applying undo for {} operation on {} table, record_id: {}", 
                        record.operationType, record.tableName, record.recordId);
            
            if (record.tableName == "MixTracks")
            {
                if (record.operationType == "INSERT")
                {
                    // Undo an insert by deleting
                    SqliteStatement stmt{m_db, "DELETE FROM MixTracks WHERE track_id = ?;"};
                    stmt.addParam(record.recordId);
                    return stmt.execute();
                }
                else if (record.operationType == "UPDATE")
                {
                    // Undo an update by restoring old state
                    auto oldTrack = mixTrackFromJson(record.oldState);
                    SqliteStatement stmt{m_db, R"SQL(
                        UPDATE MixTracks 
                        SET order_in_mix = ?, mix_data = ?
                        WHERE track_id = ?;
                    )SQL"};
                    stmt.addParam(oldTrack.orderInMix);
                    // Extract just the mix_data portion from the stored JSON
                    auto j = json::parse(record.oldState);
                    const auto mixDataJson = j["mix_data"].dump();
                    spdlog::info("Updating MixTracks with order_in_mix: {}, track_id: {}, mix_data: {}", 
                                oldTrack.orderInMix, record.recordId, mixDataJson);
                    stmt.addParam(mixDataJson);
                    stmt.addParam(record.recordId);
                    const bool success = stmt.execute();
                    if (success)
                    {
                        spdlog::info("Successfully updated MixTracks");
                    }
                    else
                    {
                        spdlog::error("Failed to update MixTracks");
                    }
                    return success;
                }
                else if (record.operationType == "DELETE")
                {
                    // Undo a delete by re-inserting
                    auto oldTrack = mixTrackFromJson(record.oldState);
                    SqliteStatement stmt{m_db, R"SQL(
                        INSERT INTO MixTracks (mix_id, track_id, order_in_mix, mix_data)
                        VALUES (?, ?, ?, ?);
                    )SQL"};
                    
                    stmt.addParam(oldTrack.mixId);
                    stmt.addParam(oldTrack.trackId);
                    stmt.addParam(oldTrack.orderInMix);
                    // Extract just the mix_data portion from the stored JSON
                    auto j = json::parse(record.oldState);
                    stmt.addParam(j["mix_data"].dump());
                    return stmt.execute();
                }
            }
            else if (record.tableName == "Mixes")
            {
                if (record.operationType == "UPDATE")
                {
                    // Undo an update by restoring old state
                    auto oldInfo = mixInfoFromJson(record.oldState);
                    SqliteStatement stmt{m_db, R"SQL(
                        UPDATE Mixes 
                        SET name = ?, timestamp = ?, track_count = ?, total_length = ?, 
                            source_ws_id = ?, status = ?
                        WHERE mix_id = ?;
                    )SQL"};
                    stmt.addParam(oldInfo.name);
                    stmt.addParam(oldInfo.timestamp.time_since_epoch().count());
                    stmt.addParam(oldInfo.numberOfTracks);
                    stmt.addParam(oldInfo.totalDuration.count());
                    stmt.addParam(oldInfo.source_ws_id);
                    stmt.addParam(oldInfo.status);
                    stmt.addParam(record.recordId);
                    return stmt.execute();
                }
                // INSERT and DELETE for Mixes would be more complex and less common
            }
            
            return false;
        }

        bool SqliteUndoManager::applyRedoRecord(const UndoRecord& record)
        {
            if (record.tableName == "MixTracks")
            {
                if (record.operationType == "INSERT")
                {
                    // Redo an insert by inserting again
                    auto newTrack = mixTrackFromJson(record.newState);
                    SqliteStatement stmt{m_db, R"SQL(
                        INSERT INTO MixTracks (mix_id, track_id, order_in_mix, mix_data)
                        VALUES (?, ?, ?, ?);
                    )SQL"};
                    
                    stmt.addParam(newTrack.mixId);
                    stmt.addParam(newTrack.trackId);
                    stmt.addParam(newTrack.orderInMix);
                    // Extract just the mix_data portion from the stored JSON
                    auto j = json::parse(record.newState);
                    stmt.addParam(j["mix_data"].dump());
                    return stmt.execute();
                }
                else if (record.operationType == "UPDATE")
                {
                    // Redo an update by applying new state
                    auto newTrack = mixTrackFromJson(record.newState);
                    SqliteStatement stmt{m_db, R"SQL(
                        UPDATE MixTracks 
                        SET order_in_mix = ?, mix_data = ?
                        WHERE track_id = ?;
                    )SQL"};
                    stmt.addParam(newTrack.orderInMix);
                    // Extract just the mix_data portion from the stored JSON
                    auto j = json::parse(record.newState);
                    stmt.addParam(j["mix_data"].dump());
                    stmt.addParam(record.recordId);
                    return stmt.execute();
                }
                else if (record.operationType == "DELETE")
                {
                    // Redo a delete by deleting again
                    SqliteStatement stmt{m_db, "DELETE FROM MixTracks WHERE track_id = ?;"};
                    stmt.addParam(record.recordId);
                    return stmt.execute();
                }
            }
            else if (record.tableName == "Mixes")
            {
                if (record.operationType == "UPDATE")
                {
                    // Redo an update by applying new state
                    auto newInfo = mixInfoFromJson(record.newState);
                    SqliteStatement stmt{m_db, R"SQL(
                        UPDATE Mixes 
                        SET name = ?, timestamp = ?, track_count = ?, total_length = ?, 
                            source_ws_id = ?, status = ?
                        WHERE mix_id = ?;
                    )SQL"};
                    stmt.addParam(newInfo.name);
                    stmt.addParam(newInfo.timestamp.time_since_epoch().count());
                    stmt.addParam(newInfo.numberOfTracks);
                    stmt.addParam(newInfo.totalDuration.count());
                    stmt.addParam(newInfo.source_ws_id);
                    stmt.addParam(newInfo.status);
                    stmt.addParam(record.recordId);
                    return stmt.execute();
                }
            }
            
            return false;
        }

    } // namespace database
} // namespace jucyaudio