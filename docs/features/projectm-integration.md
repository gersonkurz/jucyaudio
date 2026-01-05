# ProjectM Integration Plan for JucyAudio (Reviewed & Extended)

## Status: Implementation In Progress — Architectural Review Needed
**Reviewers:** Codex (Architect), Claude (Systems Integration), Gemini (Pending)

**Update (Jan 2025):** Core integration is functional on both Windows and macOS. Three architectural issues identified in Section 5 require review before proceeding with UI finalization and playlist support.

## 1. Executive Summary
This document outlines the architectural plan to integrate **projectM v4** into JucyAudio. The goal is to provide a high-performance, hardware-accelerated music visualization system that runs natively on Windows and macOS. The integration handles the complexities of bridging JUCE's audio/rendering threads with projectM's OpenGL requirements.

## 2. Architecture & Components

### 2.1. Build System (`CMakeLists.txt`)
We leverage `FetchContent` for a hermetic build.
*   **Dependency**: `projectM` (Tag: `v4.1.1` - Stable).
*   **Options**:
    *   `PROJECTM_BUILD_TESTING=OFF`
    *   `PROJECTM_BUILD_EXAMPLES=OFF`
    *   `PROJECTM_BUILD_LLVM_JIT=OFF` (Avoid heavy LLVM dependency unless strictly necessary; default parser is sufficient).
*   **Linking**: Link `projectM::projectM` privately to `jucyaudio`.
*   **Post-Build**: 
    *   Copy `projectM` default assets (textures, shaders) to `Runtime/Assets/projectm`.
    *   Copy a curated set of `.milk` presets to `Runtime/Assets/presets`.

### 2.2. Audio Pipeline (Thread-Safe Bridge)
**Challenge**: Tapping the audio stream without blocking the sensitive audio mixing thread.
**Solution**: A lock-free Single Producer, Single Consumer (SPSC) FIFO.

*   **Component**: `AudioVisualizerFIFO`
    *   **Mechanism**: `juce::AbstractFifo` with a `std::vector<float>` circular buffer.
    *   **Format**: Mono downmix (L+R)/2, float, 48kHz (or host rate).
    *   **Capacity**: Configurable via settings; current default is 2048 samples.
    *   **Overflow behavior**: Writes are partial when the FIFO is full (extra samples are dropped).
*   **Writer (Audio Thread)**: `MixPlaybackEngine::getNextAudioBlock()` writes mixed audio to the FIFO (current implementation).
*   **Reader (Render Thread)**: `ProjectMComponent` polls this buffer every frame.

### 2.3. Rendering Pipeline (OpenGL Integration)
**Challenge**: Managing OpenGL contexts between JUCE and projectM.
**Solution**: JUCE owns the context; projectM renders into it.

*   **Component**: `ProjectMComponent`
    *   **Inherits**: `juce::Component`, `juce::OpenGLRenderer`.
    *   **Lifecycle**:
        1.  **`newOpenGLContextCreated`**:
            *   Initialize `projectm_handle`.
            *   **CRITICAL**: Pass a custom OpenGL loader function if projectM v4 requires it, or rely on JUCE having established the context. *Note: projectM v4 typically uses an internal loader (glad) or system headers. We must ensure the context is active.*
            *   Load textures from `File::getSpecialLocation(currentExecutableLocation)`.
        2.  **`renderOpenGL`**:
            *   Check `m_visualizerFifo` for new audio data.
            *   `projectm_pcm_add_float()`: Feed pending audio.
            *   `projectm_opengl_render_frame()`: Draw to the current framebuffer.
        3.  **`openGLContextClosing`**:
            *   `projectm_destroy()`: Cleanup to prevent leaks.
    *   **Optimization**:
        *   Only render when the component is visible (`isVisible()`).
        *   Limit frame rate to 30 or 60 FPS via `setContinuousRepainting(false)` and using a `Timer` or `VBlankAttachment` if needed to save battery.

## 3. Detailed Implementation Steps

