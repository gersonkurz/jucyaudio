from __future__ import annotations
from pydantic import BaseModel, Field
from datetime import datetime
from typing import Optional, List, Dict, Any
from enum import Enum

class Track(BaseModel):
    track_id: int
    folder_id: int
    album_id: Optional[int] = None
    filename: str
    artist: Optional[str] = None
    title: Optional[str] = None
    bpm: Optional[float] = 0.0
    duration: float = 0.0  # In milliseconds
    bitrate: int = 0
    sample_rate: int = 0
    bit_depth: int = 0
    format: Optional[str] = None
    play_count: int = 0
    rating: int = 0
    status: str = "unknown"
    is_duplicate: bool = False
    file_hash: Optional[str] = None
    intro_end: Optional[float] = 0.0
    outro_start: Optional[float] = 0.0

class Folder(BaseModel):
    folder_id: int
    parent_id: Optional[int] = None
    name: str
    root_path: str
    actual_path: Optional[str] = None

class Album(BaseModel):
    album_id: int
    name: str
    artist: Optional[str] = None
    year: Optional[int] = None
    bitrate: Optional[int] = None

class Mix(BaseModel):
    mix_id: int
    name: str
    timestamp: datetime
    track_count: int
    total_length: float  # In milliseconds
    source_ws_id: Optional[int] = None
    status: str = "New"
    exported_at: Optional[datetime] = None
    export_folder: Optional[str] = None

class MixTrack(BaseModel):
    mix_id: int
    track_id: int
    order_in_mix: int
    cue_start: float = 0.0
    cue_end: float = 0.0
    attach_from: float = 0.0
    attach_to: float = 0.0
    # Additional data stored in JSON field 'mix_data'
    # can be expanded here as needed.
