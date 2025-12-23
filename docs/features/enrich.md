# JucyAudio - AI-Powered Metadata Enrichment

**Last Updated:** 2025-10-27
**Status:** Design Phase

---

## Overview

This document describes the AI-powered metadata enrichment system for JucyAudio. This is a **standalone Python tool** (not part of the C++ application) that uses Large Language Models to enhance metadata quality in the database.

**Key Principle**: The enrichment tool is completely separate from the C++ application. It operates on the database directly, allowing users to run enrichment on-demand with full cost control. The C++ app provides manual tagging via the Virtual Tag System (see `vtags.md`), while this AI tool provides automated enrichment.

---

## Motivation

### The Metadata Quality Problem

Large music libraries (50k+ tracks) often suffer from poor metadata quality:

1. **Missing Genre Tags**: Most MP3 files have empty or generic genre tags ("Other", "Unknown")
2. **Inconsistent Naming**: Genre spellings vary ("Shoegaze" vs "shoegaze" vs "shoe-gaze")
3. **WAV File Paths**: Complex paths like `E:\AUS\part3\Teil 1\Taksi Inc- nachtschleife - Force Inc\01-a1.wav` contain metadata but lack structure
4. **Album Context Lost**: Tracks from the same album scattered across folders lose their connection
5. **Manual Curation Overhead**: Manually tagging 50k tracks is prohibitively time-consuming

### Why AI Enrichment?

Large Language Models excel at:
- **Music Knowledge**: Recognizing artists, albums, genres from minimal context
- **Path Parsing**: Understanding complex filesystem structures and extracting metadata
- **Pattern Recognition**: Identifying genre/mood/style from artist names and album titles
- **Consistency**: Applying uniform vocabulary across entire library

### Why External Tool?

Keeping AI enrichment as a separate Python script (not integrated into the C++ app) provides:
- **Cost Control**: User runs enrichment when desired, with explicit budget limits
- **Progressive Enhancement**: Library remains fully functional while enrichment happens over time
- **Flexibility**: Easy to update prompts, switch AI providers, or add new enrichment strategies
- **No Bloat**: Core C++ app stays lean without AI dependencies

---

## Architecture Overview

### Two Complementary Systems

**Manual Curation** (C++ App - see `vtags.md`):
- Virtual Tag System for folder-level tagging
- Tag Panel UI for rapid manual classification
- Untagged/Tagged navigation trees
- Real-time, instant feedback
- User has full control

**AI Enrichment** (Python Script - this document):
- Batch processing of albums and tracks
- Genre/mood/tag suggestions using Claude API
- WAV path intelligence for metadata extraction
- Cost-controlled, on-demand execution
- Writes to database, never modifies files

**Workflow**:
1. User imports music → C++ app scans files
2. User manually tags high-priority folders → Virtual Tag System
3. User runs AI enrichment script → Python tool processes remaining albums
4. User reviews AI suggestions → C++ app (future: approval UI)
5. Approved suggestions become Virtual Tags → Database updated

---

## AI Enrichment Tasks

### Task 1: Album Metadata Enrichment

**Purpose**: Add genre, mood, and style tags to albums in the database.

**Input** (from Albums table):
- Album artist (e.g., "Secret Shine")
- Album title (e.g., "Untouched")
- Optional: Year, folder path for context

**AI Processing**:
```python
def enrich_album_metadata(artist: str, title: str, year: Optional[int]) -> AlbumEnrichment:
    """
    Uses Claude API to identify:
    - Primary genre (e.g., "Shoegaze")
    - Secondary genres (e.g., ["Indie Rock", "Dream Pop"])
    - Moods (e.g., ["Atmospheric", "Melancholic"])
    - Descriptive tags (e.g., ["Reverb-heavy", "90s UK"])

    Returns structured JSON validated by Pydantic.
    """
```

**Output** (to Albums table):
```json
{
  "album_id": 12345,
  "genres": ["Shoegaze", "Indie Rock", "Dream Pop"],
  "moods": ["Atmospheric", "Melancholic", "Ethereal"],
  "tags": ["Reverb-heavy", "90s UK", "Creation Records era"],
  "confidence": 0.92
}
```

**Database Update**:
- Writes to `Albums.genres`, `Albums.moods`, `Albums.tags` (JSON arrays)
- Optionally creates VirtualTags and FolderVirtualTags entries (future integration)
- Sets `Albums.ai_enriched_at` timestamp

