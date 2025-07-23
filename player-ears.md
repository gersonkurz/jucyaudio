# Enhanced Player EARS Specification

## 1. Overview

This document specifies the requirements for enhancing the JucyAudio player component using the EARS (Easy Approach to Requirements Syntax) methodology. The enhanced player will feature a two-row layout with advanced visualization, track markers, and mix-aware playback display.

## 2. General Requirements

### 2.1 Layout Structure

**PLAYER-001**: The player component SHALL occupy a fixed height at the bottom of the main window.

**PLAYER-002**: The player component SHALL be divided into two horizontal rows, where the top row occupies 70% of the total height and the bottom row occupies 30%.

**PLAYER-003**: The player component SHALL maintain its layout proportions when the main window is resized horizontally.

## 3. Top Row Requirements

### 3.1 Transport Controls

**PLAYER-004**: The player SHALL display five transport control buttons on the left side of the top row, in the following order: Previous Track, Stop, Play, Pause, Next Track.

**PLAYER-005**: WHEN the Play button is clicked, the player SHALL start or resume audio playback.

**PLAYER-006**: WHEN the Pause button is clicked, the player SHALL pause audio playback at the current position.

**PLAYER-007**: WHEN the Stop button is clicked, the player SHALL stop audio playback AND reset the playback position to the beginning.

**PLAYER-008**: WHEN the Previous Track button is clicked, the player SHALL load and begin playing the previous track in the current context.

**PLAYER-009**: WHEN the Next Track button is clicked, the player SHALL load and begin playing the next track in the current context.

**PLAYER-010**: WHILE no audio file is loaded, the transport control buttons SHALL be visually disabled.

### 3.2 Waveform Display

**PLAYER-011**: The player SHALL display a waveform visualization that occupies all remaining horizontal space to the right of the transport controls.

**PLAYER-012**: The waveform SHALL be scaled to fit the entire audio file duration within the available width.

**PLAYER-013**: The waveform SHALL use two distinct colors: one for the portion already played and another for the portion yet to be played.

**PLAYER-014**: WHILE audio is playing, the waveform SHALL update the color boundary in real-time to reflect the current playback position.

**PLAYER-015**: WHEN the user clicks on the waveform, the player SHALL seek to the corresponding position in the audio file.

## 4. Bottom Row Requirements

### 4.1 Playback Mode Controls

**PLAYER-016**: The player SHALL display a Repeat button that cycles through three states: Repeat Off, Repeat Track, and Repeat All.

**PLAYER-017**: WHEN in Repeat Track mode, the player SHALL restart the current track after it completes.

**PLAYER-018**: WHEN in Repeat All mode, the player SHALL continue playing from the first track after the last track completes.

**PLAYER-019**: The player SHALL display a Shuffle button that toggles between shuffle enabled and shuffle disabled states.

**PLAYER-020**: WHEN shuffle is enabled, the player SHALL select the next track randomly rather than sequentially.

### 4.2 Volume Controls

**PLAYER-021**: The player SHALL display a speaker icon followed by a horizontal volume slider.

**PLAYER-022**: The volume slider SHALL control the playback volume from 0% (muted) to 100% (maximum).

**PLAYER-023**: The volume control SHALL use logarithmic scaling for perceptually linear volume adjustment.

### 4.3 Time Display

**PLAYER-024**: The player SHALL display the current playback position in MM:SS or H:MM:SS format as appropriate.

**PLAYER-025**: The player SHALL display the total track duration in MM:SS or H:MM:SS format as appropriate.

**PLAYER-026**: WHILE playing, the current position display SHALL update at least once per second.

## 5. Track Marker System Requirements

### 5.1 Marker Creation

**PLAYER-027**: WHEN the user clicks on the waveform with a modifier key (e.g., Ctrl+Click), the system SHALL create a new marker at that position.

**PLAYER-028**: WHEN a marker is created, the system SHALL open a text input dialog for the user to enter a comment.

**PLAYER-029**: IF the user cancels the comment dialog, THEN the system SHALL not create the marker.

### 5.2 Marker Display

**PLAYER-030**: The waveform SHALL display visual indicators for all markers associated with the current track.

**PLAYER-031**: WHEN the user hovers over a marker, the system SHALL display a tooltip showing the marker's comment text.

**PLAYER-032**: Markers SHALL be visually distinct from the playback position indicator.

### 5.3 Marker Editing

**PLAYER-033**: WHEN the user clicks on an existing marker, the system SHALL open a dialog with the current comment text and options to Save changes or Delete the marker.

