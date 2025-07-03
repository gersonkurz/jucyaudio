
#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteFolderDatabase.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        FolderInfo SqliteFolderDatabase::getFolderInfoFromStatement(database::SqliteStatement &stmt) const
        {
            return FolderInfo{
                (FolderId)stmt.getInt64(0), // folder_id
                pathFromString(stmt.getText(1)),
                stmt.getInt32(2),                    // num_files
                stmt.getInt64(3),                    // total_bytes
                timestampFromInt64(stmt.getInt64(4)) // last_scanned
            };
        }

        bool SqliteFolderDatabase::getFolders(std::vector<FolderInfo> &folders) const
        {
            folders.clear();
            const std::string queryString = "SELECT * FROM Folders ORDER BY fs_path COLLATE NOCASE ASC;";
            database::SqliteStatement stmt{m_db, queryString};
            if (!stmt.isValid())
            {
                spdlog::error("getFolders{}: Failed to prepare SELECT statement. DB error: {}", __LINE__, m_db.getLastError());
                return false;
            }

            while (stmt.getNextResult())
            {
                folders.emplace_back(getFolderInfoFromStatement(stmt));
            }

            // After loop, check if getNextResult() returned false due to an error or SQLITE_DONE.
            // m_done is true in both cases. m_db.getLastError() will contain message on error.
            if (!stmt.isQueryEmpty() /* this means m_done is false, only possible if loop never ran due to immediate error */ ||
                (stmt.isQueryEmpty() && !m_db.getLastError().empty()) /* m_done is true, but there was an error */)
            {
                spdlog::error("getFolders{}: Error during query execution or loop termination. DB error: {}", __LINE__, m_db.getLastError());
                return false;
            }
            return true;
        }

        bool SqliteFolderDatabase::addFolder(FolderInfo &folder)
        {
            assert(folder.folderId <= 0 && "Folder ID must be empty for addFolder");
            const std::string insertStmt = "INSERT INTO Folders (fs_path, num_files, total_bytes, last_scanned) "
                                           "VALUES (?, ?, ?, ?);";

            database::SqliteStatement stmt{m_db, insertStmt};
            if (!stmt.isValid())
            {
                spdlog::error("addFolder: Failed to prepare INSERT statement. DB error: {}", m_db.getLastError());
                return false;
            }

            bool ok = true;
            ok = stmt.addParam(pathToString(folder.path));
            ok &= stmt.addParam(folder.numFiles);
            ok &= stmt.addParam(folder.totalSizeBytes);
            ok &= stmt.addParam(timestampToInt64(folder.lastScannedTime));
            if (!ok || !stmt.execute())
            {
                spdlog::error("addFolder: Failed to execute UPDATE statement. DB error: {}", m_db.getLastError());
                return false;
            }
            folder.folderId = m_db.getLastInsertRowId(); // Set the folderId to the last inserted ID
            return true;
        }

        bool SqliteFolderDatabase::removeFolder(FolderId folderIdToRemove)
        {
            spdlog::warn("removeAllFolders: This operation will remove all watched folders from the database. Proceed with "
                         "caution.");

            database::SqliteStatement stmt{m_db, "DELETE FROM Folders WHERE folder_id = ?;"};
            if (!stmt.isValid())
            {
                spdlog::error("removeFolder: Failed to prepare DELETE statement. DB error: {}", m_db.getLastError());
                return false;
            }
            if (!stmt.addParam(folderIdToRemove))
            {
                spdlog::error("removeFolder: Failed to bind folder_id parameter. DB error: {}", m_db.getLastError());
                return false;
            }
            stmt.execute(); // Execute the DELETE statement
            if (stmt.isQueryEmpty() && !m_db.getLastError().empty())
            {
                spdlog::error("removeAllFolders: Error during query execution. DB error: {}", m_db.getLastError());
                return false;
            }
            return true;
        }

        bool SqliteFolderDatabase::removeAllFolders()
        {
            spdlog::warn("removeAllFolders: This operation will remove all watched folders from the database.");

            database::SqliteStatement stmt{m_db, "DELETE FROM Folders;"};
            if (!stmt.isValid())
            {
                spdlog::error("removeAllFolders: Failed to prepare DELETE statement. DB error: {}", m_db.getLastError());
                return false;
            }
            stmt.execute(); // Execute the DELETE statement
            if (stmt.isQueryEmpty() && !m_db.getLastError().empty())
            {
                spdlog::error("removeAllFolders: Error during query execution. DB error: {}", m_db.getLastError());
                return false;
            }
            return true;
        }

        bool SqliteFolderDatabase::updateFolder(const FolderInfo &folder)
        {
            assert(folder.folderId > 0 && "Folder ID must be set for updateFolder");
            const std::string updateStmt = "UPDATE Folders SET "
                                           "fs_path = ?, "
                                           "num_files = ?, "
                                           "total_bytes = ?, "
                                           "last_scanned = ? "
                                           "WHERE folder_id = ?;";
            database::SqliteStatement stmt{m_db, updateStmt};
            if (!stmt.isValid())
            {
                spdlog::error("updateFolder: Failed to prepare UPDATE statement. DB error: {}", m_db.getLastError());
                return false;
            }

            bool ok = true;
            ok = stmt.addParam(pathToString(folder.path));
            ok &= stmt.addParam(folder.numFiles);
            ok &= stmt.addParam(folder.totalSizeBytes);
            ok &= stmt.addParam(timestampToInt64(folder.lastScannedTime));
            ok &= stmt.addParam(folder.folderId);
            if (!ok || !stmt.execute())
            {
                spdlog::error("updateFolder: Failed to execute UPDATE statement. DB error: {}", m_db.getLastError());
                return false;
            }
            return true;
        }

    } // namespace database
} // namespace jucyaudio