**Cost Control**:
- Process in batches of 100 albums
- Estimate: ~500 tokens per album (input + output)
- 100 albums ≈ 50k tokens ≈ $0.02 (Claude Sonnet 3.5)
- Configurable daily/monthly budget limits

### Task 2: WAV Path Intelligence

**Purpose**: Extract metadata from complex WAV file paths where ID3 tags are absent.

**Challenge**: WAV files from vinyl digitization often have paths like:
```
E:\AUS\part3\Teil 1\Taksi Inc- nachtschleife - Force Inc\01-a1.wav
E:\Musik\Krautrock\Can\1971 - Tago Mago\Side A - Track 01.wav
E:\DOWNLOADS\VA - Shoegaze Compilation (1992)\03 - Unknown Artist.wav
```

**AI Processing**:
```python
def parse_wav_path(full_path: str, duration: float) -> TrackMetadata:
    """
    Uses Claude API to extract:
    - Artist name from path components
    - Album title from parent folder
    - Track title from filename
    - Label/catalog number if present
    - Likely genre based on context

    Input includes:
    - Full filesystem path
    - Track duration (helps identify side/disc splits)
    - Parent folder structure

    Returns structured metadata with confidence score.
    """
```

**Example Parsing**:

Input:
```
Path: E:\AUS\part3\Teil 1\Taksi Inc- nachtschleife - Force Inc\01-a1.wav
Duration: 342.5 seconds
```

Output:
```json
{
  "artist": "Taksi Inc",
  "album": "nachtschleife",
  "title": "Track A1",
  "label": "Force Inc",
  "genre_hint": "Minimal Techno",
  "confidence": 0.85
}
```

**Database Update**:
- Updates `Tracks.artist_name`, `Tracks.title`, `Tracks.album_title`
- Clears `Tracks.needs_enrichment` flag
- Creates or links to Album entry
- Logs original path in `Tracks.original_path` for audit

**Cost Control**:
- Batch process WAV files by folder (shared context reduces tokens)
- Estimate: ~300 tokens per WAV file
- 1000 WAV files ≈ 300k tokens ≈ $0.12

### Task 3: Missing Metadata Enhancement

**Purpose**: Fill gaps in track metadata using contextual clues.

**Scenarios**:
- MP3 with artist but no album
- Track with filename but no title
- Album with tracks but no genre
- Partial ID3 tags needing completion

**AI Processing**:
```python
def enhance_track_metadata(partial_info: dict) -> TrackMetadata:
    """
    Given partial metadata, infer missing fields:

    Examples:
    - Artist="Slowdive", no album → Likely albums: Souvlaki, Pygmalion, etc.
    - Filename="03_soma.mp3", no title → Title: "Soma"
    - Album="Loveless", no artist → Artist: "My Bloody Valentine"

    Uses music knowledge graph to make intelligent guesses.
    """
```

**Cost Control**:
- Only process tracks flagged as `needs_enrichment = true`
- Low priority: Run after album enrichment and WAV parsing
- Estimate: ~200 tokens per track
- Budget: Process 500 tracks/day max

---

## Python Script Architecture

### Project Structure

```
jucyaudio-enrich/
├── pyproject.toml          # uv project config
├── .env.example            # Template for configuration
├── README.md               # Usage instructions
├── src/
│   ├── __init__.py
│   ├── cli.py              # Typer CLI interface
│   ├── db/
│   │   ├── __init__.py
│   │   ├── manager.py      # DatabaseManager class
│   │   └── models.py       # Pydantic models for DB rows
│   ├── ai/
│   │   ├── __init__.py
│   │   ├── enrichment.py   # EnrichmentService class
│   │   └── prompts.py      # LLM prompt templates
│   ├── processing/
│   │   ├── __init__.py
│   │   ├── albums.py       # Album enrichment logic
│   │   ├── wav_paths.py    # WAV path parsing logic
│   │   └── missing.py      # Missing metadata enhancement
│   └── utils/
│       ├── __init__.py
│       ├── cost_tracker.py # Token usage and cost tracking
│       └── checkpoint.py   # Progress saving/resuming
└── tests/
    ├── test_db.py
    ├── test_ai.py
    └── test_processing.py
```

### Dependencies (pyproject.toml)