### Phase 1: Build & Dependencies
1.  **Update `CMakeLists.txt`**:
    *   Add `FetchContent` block for projectM.
    *   Define asset copy commands.
2.  **Sanity Check**: Verify projectM compiles and links on Windows (MSVC) and macOS (Clang).

### Phase 2: Core Infrastructure (The Bridge)
1.  **`Utils/AudioVisualizerFIFO.h`**:
    *   Implement the lock-free buffer.
2.  **`UI/PlaybackController.h/cpp`** (proposed change):
    *   Add `AudioVisualizerFIFO` pointer (non-owning).
    *   Update `prepareToPlay()` to set FIFO sample rate.
    *   Modify `getNextAudioBlock()` to write *post-EQ/Reverb* audio to FIFO for both track and mix modes.

### Phase 3: Visualizer Engine
1.  **`UI/Visualizer/ProjectMComponent.h`**:
    *   Define the class structure.
2.  **`UI/Visualizer/ProjectMComponent.cpp`**:
    *   **Initialization**: Locate assets dynamically.
    *   **Rendering**: Implement the bridge to `libprojectM` C API.
    *   **Input**: Handle resize events (`projectm_set_window_size`).

### Phase 4: Integration & UX
1.  **UI Placement**:
    *   Add `ProjectMComponent` to `MainComponent`.
    *   Create a toggle/tab to show/hide it.
2.  **Preset Management**:
    *   Scan the `presets` directory.
    *   Implement `Next`/`Prev` preset logic (mapped to UI buttons or keys).
3.  **Settings**:
    *   Add "Visualizer FPS" or "High Quality" toggle in SettingsDialog.

## 4. Risk Mitigation
*   **OpenGL Version**: ProjectM v4 requires modern GL (3.3+). We must ensure `juce::OpenGLContext` requests a compatible version (`setOpenGLVersionRequired`).
*   **Context Loss**: If the window moves between monitors or minimizes, context might be lost. `newOpenGLContextCreated` handles recreation, but we must ensure projectM is fully re-initialized.
*   **Asset Paths**: Mac bundles place assets in `Resources`, Windows next to `.exe`. Use a unified `AssetLocator` helper.

---

## 5. Open Issues Requiring Review

The following issues were identified during implementation and require architectural decisions before proceeding.

### 5.1. Issue: Visualizer UI Placement

**Current State:** The visualizer component exists but its placement in the UI is not finalized.

**Context:** JucyAudio has several distinct UI areas:
- **Navigation Panel** (left): Folder tree, library navigation
- **Data View** (center): Track list, metadata columns
- **Mix Editor** (center, alternate view): Timeline with waveforms, track arrangement
- **Enhanced Player** (bottom): Waveform display, transport controls, time display
- **Status Panel** (bottom): Contains the enhanced player and additional status info

**Options to Consider:**

| Option | Description | Pros | Cons |
|--------|-------------|------|------|
| **A. Replace waveform in Enhanced Player** | Visualizer takes over the waveform area when enabled | Prominent, always visible during playback | Loses waveform context; users may want both |
| **B. Separate resizable panel** | Dedicated visualizer panel (dockable or floating) | User controls size/position; can be hidden | More complex UI; window management overhead |
| **C. Overlay on Data View** | Semi-transparent overlay on track list during playback | Immersive; "screensaver" feel | Obscures content; may be distracting while working |
| **D. Full-screen mode only** | Visualizer only available as full-screen toggle (F11) | Maximum visual impact; clean separation | Not useful for casual background visuals |
| **E. Picture-in-Picture** | Small floating visualizer window within the app | Non-intrusive; always available | Small size reduces visual impact |

**Questions for Reviewers:**
1. Should the visualizer placement be user-configurable, or should we pick one canonical location?
2. Is there value in multiple modes (e.g., PiP by default, full-screen on demand)?
3. How should the visualizer behave in Mix Editor view vs. Library view?

---

### 5.2. Issue: Unified Playback Architecture

**Current State:** Visualizer only receives audio during mix playback, not during single-track preview.

**Root Cause Analysis:**

