"""_summary_"""

from multiprocessing import connection
from pathlib import Path
import sqlite3
import unicodedata

from loguru import logger
from pydantic import BaseModel

def normalize_for_cache(input_str):
      """
      Normalizes a string for case-insensitive lookups, matching jucyaudio's normalizeForCache.

      Steps:
      1. NFC normalization (canonical composition)
      2. Case folding (ß → ss, uppercase → lowercase)
      3. Return normalized UTF-8 string
      """
      if not input_str:
          return input_str

      # Step 1: NFC normalization (canonical composition)
      normalized = unicodedata.normalize('NFC', input_str)

      # Step 2: Case folding (handles ß → ss and other case transformations)
      folded = normalized.casefold()

      return folded
  
class FolderRepresentation(BaseModel):
    """_summary_

    Args:
        BaseModel (_type_): _description_
    """
    folder_id: int
    parent_id: int | None
    name: str
    root_path: str
    actual_path: str | None

def read_db_files(db_path: str) -> set[str]:
    """_summary_

    Args:
        db_path (str): _description_
    """
    response: set[str] = set()
    connection = sqlite3.connect(db_path)
    # Make writes fast
    #connection.execute("PRAGMA journal_mode = WAL;")
    #connection.execute("PRAGMA synchronous = NORMAL;")
    #connection.execute("PRAGMA temp_store = MEMORY;")

    cursor = connection.cursor()
    cursor.execute("SELECT folder_id, parent_id, name, root_path, actual_path FROM Folders")
    rows = cursor.fetchall()

    # Build folder lookup
    lookup_folder_from_id: dict[int, FolderRepresentation] = {}
    for row in rows:
        folder_id, parent_id, name, root_path, actual_path = row
        folder = FolderRepresentation(
            folder_id=folder_id,
            parent_id=parent_id,
            name=name,
            root_path=root_path,
            actual_path=actual_path
        )
        lookup_folder_from_id[folder_id] = folder

    update_items: list[tuple[str, int]] = []

    for folder in lookup_folder_from_id.values():
        if folder.actual_path is None:
            # Determine the full path
            path_parts = []
            current_folder: FolderRepresentation | None = folder
            while current_folder is not None:
                assert isinstance(current_folder, FolderRepresentation)
                part = current_folder.actual_path if current_folder.actual_path else current_folder.name
                path_parts.append(part)
                if current_folder.parent_id and current_folder.parent_id in lookup_folder_from_id:
                    current_folder = lookup_folder_from_id[current_folder.parent_id]
                else:
                    current_folder = None
            full_path = Path(folder.root_path, *reversed(path_parts))

            if full_path.exists() and full_path.is_dir():
                folder.actual_path = str(full_path)
                update_items.append((folder.actual_path, folder.folder_id))

            else:
                logger.warning("Folder path does not exist or is not a directory: {}", full_path)
    with connection:
        connection.executemany(
            "UPDATE Folders SET actual_path = ? WHERE folder_id = ?;", update_items)
        connection.execute("COMMIT;")
        logger.info("Updated {} folder paths in DB", len(update_items))

    connection.close()

    logger.info("Read a total of {:,} files from DB", len(lookup_folder_from_id))
    return response

def read_files_from(folder: str) -> set[str]:
    """_summary_

    Args:
        folder (str): _description_
    """
    root = Path(folder)
    response: set[str] = set()

    # All files (recursive)
    for path in root.rglob("*"):
        if path.is_file():
            response.add(str(path.absolute()))
            
    logger.info("Read a total of {:,} files from {}", len(response), folder)
    return response


if __name__ == "__main__":
    #physical_files = read_files_from("D:\\MP3\\DARKGAZE")
    db_files = read_db_files("C:\\Users\\GersonKurz\\AppData\\Roaming\\jucyaudioApp_Dev\\jucyaudio_library_dev.sqlite")