```toml
[project]
name = "jucyaudio-enrich"
version = "0.1.0"
requires-python = ">=3.13"

dependencies = [
    "typer[all]>=0.12.0",        # CLI framework
    "loguru>=0.7.0",              # Logging
    "anthropic>=0.34.0",          # Claude API client
    "pydantic>=2.9.0",            # Data validation
    "python-dotenv>=1.0.0",       # Environment variables
]

[tool.uv]
dev-dependencies = [
    "black>=24.0.0",              # Code formatting
    "isort>=5.13.0",              # Import sorting
    "pylint>=3.0.0",              # Linting
    "mypy>=1.11.0",               # Type checking
    "pytest>=8.0.0",              # Testing
]
```

### CLI Interface (cli.py)

```python
import typer
from pathlib import Path
from typing import Optional

app = typer.Typer(name="jucyaudio-enrich")

@app.command()
def enrich_albums(
    db_path: Path = typer.Option(..., help="Path to jucyaudio.sqlite"),
    limit: int = typer.Option(100, help="Max albums to process"),
    dry_run: bool = typer.Option(False, help="Preview without writing"),
    folder: Optional[str] = typer.Option(None, help="Limit to specific folder path"),
    budget_limit: float = typer.Option(10.0, help="Max cost in USD"),
):
    """
    Enrich album metadata using AI.

    Example:
        uv run enrich-albums --db-path ~/Music/jucyaudio.sqlite --limit 100
    """
    from .processing.albums import process_albums

    typer.echo(f"Processing up to {limit} albums...")
    stats = process_albums(db_path, limit, dry_run, folder, budget_limit)

    typer.echo(f"✓ Enriched {stats.processed} albums")
    typer.echo(f"✓ Cost: ${stats.cost:.4f}")
    typer.echo(f"✓ Tokens used: {stats.tokens}")

@app.command()
def parse_wav_paths(
    db_path: Path = typer.Option(..., help="Path to jucyaudio.sqlite"),
    limit: int = typer.Option(1000, help="Max WAV files to process"),
    dry_run: bool = typer.Option(False, help="Preview without writing"),
):
    """
    Extract metadata from WAV file paths using AI.

    Example:
        uv run parse-wav-paths --db-path ~/Music/jucyaudio.sqlite
    """
    from .processing.wav_paths import process_wav_files

    typer.echo(f"Processing up to {limit} WAV files...")
    stats = process_wav_files(db_path, limit, dry_run)

    typer.echo(f"✓ Parsed {stats.processed} WAV files")
    typer.echo(f"✓ Cost: ${stats.cost:.4f}")

@app.command()
def show_stats(
    db_path: Path = typer.Option(..., help="Path to jucyaudio.sqlite"),
):
    """
    Show enrichment statistics.

    Example:
        uv run show-stats --db-path ~/Music/jucyaudio.sqlite
    """
    from .db.manager import DatabaseManager

    db = DatabaseManager(db_path)
    stats = db.get_enrichment_stats()

    typer.echo(f"Albums total: {stats.total_albums}")
    typer.echo(f"Albums enriched: {stats.enriched_albums} ({stats.enriched_pct:.1f}%)")
    typer.echo(f"WAV files needing parsing: {stats.wav_needs_parsing}")
    typer.echo(f"Tracks needing enrichment: {stats.tracks_needs_enrichment}")

if __name__ == "__main__":
    app()
```

### Database Manager (db/manager.py)

