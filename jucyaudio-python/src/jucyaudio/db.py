import sqlite3
import os
import json
from datetime import datetime
from loguru import logger
from typing import List, Optional, Any, Dict, Generator
from pathlib import Path

from .models import Track, Folder, Album, Mix, MixTrack

class JucyAudioDB:
    def __init__(self, db_path: str):
        self.db_path = db_path
        if not os.path.exists(db_path):
            raise FileNotFoundError(f"JucyAudio database not found at {db_path}")
        self._connection = None

    def connect(self):
        if not self._connection:
            self._connection = sqlite3.connect(self.db_path)
            self._connection.row_factory = sqlite3.Row
        return self._connection

    def close(self):
        if self._connection:
            self._connection.close()
            self._connection = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def query(self, sql: str, params: tuple = ()) -> Generator[sqlite3.Row, None, None]:
        cursor = self.connect().cursor()
        cursor.execute(sql, params)
        for row in cursor:
            yield row

    def execute(self, sql: str, params: tuple = ()) -> int:
        conn = self.connect()
        cursor = conn.cursor()
        cursor.execute(sql, params)
        conn.commit()
        return cursor.lastrowid

    def get_track(self, track_id: int) -> Optional[Track]:
        for row in self.query("SELECT * FROM Tracks WHERE track_id = ?", (track_id,)):
            return self._row_to_track(row)
        return None

    def list_tracks(self, limit: int = 100) -> List[Track]:
        return [self._row_to_track(row) for row in self.query("SELECT * FROM Tracks LIMIT ?", (limit,))]

    def get_folder(self, folder_id: int) -> Optional[Folder]:
        for row in self.query("SELECT * FROM Folders WHERE folder_id = ?", (folder_id,)):
            return Folder(**dict(row))
        return None

    def get_mix(self, mix_id: int) -> Optional[Mix]:
        for row in self.query("SELECT * FROM Mixes WHERE mix_id = ?", (mix_id,)):
            return self._row_to_mix(row)
        return None

    def get_mix_tracks(self, mix_id: int) -> List[MixTrack]:
        tracks = []
        for row in self.query("SELECT * FROM MixTracks WHERE mix_id = ? ORDER BY order_in_mix", (mix_id,)):
            data = dict(row)
            # MixTracks stores cue/attach data in a JSON blob named 'mix_data'
            mix_data_json = data.pop('mix_data', '{}')
            try:
                mix_data = json.loads(mix_data_json)
                data.update(mix_data)
            except json.JSONDecodeError:
                logger.error(f"Failed to decode mix_data for mix {mix_id} track {data['track_id']}")
            
            tracks.append(MixTrack(**data))
        return tracks

    def insert_mix(self, name: str, timestamp_ms: int, track_count: int, total_length_ms: int, 
                   source_ws_id: Optional[int] = None, status: str = "Exported", 
                   export_folder: Optional[str] = None) -> int:
        sql = """
        INSERT INTO Mixes (name, timestamp, track_count, total_length, source_ws_id, status, exported_at, export_folder)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """
        return self.execute(sql, (name, timestamp_ms, track_count, total_length_ms, source_ws_id, status, timestamp_ms, export_folder))

    def insert_mix_track(self, mix_id: int, track_id: int, order_in_mix: int, mix_data: dict):
        sql = "INSERT INTO MixTracks (mix_id, track_id, order_in_mix, mix_data) VALUES (?, ?, ?, ?)"
        self.execute(sql, (mix_id, track_id, order_in_mix, json.dumps(mix_data)))

    def get_or_create_export_folder(self, name: str) -> str:
        # Check if exists
        for row in self.query("SELECT name FROM ExportFolders WHERE name = ?", (name,)):
            return row['name']
        
        # Create it
        timestamp = int(datetime.now().timestamp() * 1000)
        # Get next display order
        order = 1
        for row in self.query("SELECT COALESCE(MAX(display_order), 0) + 1 as next_order FROM ExportFolders"):
            order = row['next_order']
            
        self.execute("INSERT INTO ExportFolders (name, display_order, created_at) VALUES (?, ?, ?)", 
                     (name, order, timestamp))
        return name

    def get_working_set_id(self, name: str) -> Optional[int]:
        for row in self.query("SELECT ws_id FROM WorkingSets WHERE name = ?", (name,)):
            return row['ws_id']
        return None

    def insert_working_set(self, name: str) -> int:
        timestamp = int(datetime.now().timestamp() * 1000)
        return self.execute("INSERT INTO WorkingSets (name, timestamp, sort_order) VALUES (?, ?, ?)", (name, timestamp, "Manual"))

    def _row_to_track(self, row: sqlite3.Row) -> Track:
        d = dict(row)
        return Track(**d)

    def _row_to_mix(self, row: sqlite3.Row) -> Mix:
        d = dict(row)
        if d.get('timestamp'):
            d['timestamp'] = datetime.fromtimestamp(d['timestamp'] / 1000.0)
        if d.get('exported_at'):
            d['exported_at'] = datetime.fromtimestamp(d['exported_at'] / 1000.0)
        return Mix(**d)