JucyAudio currently has **two completely separate playback paths**:

```
┌─────────────────────────────────────────────────────────────────┐
│ PATH A: Single Track Preview (Library)                          │
├─────────────────────────────────────────────────────────────────┤
│ Trigger: Double-click track in library                          │
│ Flow:    AudioFormatReader → AudioTransportSource →             │
│          PlaybackController::getNextAudioBlock() →              │
│          EQ → Reverb → Audio Output                             │
│ Visualizer: ❌ NOT CONNECTED                                    │
│ Playlist:   ❌ NO CONCEPT (plays one track, then stops)         │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ PATH B: Mix Playback (Mix Editor)                               │
├─────────────────────────────────────────────────────────────────┤
│ Trigger: Play button in Mix Editor                              │
│ Flow:    MixProjectLoader → MixPlaybackEngine →                 │
│          (multiple tracks mixed with overlaps/crossfades) →     │
│          EQ → Reverb → AudioVisualizerFIFO → Audio Output       │
│ Visualizer: ✅ CONNECTED                                        │
│ Playlist:   N/A (mix defines all tracks and timing)             │
└─────────────────────────────────────────────────────────────────┘
```

**Why Mixes Are Different:**

A mix is fundamentally different from a playlist:

| Aspect | Playlist | Mix |
|--------|----------|-----|
| **Track timing** | Sequential (A ends → B starts) | Overlapping (B starts while A plays) |
| **Data model** | `vector<TrackInfo>` + index | `vector<MixTrack>` with cue points, attach points, envelopes |
| **Audio engine** | Single `AudioTransportSource` | `MixPlaybackEngine` mixing multiple sources |
| **"Next track"** | Stop A, start B at 0:00 | Jump to B's entry point on timeline (may already be audible) |

**Proposed Architecture: Two Engines, Unified Interface**

```
┌─────────────────────────────────────────────────────────────────┐
│                      PlaybackController                         │
│                     (Unified Interface)                         │
├─────────────────────────────────────────────────────────────────┤
│  Public API (same for both modes):                              │
│  • play(), pause(), stop()                                      │
│  • seek(position)                                               │
│  • nextTrack(), previousTrack()                                 │
│  • getPosition(), getDuration()                                 │
│  • getCurrentTrackInfo()                                        │
│  • setVisualizerFIFO() → feeds BOTH engines                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────────────┐    ┌──────────────────────┐          │
│  │   PLAYLIST MODE      │    │     MIX MODE         │          │
│  ├──────────────────────┤    ├──────────────────────┤          │
│  │ PlaylistQueue:       │    │ MixProjectLoader:    │          │
│  │ • vector<TrackInfo>  │    │ • vector<MixTrack>   │          │
│  │ • currentIndex       │    │ • trackStartTimes    │          │
│  │ • repeatMode         │    │ • totalDuration      │          │
│  │ • shuffleEnabled     │    │ • cue/attach points  │          │
│  ├──────────────────────┤    ├──────────────────────┤          │
│  │ AudioTransportSource │    │ MixPlaybackEngine    │          │
│  │ (one track at a time)│    │ (multi-track mixing) │          │
│  └──────────┬───────────┘    └──────────┬───────────┘          │
│             │                           │                       │
│             └─────────┬─────────────────┘                       │
│                       ▼                                         │
│              ┌─────────────────┐                                │
│              │ Master Chain:   │                                │
│              │ EQ → Reverb →   │                                │
│              │ VisualizerFIFO →│                                │
│              │ Audio Output    │                                │
│              └─────────────────┘                                │
└─────────────────────────────────────────────────────────────────┘
```

**PlaylistQueue Data Structure:**

```cpp
struct PlaylistQueue {
    std::vector<TrackInfo> tracks;
    size_t currentIndex = 0;

    enum class RepeatMode { None, One, All };
    RepeatMode repeatMode = RepeatMode::None;

    bool shuffleEnabled = false;
    std::vector<size_t> shuffleOrder;  // Indices into tracks

    // Source tracking (for UI context)
    enum class Source {
        Folder,      // Double-clicked track in a folder
        Selection,   // Multiple selected tracks
        SearchResult // Tracks from a search
    };
    Source source;
    std::string sourcePath;  // e.g., folder path for context
};
```