```python
import sqlite3
from pathlib import Path
from typing import List, Optional
from .models import Album, Track, EnrichmentStats

class DatabaseManager:
    """Handles all SQLite database operations."""

    def __init__(self, db_path: Path):
        self.db_path = db_path
        self.conn = sqlite3.connect(db_path)
        self.conn.row_factory = sqlite3.Row

    def get_albums_to_enrich(
        self,
        limit: int,
        folder_filter: Optional[str] = None
    ) -> List[Album]:
        """
        Get albums that need AI enrichment.

        Criteria:
        - genres IS NULL OR genres = '[]'
        - album_id IS NOT NULL (valid album)
        """
        query = """
            SELECT album_id, album_artist, title, year, folder_id
            FROM Albums
            WHERE (genres IS NULL OR genres = '[]')
        """

        if folder_filter:
            query += """
                AND folder_id IN (
                    SELECT folder_id FROM Folders
                    WHERE actual_path LIKE ? OR root_path LIKE ?
                )
            """

        query += " LIMIT ?"

        params = [f"%{folder_filter}%", f"%{folder_filter}%", limit] if folder_filter else [limit]

        cursor = self.conn.execute(query, params)
        return [Album.from_row(row) for row in cursor.fetchall()]

    def update_album_metadata(
        self,
        album_id: int,
        genres: List[str],
        moods: List[str],
        tags: List[str],
        confidence: float
    ):
        """Update album metadata with AI-enriched data."""
        import json

        self.conn.execute("""
            UPDATE Albums
            SET genres = ?, moods = ?, tags = ?,
                ai_enriched_at = CURRENT_TIMESTAMP,
                ai_confidence = ?
            WHERE album_id = ?
        """, (
            json.dumps(genres),
            json.dumps(moods),
            json.dumps(tags),
            confidence,
            album_id
        ))
        self.conn.commit()

    def get_wav_tracks_needing_parsing(self, limit: int) -> List[Track]:
        """Get WAV tracks with minimal metadata."""
        cursor = self.conn.execute("""
            SELECT track_id, filename, folder_id, duration,
                   artist_name, album_title, title
            FROM Tracks
            WHERE (filename LIKE '%.wav' OR filename LIKE '%.WAV')
              AND needs_enrichment = 1
            LIMIT ?
        """, (limit,))

        return [Track.from_row(row) for row in cursor.fetchall()]

    def update_track_metadata(
        self,
        track_id: int,
        artist: str,
        album: str,
        title: str
    ):
        """Update track metadata from WAV path parsing."""
        self.conn.execute("""
            UPDATE Tracks
            SET artist_name = ?, album_title = ?, title = ?,
                needs_enrichment = 0,
                updated_at = CURRENT_TIMESTAMP
            WHERE track_id = ?
        """, (artist, album, title, track_id))
        self.conn.commit()

    def get_enrichment_stats(self) -> EnrichmentStats:
        """Get overall enrichment statistics."""
        # Implementation details...
        pass
```

### AI Enrichment Service (ai/enrichment.py)

```python
import anthropic
from typing import List, Tuple, Optional
from pydantic import BaseModel
from .prompts import ALBUM_ENRICHMENT_PROMPT, WAV_PATH_PARSING_PROMPT

class AlbumEnrichmentResult(BaseModel):
    """Structured output from album enrichment."""
    primary_genre: str
    secondary_genres: List[str]
    moods: List[str]
    tags: List[str]
    confidence: float

class EnrichmentService:
    """Handles all Claude API interactions."""

    def __init__(self, api_key: str, model: str = "claude-sonnet-4-20250514"):
        self.client = anthropic.Anthropic(api_key=api_key)
        self.model = model

    def enrich_album(
        self,
        artist: str,
        title: str,
        year: Optional[int] = None
    ) -> Tuple[AlbumEnrichmentResult, int]:
        """
        Enrich album metadata using Claude API.

        Returns:
            (enrichment result, token count)
        """
        prompt = ALBUM_ENRICHMENT_PROMPT.format(
            artist=artist,
            title=title,
            year=year or "Unknown"
        )

        response = self.client.messages.create(
            model=self.model,
            max_tokens=1024,
            messages=[{"role": "user", "content": prompt}]
        )

        # Parse structured response
        result = AlbumEnrichmentResult.model_validate_json(
            response.content[0].text
        )

        # Calculate token usage
        tokens = response.usage.input_tokens + response.usage.output_tokens

        return result, tokens

    def parse_wav_path(
        self,
        full_path: str,
        duration: float
    ) -> Tuple[dict, int]:
        """
        Extract metadata from WAV file path.

        Returns:
            (metadata dict, token count)
        """
        prompt = WAV_PATH_PARSING_PROMPT.format(
            path=full_path,
            duration=duration
        )

        response = self.client.messages.create(
            model=self.model,
            max_tokens=512,
            messages=[{"role": "user", "content": prompt}]
        )

        # Parse and return
        # Implementation details...
        pass
```

### Prompt Templates (ai/prompts.py)

