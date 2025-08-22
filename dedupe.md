# JucyAudio - Duplicate Detection System Design

## Overview

The duplicate detection system provides automated, configuration-based duplicate management across three contexts: Library, Working Sets, and Mixes. The system prioritizes safety and efficiency, with increasingly conservative defaults based on the destructiveness of the operation.

## Configuration (Settings Dialog)

### Duplicate Detection Tab

**Detection Method:**
- **Exact Match**: Artist + Album + Title + Duration (default)
- **Fuzzy Match**: Allows minor variations in metadata
- **Audio Fingerprint**: (Future) Detects same audio regardless of tags

**Match Tolerances** (for Fuzzy Match):
- Duration difference: ±2 seconds (configurable)
- BPM difference: ±1 BPM (configurable)
- Text similarity: 85% minimum (configurable)

**Quality Preferences:**
- Always keep highest bitrate version (default: ON)
- Prefer lossless formats (FLAC/WAV) over lossy (default: ON)
- Keep version with most complete metadata (default: ON)
- Preserve tracks that are used in mixes (default: ON)
- Priority when quality is similar: Newest file/Oldest file/Largest file

**Safe Mode:**
- Never auto-remove from Library - always review (default: ON, not changeable)
- Require confirmation for Working Sets (default: OFF)
- Require confirmation for Mixes (default: OFF)

## Three Operational Contexts

### 1. Working Set Duplicates (Least Destructive)

**Operation:** Removes duplicate track references from working set
- No dialog if "Require confirmation for Working Sets" is unchecked
- Uses configured detection method and quality preferences
- Shows status: "Removed X duplicate tracks from working set"
- Non-destructive - only removes references, not actual files

### 2. Mix Duplicates (Medium Risk)

**Operation:** Removes duplicate tracks from mix timeline
- No dialog if "Require confirmation for Mixes" is unchecked
- Preserves first occurrence, removes subsequent ones
- Optional: Auto-adjust remaining track positions to close gaps
- Shows status: "Removed X duplicate tracks from mix"

### 3. Library Duplicates (Most Critical)

**Operation:** Three-phase process for maximum safety

**Phase 1 - Detection:**
- Scans library using configured detection method
- Groups duplicates by quality score

**Phase 2 - Review (ALWAYS REQUIRED):**
- Shows simplified dialog listing what will be kept vs marked
- No complex interaction needed - just review and confirm
- Can export list for external review

**Phase 3 - Marking:**
- Duplicates marked in database (not deleted)
- Can be hidden from library view
- Physical deletion is a separate future feature

## Quality Scoring Algorithm

```cpp
int calculateQualityScore(const TrackInfo& track) {
    int score = 0;
    
    // Base score from format and bitrate
    if (track.format == "FLAC" || track.format == "WAV") 
        score = 10000;
    else if (track.format == "MP3") 
        score = track.bitrate;
    else if (track.format == "AAC") 
        score = track.bitrate * 1.1; // Slight AAC preference
    
    // Bonus points for quality indicators
    if (track.sampleRate >= 48000) score += 100;
    if (track.bitDepth >= 24) score += 100;
    if (hasCompleteMetadata(track)) score += 50;
    if (isUsedInMix(track)) score += 500; // Strong preference
    
    return score;
}
```

## Database Schema Additions

```sql
-- Track duplicate status and relationships
ALTER TABLE Tracks ADD COLUMN is_duplicate BOOLEAN DEFAULT 0;
ALTER TABLE Tracks ADD COLUMN duplicate_of_track_id INTEGER;
ALTER TABLE Tracks ADD COLUMN quality_score INTEGER;
ALTER TABLE Tracks ADD COLUMN metadata_completeness FLOAT; -- 0.0 to 1.0

-- Index for efficient duplicate queries
CREATE INDEX idx_tracks_duplicate ON Tracks(is_duplicate, duplicate_of_track_id);
```

## Duplicate Detection Key Generation

### Exact Match Key
```cpp
string createExactKey(const TrackInfo& track) {
    return format("{}/{}/{}/{}/{}",
        track.artist_name,
        track.album_title, 
        track.title,
        track.bpm.value_or(0),
        track.duration);
}
```

### Fuzzy Match Key
```cpp
string createFuzzyKey(const TrackInfo& track, const Settings& settings) {
    return format("{}/{}/{}/{}/{}",
        normalizeText(track.artist_name),
        normalizeText(track.album_title),
        normalizeText(track.title),
        roundToBPMTolerance(track.bpm, settings.bpmTolerance),
        roundToDurationTolerance(track.duration, settings.durationTolerance));
}
```

## User Interface Integration

### Working Set Context Menu
- "Remove Duplicates" - immediate action based on settings

### Mix Editor Menu
- "Clean Up Duplicates" - immediate action based on settings

### Library/Data View Menu
- "Find Library Duplicates..." - always shows review dialog

### Status Bar Feedback
- Clear messages about what was done
- Undo option where applicable (working sets/mixes)

## Safety Features

1. **Conservative Defaults:** All destructive operations require confirmation initially
2. **Library Protection:** Library duplicates are never auto-removed, only marked
3. **Mix Preservation:** Tracks used in mixes get bonus quality points
4. **Undo Support:** Working set and mix operations can be undone
5. **Export Option:** Can export duplicate list for external review
6. **Mark-Only System:** Physical deletion is a separate, future operation

## Future Enhancements

- **Import-Time Detection:** Warn when importing duplicates
- **Smart Grouping:** Recognize versions (Radio Edit, Club Mix, etc.)
- **Archive Mode:** Move duplicates to archive folder vs deletion
- **Audio Fingerprinting:** Chromaprint or similar for audio-based matching
- **Batch Operations:** Process multiple working sets/mixes at once
- **Duplicate Prevention:** Option to reject duplicate imports entirely

## Implementation Priority

1. **Phase 1:** Settings UI + Exact match for working sets
2. **Phase 2:** Library duplicate review dialog + marking system
3. **Phase 3:** Fuzzy matching algorithm
4. **Phase 4:** Mix duplicate removal
5. **Phase 5:** Audio fingerprinting (if needed)