**Behavior Specification ("No Surprises"):**

| User Action | Playlist Mode Behavior | Mix Mode Behavior |
|-------------|------------------------|-------------------|
| Double-click track in library | Play single track only (no queue) | N/A (switches to playlist mode) |
| Press Play | Resume current track | Resume mix from position |
| Press Pause | Pause, preserve position | Pause, preserve position |
| Press Stop | Stop, reset to track start | Stop, reset to mix start |
| Press Next | If repeat-one: restart track. Else: advance queue, play from 0:00. If last track + no repeat: stop. | Jump to next track's timeline entry point |
| Press Previous | If >3s into track: restart. Else: go to previous track. If first track: restart. | Jump to previous track's timeline entry point |
| Track ends naturally | Auto-advance per repeat mode | Continue mix (overlapping tracks handle themselves) |
| Seek | Seek within current track | Seek on mix timeline |

**Implementation Changes Required:**

1. **PlaybackController.h/cpp:**
   - Add `PlaylistQueue` member
   - Add playlist management methods: `setPlaylist()`, `clearPlaylist()`, `getPlaylistInfo()`
   - Modify `getNextAudioBlock()` to feed visualizer in BOTH modes
   - Implement auto-advance logic when track ends

2. **MainComponent.cpp:**
   - On track double-click: Build playlist from current folder view, call `setPlaylist()`
   - Wire Next/Previous buttons to `PlaybackController::nextTrack()`/`previousTrack()`

3. **EnhancedPlayerComponent:**
   - Display current track index (e.g., "Track 3 of 12")
   - Show repeat/shuffle toggle buttons

4. **DataViewComponent:**
   - Highlight currently playing track in list
   - Consider "Add to Queue" context menu option for future

**Questions for Reviewers:**

1. Should "double-click plays folder as playlist" be the default, or should it require explicit opt-in (e.g., "Play All" button)?
2. How should the queue behave when the user navigates to a different folder while music is playing?
3. Should there be a visible "Queue" panel showing upcoming tracks, or is implicit queue sufficient?
4. Is the proposed "Previous" behavior (restart if >3s, else go back) intuitive?

---

### 5.3. Issue: Preset Licensing

**Current State:** We're using the `presets-cream-of-the-crop` repository from projectM.

**Concern:** The licensing status of individual `.milk` presets is unclear. These presets were created by various community members over 20+ years, originally for Winamp/MilkDrop.

**Risk Assessment:**

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Preset author claims copyright | Low | Medium | Use only presets with clear licensing |
| Preset contains trademarked imagery | Very Low | Low | Manual review of popular presets |
| Distribution violates license terms | Unknown | High | Need to verify repository license |

**Research Needed:**

1. **Repository License:** Does `presets-cream-of-the-crop` have a LICENSE file? What does it cover?
2. **Original MilkDrop License:** Were presets contributed under any implicit license?
3. **ProjectM's Position:** How does the projectM project handle preset licensing?

**Possible Approaches:**

| Approach | Description | Effort | Risk Level |
|----------|-------------|--------|------------|
| **A. Use repository as-is** | Trust that projectM has vetted the presets | None | Unknown |
| **B. Ship minimal set** | Include only 10-20 presets with verified licenses | Low | Very Low |
| **C. No bundled presets** | Ship without presets; let users add their own | None | None (but poor UX) |
| **D. Download on first run** | Presets downloaded separately, not part of distribution | Medium | Shifts liability |
| **E. Contact projectM maintainers** | Ask about licensing status directly | Low | Clarifies position |

**Questions for Reviewers:**

1. What is the acceptable risk level for preset licensing?
2. Should we attempt to contact preset authors for popular presets?
3. Is a "download presets" approach worth the implementation complexity?
4. Are there any presets we should definitely exclude (e.g., those with artist names/logos)?

---

## 6. Reviewer Feedback