```python
ALBUM_ENRICHMENT_PROMPT = """
You are a music expert tasked with enriching album metadata.

Album Information:
- Artist: {artist}
- Title: {title}
- Year: {year}

Please provide:
1. Primary genre (single most accurate genre)
2. Secondary genres (up to 3 related genres)
3. Moods (up to 5 descriptive moods/atmospheres)
4. Tags (up to 5 descriptive tags about style, era, scene)
5. Confidence score (0.0-1.0) for your assessment

Return your response as valid JSON matching this structure:
{{
  "primary_genre": "Shoegaze",
  "secondary_genres": ["Indie Rock", "Dream Pop"],
  "moods": ["Atmospheric", "Melancholic", "Ethereal"],
  "tags": ["Reverb-heavy", "90s UK", "Creation Records"],
  "confidence": 0.92
}}

Important guidelines:
- Use consistent genre naming (capitalize first letter)
- Be specific but not overly niche
- Consider the era and cultural context
- If unsure, lower the confidence score
"""

WAV_PATH_PARSING_PROMPT = """
You are a metadata extraction expert. Given a WAV file path, extract structured metadata.

File Information:
- Path: {path}
- Duration: {duration} seconds

Analyze the path structure and extract:
1. Artist name (from folder names or filename)
2. Album title (from parent folder)
3. Track title (from filename, cleaned up)
4. Label/catalog number (if present)
5. Genre hint (based on folder structure/context)
6. Confidence score (0.0-1.0)

Return valid JSON:
{{
  "artist": "Artist Name",
  "album": "Album Title",
  "title": "Track Title",
  "label": "Label Name",
  "genre_hint": "Genre",
  "confidence": 0.85
}}

Guidelines:
- Handle German/multilingual paths gracefully
- Recognize common patterns (e.g., "01-a1.wav" → "Track A1")
- Extract catalog numbers (e.g., "Force Inc" is a label)
- Use duration to infer side breaks (sides < 25 min typical)
"""
```

---

## Cost Management & Limits

### Token Estimation

**Album Enrichment**:
- Input: ~100 tokens (artist + title + prompt)
- Output: ~400 tokens (JSON response)
- **Total per album: ~500 tokens**

**WAV Path Parsing**:
- Input: ~150 tokens (path + duration + prompt)
- Output: ~150 tokens (JSON response)
- **Total per WAV: ~300 tokens**

**Missing Metadata**:
- Input: ~80 tokens (partial info + prompt)
- Output: ~120 tokens (completion)
- **Total per track: ~200 tokens**

### Cost Calculations (Claude Sonnet 3.5)

Pricing (as of 2025-10):
- Input: $3 per million tokens
- Output: $15 per million tokens

**Album Enrichment** (100 albums):
- Tokens: 50,000 (100 input + 400 output per album)
- Cost: ~$0.02

**WAV Parsing** (1000 files):
- Tokens: 300,000 (150 input + 150 output per file)
- Cost: ~$0.12

**Typical Full Library Enrichment** (50k tracks, 10k albums):
- Albums: 10k × 500 tokens = 5M tokens ≈ $2.00
- WAV files: 5k × 300 tokens = 1.5M tokens ≈ $0.60
- **Total: ~$3 for complete enrichment**

### Budget Limits (config)

```ini
# .env file
ANTHROPIC_API_KEY=sk-...
DATABASE_PATH=/Users/me/Music/jucyaudio.sqlite

# Cost control
DAILY_BUDGET_USD=5.00
MONTHLY_BUDGET_USD=50.00
MAX_BATCH_SIZE=100

# Processing limits
MAX_ALBUMS_PER_RUN=500
MAX_WAV_FILES_PER_RUN=1000
MAX_TRACKS_PER_RUN=500

# Model selection
AI_MODEL=claude-sonnet-4-20250514
AI_TEMPERATURE=0.3
```

---

## Usage Examples

### Basic Album Enrichment

```bash
# Enrich 100 albums
uv run enrich-albums --db-path ~/Music/jucyaudio.sqlite --limit 100

# Dry run (preview only)
uv run enrich-albums --db-path ~/Music/jucyaudio.sqlite --limit 10 --dry-run

# Target specific folder
uv run enrich-albums --db-path ~/Music/jucyaudio.sqlite --folder "autumn25" --limit 50
```

### WAV Path Parsing

```bash
# Parse 1000 WAV files
uv run parse-wav-paths --db-path ~/Music/jucyaudio.sqlite --limit 1000

# Dry run to preview parsing
uv run parse-wav-paths --db-path ~/Music/jucyaudio.sqlite --limit 10 --dry-run
```

### Statistics & Monitoring

```bash
# Show enrichment progress
uv run show-stats --db-path ~/Music/jucyaudio.sqlite

# Output:
# Albums total: 10,234
# Albums enriched: 8,456 (82.6%)
# WAV files needing parsing: 1,234
# Tracks needing enrichment: 456
# Estimated cost to complete: $0.85
```