**PLAYER-034**: WHEN Save is selected, the system SHALL update the marker with the edited comment text.

**PLAYER-035**: WHEN Delete is selected, the system SHALL remove the marker from the track.

### 5.4 Marker Persistence

**PLAYER-036**: The system SHALL persist all markers to the database, associated with the specific track.

**PLAYER-037**: WHEN a track is loaded, the system SHALL retrieve and display all associated markers.

**PLAYER-038**: [Removed - not applicable as we don't handle external file modifications]

## 6. Mix Track Display Requirements

### 6.1 Mix Detection

**PLAYER-039**: WHERE the currently playing file is a mix created within JucyAudio, the system SHALL identify it as such using the database mix information.

**PLAYER-040**: For identified mixes, the system SHALL load the track boundary information from the MixTracks table.

### 6.2 Current Track Display

**PLAYER-041**: WHILE playing a JucyAudio mix, the system SHALL display the name of the current track within the mix.

**PLAYER-042**: The current track display SHALL update automatically when playback crosses a track boundary.

**PLAYER-043**: The current track display SHALL show the track position within the mix (e.g., "Track 3 of 7").

**PLAYER-044**: WHERE the original track information is available, the system SHALL display the artist and title.

## 7. Database Requirements

### 7.1 Marker Storage

**PLAYER-045**: The system SHALL create a new database table `TrackMarkers` with fields: marker_id (primary key), track_id (foreign key), position_ms, comment, created_at, updated_at.

**PLAYER-046**: The TrackMarkers table SHALL enforce referential integrity with the Tracks table.

**PLAYER-047**: WHEN a track is deleted from the database, the system SHALL cascade delete all associated markers.

### 7.2 Schema Migration

**PLAYER-048**: The system SHALL implement a database migration to create the TrackMarkers table when upgrading to the new version.

**PLAYER-049**: The migration SHALL be reversible to allow rollback if needed.

## 8. Performance Requirements

**PLAYER-050**: The waveform generation SHALL NOT block the UI thread.

**PLAYER-051**: The waveform SHALL begin displaying within 2 seconds of track load, regardless of file duration.

**PLAYER-052**: Marker operations (create, edit, delete) SHALL complete within 100ms.

**PLAYER-053**: The player SHALL maintain smooth playback while updating the waveform display.

## 9. Error Handling

**PLAYER-054**: IF waveform generation fails, THEN the player SHALL fall back to displaying a simple progress bar.

**PLAYER-055**: IF marker creation fails due to database errors, THEN the system SHALL display an error message and not create a partial marker.

**PLAYER-056**: IF mix track information cannot be loaded, THEN the player SHALL display only the mix filename without track details.

## 10. Future Enhancements (Phase 2)

**PLAYER-057**: WHERE markers exist, the system SHALL support categorizing markers with colors and emoji indicators.

**PLAYER-058**: The system SHALL support keyboard shortcuts for marker navigation (jump to next/previous marker).

**PLAYER-059**: The system SHALL support exporting markers as cue points in standard formats.

**PLAYER-060**: The waveform SHALL support zooming in/out for detailed view of specific sections.

---

## Implementation Phases

### Phase 1: Core UI Layout ✓ COMPLETED
- Implement two-row layout structure ✓
- Add transport control buttons with new styling ✓
- Add playback mode buttons ✓
- Add volume control and time displays ✓

### Phase 1.5: Icon Integration and UI Polish (IN PROGRESS)
- Replace text labels with proper icon system (PNG/SVG icons) ✓ (transport controls only)
- Implement proper icon loading and display for transport controls ✓
- Add icons for repeat modes, shuffle, and volume ⚠️ (text placeholders remain)
- Ensure proper scaling and theming support for icons ✓
- Polish button sizes and spacing for better visual appearance ✓

**Current Status:**
- Transport controls use Material Design SVG icons
- Icons display at 50% of button size for visual balance
- Disabled buttons show with 70% opacity
- **ISSUE:** Play/pause toggle logic needs fixing - pause only works once per track

### Phase 2: Waveform Visualization
- Implement waveform rendering
- Add two-color progress display
- Implement click-to-seek functionality

### Phase 3: Marker System
- Create database schema for markers
- Implement marker creation and display
- Add marker editing capabilities
- Implement persistence layer

### Phase 4: Mix Track Display
- Implement mix detection
- Add current track display
- Implement boundary crossing detection

### Phase 5: Polish and Performance
- Optimize waveform rendering
- Add animations and transitions
- Implement error handling
- Performance testing and optimization