### 6.1. Original Architecture Comments

# Codex Comments (Architecture Review)
- The FIFO size (2048 samples) may underflow if the render thread stalls; consider a larger buffer and drop strategy.
- projectM expects a sample rate; document how to pass the actual host rate (or resample) so visuals stay stable at 44.1k vs 48k.
- Add an explicit fallback/log when asset paths are missing to avoid silent black frames.

# Claude Comments (Systems Integration)
- The SPSC FIFO design is sound, but consider using `juce::AbstractFifo` with a power-of-2 buffer size (2048 is good) for optimal performance. The mono downmix (L+R)/2 is appropriate for visualization.
- For OpenGL context management: JUCE's `OpenGLContext::setOpenGLVersionRequired()` should request GL 3.3 Core Profile explicitly. ProjectM v4 uses modern GL and will fail silently on legacy contexts.
- Thread priority: The render thread should not compete with audio. Consider setting `setContinuousRepainting(false)` and using a 30fps Timer rather than VBlank sync to reduce CPU load when visuals aren't the focus.
- Memory consideration: ProjectM presets can allocate significant GPU memory. Implement a "low memory" mode that disables the visualizer when system resources are constrained.
- The asset copy strategy should use CMake's `install(DIRECTORY...)` rather than post-build commands for better cross-platform compatibility and incremental builds.

---

### 6.2. Open Issues Feedback (Section 5)

Please add your comments on the three open issues below. Reference the issue number (5.1, 5.2, 5.3) in your feedback.

# Codex Comments (Open Issues)
**On 5.1 (UI Placement):**
- Recommend a compact Picture-in-Picture panel docked to the status area by default, plus a full-screen toggle (F11). Avoid overlaying the Data View so library work remains readable.
- Visualizer behavior should be identical across Library and Mix Editor; it reflects the master output, not view-specific content.
- Add an "auto-pause on app background/minimize" option to reduce GPU load.

**On 5.2 (Unified Playback):**
- Keep two engines but formalize a single PlaybackController interface. The visualizer tap must live after the master FX chain (EQ/Reverb) so it represents final output for both engines.
- Default double-click should remain "play single track" (v1.x muscle memory). Add "Play All" and Shift+double-click to start a folder playlist.
- Queue should persist across navigation changes; it is a playback context, not a view context.
- Move next/previous behavior for Mix mode into the playback layer (not MainComponent UI) to avoid duplicate logic and desync with engine timing.
**Codex position:** I agree with implementing the unified playback list exactly as above; this is the next logical step and should be treated as the canonical behavior spec for Claude's tasking.

**On 5.3 (Preset Licensing):**
- Contact projectM maintainers for guidance and document the response.
- If licensing remains unclear, ship a small curated preset set with explicit licenses; provide a "Drop presets here" path for user additions.

# Gemini Comments (Open Issues)
*(Pending review)*

# Claude Comments (Open Issues)

**On 5.1 (UI Placement):**
- I lean toward Option B (separate resizable panel) or a hybrid: small PiP by default with double-click to expand, plus F11 for true full-screen.
- The visualizer should behave identically in Library and Mix Editor views — it's displaying the audio output, not view-specific content.
- Consider: should the visualizer pause/dim when app loses focus to save resources?

**On 5.2 (Unified Playback):**
- The two-engine architecture is sound. The key is ensuring `PlaybackController` presents a consistent interface regardless of which engine is active.
- For the "double-click plays folder" behavior: I'd default to single-track-only to match user expectations from v1.x, with an easy "Play All" button or Shift+double-click for playlist mode.
- The queue should survive folder navigation — don't clear it just because the user browses elsewhere. Display a subtle "Now Playing" indicator in navigation panel.
- The 3-second threshold for "Previous" is standard (matches Spotify, iTunes). Good choice.

**On 5.3 (Preset Licensing):**
- Recommend Option E first (contact projectM maintainers) — they likely have a clear position.
- Fallback to Option B (curated minimal set) if licensing is unclear.
- Avoid Option C (no presets) — the "wow factor" of out-of-box visuals is too valuable for UX.