### Batch Processing Strategy

For large libraries, process incrementally:

```bash
# Week 1: High-priority folders
uv run enrich-albums --folder "autumn25" --limit 500 --budget-limit 2.00

# Week 2: Specific genres
uv run enrich-albums --folder "Shoegaze" --limit 500 --budget-limit 2.00

# Week 3: WAV files
uv run parse-wav-paths --limit 2000 --budget-limit 1.00

# Week 4: Remaining albums
uv run enrich-albums --limit 1000 --budget-limit 3.00
```

---

## Integration with Virtual Tags

**Current State**: AI enrichment writes to `Albums.genres/moods/tags` (JSON arrays).

**Future Integration** (Phase 2):
1. AI enrichment suggests genres/moods/tags
2. User reviews suggestions in C++ app (future approval UI)
3. Approved suggestions converted to VirtualTags
4. FolderVirtualTags entries created automatically
5. Folders appear in "Tagged" navigation tree

**Implementation Plan**:
```python
def apply_enrichment_to_virtual_tags(album_id: int, approved_tags: List[str]):
    """
    Convert approved AI tags into VirtualTags.

    1. Check if VirtualTag exists, create if missing
    2. Get folder_id for album
    3. Create FolderVirtualTags entry
    4. Folder now appears in Tagged tree
    """
```

---

## Future Enhancements

### Phase 2: Advanced Features

1. **Confidence-Based Auto-Approval**
   - High confidence (>0.9) → Auto-approve
   - Medium confidence (0.7-0.9) → Suggest in UI
   - Low confidence (<0.7) → Flag for manual review

2. **Genre Taxonomy Learning**
   - Build custom genre vocabulary from user's library
   - AI learns user's preferred genre granularity
   - Consistent with user's manual Virtual Tags

3. **Batch Album Processing**
   - Group albums by artist for context sharing
   - Reduce token usage via shared context
   - "This artist's discography includes..." prompting

4. **Multi-Pass Enrichment**
   - Pass 1: High-confidence albums
   - Pass 2: Medium-confidence with user feedback
   - Pass 3: Low-confidence with additional context

### Phase 3: Advanced Parsing

1. **Bandcamp URL Scraping**
   - Extract metadata from Bandcamp pages
   - Store album art URLs
   - Get full track listings

2. **Cross-Reference Validation**
   - Compare AI suggestions with MusicBrainz
   - Validate artist/album spelling
   - Detect compilation albums

3. **Label Recognition**
   - Build label taxonomy from file paths
   - Extract catalog numbers
   - Associate labels with genres

---

## Testing Strategy

### Unit Tests

```python
# Test database operations
def test_get_albums_to_enrich():
    db = DatabaseManager(":memory:")
    albums = db.get_albums_to_enrich(limit=10)
    assert len(albums) <= 10

# Test AI response parsing
def test_parse_album_enrichment_response():
    response = '{"primary_genre": "Shoegaze", ...}'
    result = AlbumEnrichmentResult.model_validate_json(response)
    assert result.primary_genre == "Shoegaze"

# Test WAV path parsing
def test_parse_complex_wav_path():
    path = "E:\\AUS\\part3\\Taksi Inc- nachtschleife\\01-a1.wav"
    result = parse_path_components(path)
    assert result.artist == "Taksi Inc"
```

### Integration Tests

```python
# Test full enrichment pipeline
def test_enrich_album_end_to_end():
    db = create_test_db()
    service = EnrichmentService(api_key="test")

    album = db.get_albums_to_enrich(limit=1)[0]
    result, tokens = service.enrich_album(album.artist, album.title)

    db.update_album_metadata(album.id, result.primary_genre, ...)

    # Verify database update
    updated = db.get_album(album.id)
    assert updated.genres is not None
```

### Manual Testing

1. **Dry Run Validation**:
   - Run with `--dry-run` on 10 albums
   - Verify AI suggestions are accurate
   - Check token usage estimates

2. **Small Batch Test**:
   - Process 20 albums (actual write)
   - Verify database updates
   - Check cost tracking

3. **Full Library Test**:
   - Process entire library incrementally
   - Monitor costs and accuracy
   - Validate no database corruption

---

## Revision History

- **2025-10-27**: Complete rewrite focusing on AI enrichment, removed outdated album implementation details
- **2025-08-13**: Original document created for album-centric model
