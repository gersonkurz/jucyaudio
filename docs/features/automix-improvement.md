# Smart Automix: Intelligent Transition Point Discovery

## Goal

Improve the automix algorithm to find **optimal AttachTo/AttachFrom points** when creating offline mixes, replacing the current fixed-duration crossfade with musically-aware transitions.

## Problem Statement

The current automix uses a fixed crossfade (approximately 5 seconds) regardless of musical content. This results in:

- Transitions that cut across phrases awkwardly
- Energy mismatches (loud section fading into quiet, or vice versa)
- No awareness of where tracks naturally "want" to blend

**Target genres**: Post-metal, shoegaze, noise rock, coldwave, dark ambient — music that doesn't conform to traditional DJ/EDM structures. Beat-grid matching is often irrelevant; energy flow and phrase boundaries matter more.

## Core Concept

### What This Feature Does

1. **Analyzes each track lazily** (at mix creation time, only if missing data):
   - Energy contour over time
   - Natural phrase boundaries
   - Good "intro zones" and "outro zones" for blending

2. **Calculates optimal transitions** between adjacent tracks:
   - Finds where Track A's outro energy matches Track B's intro energy
   - Aligns to phrase boundaries where possible
   - Determines appropriate crossfade duration per transition

3. **Outputs better cue points** — the result is improved AttachTo/AttachFrom values, not real-time audio manipulation

### What This Feature Does NOT Do

- No real-time tempo matching or time-stretching
- No "master BPM" or DJ-style beat sync
- No live mixing decisions — this is purely for offline mix creation
- No changes to BPM analysis — that remains separate and unchanged

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      MIX CREATION FLOW                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Working Set ──► Create Mix ──► For each track:                 │
│                                                                 │
│                    ┌─────────────────────────────────┐          │
│                    │ Has energy data in DB?          │          │
│                    └─────────────────────────────────┘          │
│                         │              │                        │
│                        YES            NO                        │
│                         │              │                        │
│                         │    ┌────────────────────┐             │
│                         │    │ Read audio file    │             │
│                         │    │ Calculate energy   │             │
│                         │    │ Store in DB        │             │
│                         │    └────────────────────┘             │
│                         │              │                        │
│                         ▼              ▼                        │
│                    ┌─────────────────────────────────┐          │
│                    │ Run TransitionCalculator        │          │
│                    │ for each adjacent pair          │          │
│                    └─────────────────────────────────┘          │
│                                   │                             │
│                                   ▼                             │
│                    ┌─────────────────────────────────┐          │
│                    │ Apply optimal AttachTo/AttachFrom│         │
│                    │ to mix tracks                    │         │
│                    └─────────────────────────────────┘          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Lazy Evaluation Strategy

Analysis happens **only when needed**:

| Scenario | What Happens |
|----------|--------------|
| New track in mix | Read audio, calculate energy, cache in DB |
| Previously analyzed track | Use cached data (instant) |
| Track removed from mix | Recalculate transition for newly-adjacent tracks |

**Benefits**:
- No upfront analysis of entire library (1.4M+ tracks)
- Only tracks that actually appear in mixes get analyzed
- First mix with a track: slight delay (~2-3 sec per track)
- Subsequent mixes: instant (data cached)

## Database Storage

### Reusing Existing Columns (No Schema Migration)

The `Tracks` table already has columns we can repurpose:

| Column | Current State | New Purpose |
|--------|---------------|-------------|
| `intro_end` | 0.1% populated (broken) | Intro zone end (ms) |
| `outro_start` | 0.9% populated (broken) | Outro zone start (ms) |
| `beat_locations_json` | 0% populated (empty) | Energy analysis JSON |

The existing intro/outro detection is broken (only analyzed middle 60 seconds of tracks). We'll replace it with correct analysis.

### Energy Data Format

Store in `beat_locations_json` column:

```json
{
  "version": 1,
  "energy_contour": [0.12, 0.15, 0.18, 0.22, ...],
  "phrase_boundaries": [45000, 92000, 138000, ...],
  "analysis_timestamp": 1706812800
}
```

- `energy_contour`: RMS values at 1-second intervals, normalized 0.0-1.0
- `phrase_boundaries`: Timestamps (ms) of detected phrase changes
- `version`: For future algorithm updates (can re-analyze if version changes)

**Storage size**: ~1-2 KB per track (vs ~104 KB for waveform cache). Negligible.

## Track Analysis Details

### Energy Contour Calculation

```cpp
// Read FULL audio file (not just middle 60 seconds like broken code)
// Calculate RMS energy in 1-second windows
for each 1-second window:
    rms = sqrt(sum(samples^2) / num_samples)
    normalized = rms / track_peak_rms
    energy_contour.push_back(normalized)
```

### Phrase Boundary Detection

- Look for significant energy changes (>30% delta over 2-3 seconds)
- Detect silence or near-silence gaps (< -40dB)
- Minimum spacing: 4 seconds between boundaries

### Intro/Outro Zone Identification

- **Intro zone end**: Point where energy first exceeds 60% of track average (within first 25% of track)
- **Outro zone start**: Point where energy drops below 60% of track average (within last 25% of track)
- Default fallback: first/last 15% of track if no clear boundary

## Transition Matching Algorithm

Given Track A (outgoing) and Track B (incoming):

