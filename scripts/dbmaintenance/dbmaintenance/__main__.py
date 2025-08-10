"""Entry point for the dbmaintenance module."""

from __future__ import annotations
import os
import sqlite3
from datetime import datetime
from typing import Final
import typer
from loguru import logger

# Things to remember:
# - we should remove "root_path" from the database. it servers no real purpose



class DatabaseMaintenance:
    def __init__(self, db_path: str):
        self.db_path: Final[str] = db_path
        self.connection: sqlite3.Connection | None = None
        self.__flat_folder_lookup: dict[int, tuple[str, int]] = {}
        self.__folder_from_id: dict[int, str] = {}
        self.__id_from_folder: dict[str, int] = {}

    def __enter__(self) -> DatabaseMaintenance:
        self.connection = sqlite3.connect(self.db_path)

        return self
    
    def fix_mixes(self) -> None:
        if not self.connection:
            raise RuntimeError("Database connection is not established")

        cursor = self.connection.cursor()

        cursor.execute("SELECT mix_id, name FROM mixes")
        mixes = cursor.fetchall()
        logger.info("Got {:,} mix(es): {!r}", len(mixes), mixes)
        for mix_nr, mix_data in enumerate(mixes):
            mix_id, mix_name = mix_data
            logger.info("Processing mix {:,} of {:,}: {} with id {}", mix_nr + 1, len(mixes), mix_name, mix_id)

            cursor.execute("SELECT rowid, track_id, order_in_mix FROM mixtracks WHERE mix_id = ? ORDER BY order_in_mix", (mix_id, ))
            mix_tracks = cursor.fetchall()
            for index, mix_track in enumerate(mix_tracks):
                rowid, track_id, order_in_mix = mix_track
                if order_in_mix != index:
                    logger.info("Fixing mix track order for mix {}: {} ({} -> {})", mix_id, track_id, order_in_mix, index)
                    cursor.execute("UPDATE mixtracks SET order_in_mix = ? WHERE rowid = ?;", (index, rowid))

    def build_folders(self) -> None:
        """Create necessary folders for the database."""
        if not self.connection:
            raise RuntimeError("Database connection is not established")

        cursor = self.connection.cursor()

        try:
            query = "SELECT folder_id, parent_id, name FROM folders"
            cursor.execute("PRAGMA cache_size = 10000")  # Increase cache
            cursor.execute("PRAGMA temp_store = memory")  # Use memory for temp storage

            t0 = datetime.now()
            cursor.execute(query)

            # Fetch all at once (good for moderate datasets)
            folders = cursor.fetchall()
            logger.debug("Got {:,} folders in {}", len(folders), (datetime.now() - t0))

            # Process the folders
            for folder_id, parent_id, name in folders:
                self.__flat_folder_lookup[folder_id] = (name, parent_id)

                if parent_id:
                    path_segments: list[str] = []
                    current_id = folder_id
                    while True:
                        current_name, current_parent_id = self.__flat_folder_lookup[current_id]
                        path_segments.insert(0, current_name)
                        if current_parent_id is None or current_parent_id == 0:
                            break
                        current_id = current_parent_id
                    reconstructed_path = os.path.join(*path_segments)
                    assert reconstructed_path not in self.__id_from_folder, f"Path {reconstructed_path} already exists in lookup"
                    self.__folder_from_id[folder_id] = reconstructed_path
                    self.__id_from_folder[reconstructed_path] = folder_id

            logger.debug("Builtup lookup for {:,} folders in {}", len(folders), (datetime.now() - t0))

        except sqlite3.Error as e:
            logger.error(f"Database error while querying folders: {e}")
            raise
        finally:
            cursor.close()


    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: object | None
    ) -> None:
        if self.connection:
            self.connection.close()
            self.connection = None
        if exc_type is not None:
            logger.error(f"An error occurred: {exc_value}")

app = typer.Typer()

@app.command()
def main() -> None:
    """Main entry point for dbmaintenance."""
    logger.info("Starting dbmaintenance...")

    app_data = os.environ["APPDATA"]
    pathname = os.path.join(app_data, "jucyaudioApp_Dev", "jucyaudio_library_dev.sqlite")
    if not os.path.exists(pathname):
        logger.error(f"Database file does not exist: {pathname}")
        raise FileNotFoundError(f"Database file not found at {pathname}")

    with DatabaseMaintenance(pathname) as db_maintenance:
        # Perform database maintenance tasks here
        logger.info("Performing database maintenance tasks...")
        #db_maintenance.build_folders()
        db_maintenance.fix_mixes()
    typer.echo("Database maintenance tool completed!")


if __name__ == "__main__":
    app()