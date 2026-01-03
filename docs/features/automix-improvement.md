# Automix Improvement Plan

## Goal
Upgrade the current "Auto-DJ" functionality from a simple crossfade sequencer to a beat-aware, intelligent mixing system.

## 1. Analysis of Current Implementation
*   **Current Logic**: `MixProjectLoader::calculateMixDuration` simply sequences tracks based on `AttachTo` / `AttachFrom` points.
*   **Transitions**: Uses linear crossfades defined by `envelopePoints`.
*   **Limitations**:
    *   No tempo matching (tracks drift apart).
    *   No phase alignment (kick drums may clash).
    *   Transitions are fixed length, regardless of musical phrasing.

## 2. Proposed "Smart Automix" Architecture

We will implement a multi-stage pipeline:

### Stage 1: Analysis (Offline/Background)
We already use **SoundTouch** for BPM detection (`BpmAnalysisTask`). We need to extend this to extract a **Beat Grid**.
*   **Library**: `BTrack` (GPL compatible). It is a lightweight, header-only C++ library for real-time beat tracking, making it much easier to integrate on Windows than `aubio`.
*   **Storage**: Store `BeatGrid` (vector of timestamps) in the `Tracks` database table (as a BLOB or separate table).

### Stage 2: Selection & Ordering (The "DJ Brain")
Instead of random or alphabetical order, implement heuristics:
*   **Key Compatibility**: Use the Circle of Fifths (we need a Key Detection library, `keyfinder` is common).
*   **Tempo Proximity**: Prefer mixing 120 BPM -> 122 BPM over 120 -> 140.
*   **Energy Level**: calculated from RMS/LUFS.

### Stage 3: The Mixing Engine (Real-time)
We need to enhance `MixPlaybackEngine` to perform **Time Stretching** on the fly.
*   **Master Clock**: The engine defines a "Master BPM".
*   **Sync**: Tracks are time-stretched (using SoundTouch, which we already link) to match the Master BPM.
*   **Phase Lock**: The engine aligns the *downbeats* (Beat 1) of the outgoing and incoming tracks.

## 3. Technology Stack Selection

*   **Tempo/Pitch Shifting**: **SoundTouch** (Already integrated). It is industry standard for high-quality time stretching.
*   **Beat/Onset Detection**: **BTrack**. Selected for being lightweight and header-only, avoiding the complex build requirements of `aubio` on Windows.
*   **Key Detection**: **libKeyFinder** (GPL).

## 4. Implementation Steps

### Phase 1: Enhanced Analysis
1.  [ ] Add `BTrack` to the project (header-only, so just copy to `Database/Includes` or `FetchContent`).
2.  [ ] Update `BpmAnalysisTask` to also extract:
    *   Precise Beat Grid (vector of times).
    *   Downbeat locations (measure starts).
3.  [ ] Update SQLite schema to store this metadata.

### Phase 2: The "Sync" Button
1.  [ ] Update `PlaybackTrackSource` to own a `soundtouch::SoundTouch` processor.
2.  [ ] Implement logic: `PlaybackRate = TargetBPM / TrackBPM`.
3.  [ ] Feed audio through SoundTouch before the resampler.

### Phase 3: Intelligent Transitions
1.  [ ] Create `AutomixGenerator` class.
2.  [ ] Implement `findBestTransitionPoint(Track A, Track B)`:
    *   Aligns Outro of A with Intro of B.
    *   Ensures beats are phase-aligned.
    *   Creates a dynamic Crossfade Envelope (EQ-based mixing is better than volume-based).

## 5. Risks
*   **CPU Load**: Real-time time-stretching 2 tracks is heavy.
    *   *Mitigation*: Pre-calculate (render) transitions if CPU is low, or use efficient SoundTouch settings.
*   **Accuracy**: Beat detection can be wrong (half-time/double-time errors).
    *   *Mitigation*: Allow users to manually correct the Beat Grid in the `TrackEditor`.

## 6. Conclusion
This is a feasible, high-value upgrade. We have the core tech (SoundTouch) and just need the glue logic and better analysis (BTrack).