```
1. Get A's outro zone and B's intro zone
2. For each candidate crossfade point:
   a. Compute energy difference at crossover
   b. Check if near a phrase boundary in both tracks
   c. Score: energy_match * 0.7 + phrase_alignment * 0.3
3. Select best candidate
4. Determine crossfade duration:
   - Short (2-3s) if energy levels match well
   - Medium (5-8s) if moderate mismatch
   - Long (10-15s) if blending quiet/ambient sections
5. Output: AttachFrom, AttachTo, CrossfadeDuration
```

### Energy Matching Intuition

```
Track A energy:  ████████████▇▇▅▅▃▃▂▁▁
Track B energy:  ▁▁▂▂▃▃▅▅▇▇████████████
                         ↑
                   Best crossover point
                   (energy levels similar)
```

## Recalculation on Track Removal

When the user removes Track B from sequence [A → B → C]:

1. Detect that A and C are now adjacent
2. Retrieve stored analysis for A and C (already cached)
3. Run transition matching algorithm for (A, C)
4. Update AttachFrom/AttachTo values automatically
5. Optionally notify user: "Transition A→C recalculated"

This should be fast (<100ms) since analysis is pre-computed.

## Code Changes Required

### Remove (Cleanup)

From `AudioAnalysis.cpp`:
- Remove broken `detectIntro()` / `detectOutro()` functions
- Remove `calculateEnergyFrames()` (will be reimplemented properly)
- Keep `detectBPM()` unchanged (uses SoundTouch, works fine)

From `AudioMetadata` struct:
- Remove `hasIntro`, `hasOutro`, `introStart`, `introEnd`, `outroStart`, `outroEnd`
- BPM fields remain unchanged

From `BpmAnalysisTask`:
- Remove intro/outro detection calls
- Keep BPM-only analysis (works correctly on middle 60 seconds)

### Add (New Code)

1. **`EnergyAnalyzer` class** — calculates energy contour from audio buffer
2. **`TransitionCalculator` class** — finds optimal transition points
3. **Integration in mix creation** — lazy analysis + transition calculation

## Roadmap

### Phase 1: Cleanup & Energy Analyzer

**Goal**: Remove broken code, implement correct energy analysis

- [ ] Remove broken intro/outro detection from `AudioAnalysis.cpp`
- [ ] Create `EnergyAnalyzer` class (reads full track, computes energy contour)
- [ ] Implement phrase boundary detection
- [ ] Store results in `beat_locations_json` column
- [ ] Unit tests with known audio files

**Deliverable**: Can analyze a track and store energy data correctly

### Phase 2: Transition Calculator

**Goal**: Algorithm to find optimal transition points between two tracks

- [ ] Create `TransitionCalculator` class
- [ ] Implement energy-matching algorithm
- [ ] Implement phrase-boundary snapping
- [ ] Implement dynamic crossfade duration calculation
- [ ] Unit tests with synthetic energy curves

**Deliverable**: Given two tracks with energy data, returns optimal AttachFrom/AttachTo/CrossfadeDuration

### Phase 3: Mix Integration

**Goal**: Use smart transitions when creating mixes

- [ ] Hook lazy analysis into mix creation flow
- [ ] Check for existing energy data, analyze if missing
- [ ] Hook `TransitionCalculator` into mix track sequencing
- [ ] Replace fixed 5-second crossfade with calculated values
- [ ] Implement recalculation on track removal
- [ ] Add user preference: "Use smart transitions" (default on, can disable)

**Deliverable**: Automix creates better-sounding transitions automatically

### Phase 4: Refinement (Optional)

**Goal**: Polish and edge cases

- [ ] Handle tracks with no clear intro/outro (constant energy)
- [ ] Spectral analysis for ambient/drone tracks (frequency-based matching)
- [ ] UI visualization of energy contour in mix editor
- [ ] Allow user to manually adjust suggested points (with "reset to suggested" option)
- [ ] A/B comparison: play transition with old vs new points

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Phrase detection fails on ambient/drone | Fall back to energy-only matching; add spectral analysis in Phase 4 |
| First-time analysis slows mix creation | Show progress dialog; analysis is ~2-3 sec per track |
| User prefers manual control | Provide toggle to disable smart transitions |
| Calculated transitions sound worse than fixed | Always allow manual override; consider A/B preview |

## Success Criteria

1. Transitions sound more natural to the user's ear
2. Less manual adjustment of AttachTo/AttachFrom needed
3. Removing a track from a mix doesn't require manually fixing adjacent transitions
4. No noticeable delay for mixes with previously-analyzed tracks

## Non-Goals (Future Work)

These are explicitly out of scope for this feature:
- Real-time tempo/beat synchronization
- Time-stretching or pitch-shifting
- Key detection and harmonic mixing
- Intelligent track ordering ("DJ brain")
- Live DJ playback mode
- Changes to BPM analysis workflow

These could be separate features later, but this document focuses solely on finding better transition points for offline mixes.

## Relationship to Existing Code

| Component | Status | Notes |
|-----------|--------|-------|
| `BpmAnalysisTask` | Keep as-is | BPM detection works, runs on working set creation |
| `AudioAnalysis.cpp` intro/outro | Remove | Broken (middle 60s only), will be replaced |
| `WaveformLoadingTask` | Keep as-is | Separate concern, runs when viewing mix |
| `intro_end` / `outro_start` columns | Repurpose | Currently broken data, will be overwritten |
| `beat_locations_json` column | Repurpose | Currently empty, will store energy data |