---

## 7. Codex Detailed Proposal (Implementation-Ready)

This section turns the review into a concrete, end-to-end plan covering audio correctness, playback UX, UI wiring, and licensing.

### 7.1. Audio Pipeline (Correctness First)

**Goal:** The visualizer must reflect the *exact* audio output the user hears, regardless of playback mode.

**Decisions (target behavior):**
- The FIFO write happens in `PlaybackController::getNextAudioBlock()` *after* EQ/Reverb.
- `MixPlaybackEngine` does **not** write to the FIFO; it only provides raw mix output upstream.
- FIFO stores mono downmix (L+R)/2; stereo meters remain separate.

**Implementation detail (target behavior):**
- `PlaybackController::prepareToPlay()` sets FIFO sample rate to the device rate.
- If FIFO is full, writes are partial (extra samples are dropped) rather than blocking audio.
- Buffer size remains configurable via Settings; 2048 is acceptable today, with 8192 as an optional preset for slower render paths.

### 7.2. Playback Unification (Two Engines, One Interface)

**Goal:** Single-track preview and mix playback share a unified control surface.

**New playback layer:** `PlaybackController` owns a `PlaylistQueue` for playlist mode; mix mode continues to be driven by `MixPlaybackEngine`.

**Data model (playlist):**
- `PlaylistQueue` keeps `tracks`, `currentIndex`, `repeatMode`, `shuffleEnabled`, and `shuffleOrder`.
- `source` and `sourcePath` are retained for UI context (Now Playing).

**Controller API (additions):**
- `setPlaylist(PlaylistQueue queue)`
- `clearPlaylist()`
- `getPlaylistInfo()`
- `nextTrack()` / `previousTrack()`
- `handleTrackEnded()` (invoked by transport end callback)

**Behavior (playlist mode):**
- Double-click plays single track only (no queue) by default.
- "Play All" button or Shift+double-click builds a queue from the current folder view.
- Repeat-one: restart current track on Next.
- Repeat-all: wrap to index 0.
- Shuffle: iterate through `shuffleOrder` without repeating until exhausted, then reshuffle.

**Behavior (mix mode):**
- `nextTrack()` and `previousTrack()` seek to mix track start times.
- Track start times should be provided by `MixProjectLoader` or `MixPlaybackEngine` as a helper to avoid UI duplication.

### 7.3. UI Wiring and User Experience

**EnhancedPlayerComponent:**
- Show track index "3 of 12" when a playlist queue exists.
- Add repeat/shuffle toggles and reflect controller state.
- Display "Mix" indicator when in mix playback mode.

**DataViewComponent:**
- Highlight the currently playing track (by TrackId).
- Keep highlight even if user navigates elsewhere (queue persists).

**MainComponent:**
- On track end: call `PlaybackController::handleTrackEnded()` rather than UI-managed auto-advance.
- Next/Previous buttons call `PlaybackController::nextTrack()` / `previousTrack()` directly.

**Visualizer UI:**
- Default: small docked panel within status area (PiP).
- Full-screen toggle (F11) as a separate mode.
- Optional "Pause when app inactive" preference to save GPU.

### 7.4. Presets and Licensing

**Licensing plan:**
- Contact projectM maintainers and record guidance in docs.
- If uncertain: ship a curated, licensed subset (10-20 presets) with explicit attribution.
- Provide a clearly labeled folder for user presets and support runtime scanning.

**Distribution:**
- Use CMake `install(DIRECTORY ...)` for assets and presets instead of post-build copying.

### 7.5. Resilience and Performance

**OpenGL context:**
- Require GL 3.3+ (Core profile). Keep explicit log of context version and fallback message.
- On context loss, reinitialize projectM and reload presets.

**Frame rate:**
- Default 30 FPS; allow 60 FPS in settings.
- Disable continuous repainting and render on timer.

### 7.6. Test Plan (Manual)

