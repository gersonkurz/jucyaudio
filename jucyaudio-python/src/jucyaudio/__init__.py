from __future__ import annotations
import os
import re
from typing import List, Optional, Dict
from pathlib import Path
from loguru import logger

from .db import JucyAudioDB
from .models import Track, Folder, Album, Mix, MixTrack

class FastLibrary:
    def __init__(self, ja: JucyAudio):
        self.ja = ja
        self.skeleton_map: Dict[str, List[Track]] = {}
        self._load_all()

    def _normalize(self, filename: str) -> str:
        # Strip extension, lowercase, and remove EVERYTHING except a-z and 0-9
        name = os.path.splitext(filename)[0].lower()
        return re.sub(r'[^a-z0-9]', '', name)

    def _load_all(self):
        logger.info("Loading entire library into memory for fast lookup...")
        count = 0
        for row in self.ja.db.query("SELECT * FROM Tracks"):
            track = self.ja.db._row_to_track(row)
            skel = self._normalize(track.filename)
            if skel not in self.skeleton_map:
                self.skeleton_map[skel] = []
            self.skeleton_map[skel].append(track)
            count += 1
        logger.success(f"Loaded {count:,} tracks into memory.")

    def find_track(self, filename: str) -> Optional[Track]:
        skel = self._normalize(filename)
        
        # 1. Exact skeleton match
        matches = self.skeleton_map.get(skel)
        if matches:
            return matches[0] # Return first match for now
            
        # 2. Fuzzy prefix match (handle cases where M3U has trailing garbage/missing chars)
        # This is slower but only happens for misses. With 128GB we could optimize this 
        # further with a Trie if needed, but let's start with a simple check.
        if len(skel) > 10:
            for db_skel, db_tracks in self.skeleton_map.items():
                if skel in db_skel or db_skel in skel:
                    return db_tracks[0]
                    
        return None

class JucyAudio:
    def __init__(self, db_path: Optional[str] = None):
        if db_path is None:
            # Correct path for JucyAudio on Windows: Local AppData
            local_appdata = os.environ.get("LOCALAPPDATA", "")
            db_path = os.path.join(local_appdata, "jucyaudio", "jucyaudio.db")
        
        self.db = JucyAudioDB(db_path)
        self._folder_cache: Dict[int, Folder] = {}
        self._fast_lib: Optional[FastLibrary] = None

    def get_fast_library(self) -> FastLibrary:
        if self._fast_lib is None:
            self._fast_lib = FastLibrary(self)
        return self._fast_lib

    def get_track(self, track_id: int) -> Optional[Track]:
        return self.db.get_track(track_id)

    def get_full_path(self, track: Track) -> Optional[Path]:
        """Resolves the full physical path of a track."""
        folder = self.get_folder(track.folder_id)
        if not folder:
            return None
        
        # Use actual_path if available, otherwise reconstruct from hierarchy
        if folder.actual_path:
            return Path(folder.actual_path) / track.filename
            
        # Fallback: climb up the tree
        parts = [track.filename, folder.name]
        current = folder
        while current.parent_id:
            parent = self.get_folder(current.parent_id)
            if not parent:
                break
            parts.append(parent.name)
            current = parent
        
        return Path(current.root_path, *reversed(parts))

    def get_folder(self, folder_id: int) -> Optional[Folder]:
        if folder_id not in self._folder_cache:
            folder = self.db.get_folder(folder_id)
            if folder:
                self._folder_cache[folder_id] = folder
        return self._folder_cache.get(folder_id)

    def list_mixes(self) -> List[Mix]:
        mixes = []
        for row in self.db.query("SELECT * FROM Mixes ORDER BY timestamp DESC"):
            mixes.append(self.db._row_to_mix(row))
        return mixes

    def get_mix(self, mix_id: int) -> Optional[Mix]:
        return self.db.get_mix(mix_id)

    def get_tracks_in_mix(self, mix_id: int) -> List[tuple[MixTrack, Track]]:
        """Returns a list of (MixTrack info, Track metadata) for a given mix."""
        mix_tracks = self.db.get_mix_tracks(mix_id)
        result = []
        for mt in mix_tracks:
            track = self.get_track(mt.track_id)
            if track:
                result.append((mt, track))
        return result

    def find_tracks_by_title(self, title: str) -> List[Track]:
        tracks = []
        for row in self.db.query("SELECT * FROM Tracks WHERE title LIKE ?", (f"%{title}%",)):
            tracks.append(self.db._row_to_track(row))
        return tracks