1. Single-track playback: visualizer reacts, EQ/Reverb changes are reflected.
2. Mix playback: visualizer reacts, next/previous seek to track boundaries.
3. Toggle visualizer on/off: no audio glitches or UI freezes.
4. Switch audio device/sample rate: visualizer remains stable.
5. Preset cycling: next/previous/random works; handles missing preset folder gracefully.

---

## 8. Claude Review of Codex Proposal (Section 7)

**Overall:** Strong agreement. The proposal is well-structured and addresses all three open issues coherently. A few clarifications and implementation notes:

### 8.1. On Audio Pipeline (Section 7.1)

**Agreement:** Moving the FIFO write to `PlaybackController::getNextAudioBlock()` post-EQ/Reverb is the correct architectural choice. This:
- Creates a single, authoritative tap point for visualization
- Guarantees visualizer matches what the user hears (including EQ/Reverb effects)
- Eliminates the current asymmetry where only mix mode feeds the visualizer

**Implementation Note:** The current code has `MixPlaybackEngine::getNextAudioBlock()` writing to the FIFO (around line 515). This must be **removed** when implementing the new architecture to avoid double-writes or inconsistent behavior.

**On buffer size:** 8192 samples (170ms @ 48kHz) is reasonable. However, consider making this a compile-time constant in `AudioVisualizerFIFO.h` with a comment explaining the tradeoff:
```cpp
// Larger buffer = more resilience to render stalls, but increased visual latency
// 8192 samples @ 48kHz = ~170ms latency (acceptable for visualization)
static constexpr size_t kDefaultBufferSize = 8192;
```

### 8.2. On Playback Unification (Section 7.2)

**Agreement:** The two-engine approach with unified `PlaybackController` interface is sound.

**Clarification on `handleTrackEnded()`:** This should be triggered by JUCE's `AudioTransportSource::addChangeListener()` mechanism. When the transport position reaches the end, it fires a change callback. The listener should check `isPlaying()` and `getCurrentPosition() >= getTotalLength()` to detect natural track end vs. user stop.

**Edge case to document:** What happens if user is in mix mode and double-clicks a library track?
- Proposed behavior: Switch to playlist mode, unload mix, start single track
- The mix should be unloaded cleanly (stop playback, release resources) before loading the new track
- Consider a brief confirmation if mix has unsaved changes (future enhancement)

### 8.3. On UI Wiring (Section 7.3)

**Agreement:** PiP docked to status area is the right default.

**Additional consideration:** The visualizer toggle should be a clearly visible button (not buried in menus). Suggest adding it to the transport control area near play/pause, with a recognizable icon (waveform/spectrum icon).

**Keyboard shortcuts:**
- `V` - Toggle visualizer visibility
- `F11` - Toggle full-screen visualizer
- `N` - Next preset (when visualizer has focus or is visible)
- `P` or `Shift+N` - Previous preset

### 8.4. On Presets (Section 7.4)

**Agreement:** Contact maintainers first, fallback to curated set.

**Suggestion for user preset folder:** On first run, if the user preset folder is empty, show a one-time tooltip: "Drop .milk preset files here to add your own visualizations" pointing to the folder location.

### 8.5. On Test Plan (Section 7.6)

**Additional test cases:**
6. **Rapid mode switching:** Alternate between single-track and mix playback rapidly - no crashes or audio glitches
7. **Sample rate change mid-playback:** Change audio device while playing - visualizer should reinitialize gracefully
8. **Memory pressure:** Play for extended period (1+ hour) - no memory leaks in FIFO or projectM
9. **Context loss simulation:** Minimize/restore app repeatedly, switch monitors - visualizer recovers

### 8.6. Implementation Priority

Suggested order of implementation:
1. **Move FIFO tap to PlaybackController** (fixes visualizer for single-track immediately)
2. **Add playlist queue infrastructure** (data structure, basic next/prev)
3. **Wire up UI** (track highlight, index display)
4. **Visualizer UI refinement** (PiP positioning, F11 toggle)
5. **Preset licensing resolution** (can proceed in parallel)

---

**Status:** Ready for implementation pending human approval of the unified approach.
