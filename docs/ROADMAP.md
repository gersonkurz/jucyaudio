# JucyAudio Development Roadmap

## Current Working Reality (as of 2026-06-27)

- Active development happens on `main`. The 2.0 line (formerly `dev/2.0`) was consolidated onto
  `main` on 2026-06-27 and `dev/2.0` was deleted.
- `release/1.x` is retained for 1.x hotfixes (done only if real user-reported issues require them).
- 2.0 has not been tagged yet — see `docs/release-plan-2.0.md` for the remaining release gates.

## Release Criteria

**Version 2.0 is feature-complete.** All remaining MUST HAVE *features* are DONE. The full
Dedupe System was descoped to 2.1 (2026-06-07) — only the existing working-set metadata dedup
ships in 2.0. The remaining 2.0 work is the **MSI installer** (via the `msis` tool) plus a tail
of bug fixes; see `tasks.md` and `docs/release-plan-2.0.md`.

| ID   | Feature                | Priority      | Target | Status              |
|------|------------------------|---------------|--------|---------------------|
| 1.1  | ProjectM Integration   | MUST HAVE     | 2.0    | DONE                |
| 1.2  | VST3 Support           | MUST HAVE     | 2.0    | DONE                |
| 2.1  | User Manual            | NICE TO HAVE  | 2.x    |                     |
| 2.2  | Dedupe System          | MUST HAVE     | 2.1    | DESCOPED to 2.1     |
| 2.3  | Smart Automix          | MUST HAVE     | 2.0    | DONE                |
| 3.1  | AI Stem Separation     | NICE TO HAVE  | 2.2    |                     |
| 3.2  | AI Metadata Enrichment | NICE TO HAVE  | 2.1    |                     |
| 3.3  | Library Organizer      | NICE TO HAVE  | 2.x    |                     |
| 4.1  | Linux Port             | NICE TO HAVE  | 2.3    |                     |

> **2.2 Dedupe note:** 2.0 ships only the in-working-set metadata dedup ("Remove Duplicates",
> keyed on artist/album/title/bpm/duration). SHA-256 file hashing, Chromaprint fingerprinting,
> and the library-wide review/marking system (`docs/features/dedupe.md`) move to 2.1.

---

## Branch Structure (current)

```
main          ← active development + the 2.0 line (dev/2.0 was merged here and deleted)
release/1.x   ← retained for 1.x hotfixes (forward-port to main if still relevant)
```

`dev/2.0` no longer exists — its 79 commits plus the forward-ported accidental-reorder hotfix
were consolidated onto `main` on 2026-06-27. The undo-deadlock 1.x hotfix was already solved
independently in the 2.0 code, so it was not re-applied.

### Forward-Porting Workflow (only when 1.x hotfixes exist)

When a bug is fixed in `release/1.x`:

1. **Fix in `release/1.x`** → tag and release (e.g., 1.1.1)
2. **Cherry-pick to `main`**:
   ```bash
   git checkout main
   git cherry-pick -x <commit-hash>   # -x records the original commit
   ```
3. **Resolve conflicts** if the 2.0 code has diverged — and first check whether the fix is already
   present in a different form (as the undo-deadlock fix was). Document adaptations in the message.

### Remaining steps to cut 2.0

1. Close out the release gates in `docs/release-plan-2.0.md` (stabilization, QA).
2. Tag `v2.0.0` on `main`.
3. Create `release/2.x` for future 2.0.x maintenance.

---

## Phase 1: Immediate Impact (Visuals & FX)

**Goal:** Drastically improve the user experience and creative possibilities with "low hanging fruit" integrations.

1.1. **ProjectM Integration** — DONE
    - Hardware-accelerated music visualization using projectM v4
    - Three layout modes (Bottom/Left/Right), automatic preset switching
    - Unified playback architecture feeds visualizer from both single-track and mix modes
    - Ships with ~9,800 curated presets (Cream of the Crop collection)
1.2. **VST3 Support** (`docs/features/vst-integration.md`) — DONE
    - **Why**: Unlocks infinite audio processing possibilities (EQ, Compression, creative FX) using the industry standard.
    - **Tech**: JUCE Plugin Hosting, VST3 SDK (MIT).

## Phase 2: Core Enhancements & Usability

**Goal**: Refine the existing toolset and help users manage their libraries better.

2.1. **User Manual** (`docs/features/user-manual.md`) — NICE TO HAVE
    - **Why**: Essential for onboarding. Low technical risk.
    - **Tech**: MkDocs, GitHub Pages.
2.2. **Dedupe System** (`docs/features/dedupe.md`) — DESCOPED to 2.1
    - **Why**: Solves a major pain point for users with large libraries.
    - **Tech**: SHA-256, Chromaprint.
    - **2.0 status**: Only the working-set metadata dedup ships in 2.0. Full system moved to 2.1
      (2026-06-07) — it was the riskiest, least-started MUST-HAVE and was blocking an otherwise
      feature-complete release.
2.3. **Smart Automix** (`docs/features/automix-improvement.md`) — DONE
    - **Why**: Upgrades the "Auto-DJ" from a toy to a useful tool.
    - **Tech**: BTrack (Beat Detection), SoundTouch (Time Stretch).

## Phase 3: AI & Advanced Features (Medium Term)

**Goal**: Introduce cutting-edge features that require external dependencies or APIs.

3.1. **AI Stem Separation** (`docs/features/stems.md`) — NICE TO HAVE
    - **Why**: The "Killer Feature" of modern DJ software.
    - **Tech**: ONNX Runtime, Demucs.
3.2. **AI Metadata Enrichment** (`docs/features/enrich.md`) — NICE TO HAVE
    - **Why**: Automates the tedious task of tagging.
    - **Tech**: Python, Claude API / Ollama (Local).
3.3. **Library Organizer** (`docs/features/library-org.md`) — NICE TO HAVE
    - **Why**: Physical file management based on metadata.
    - **Tech**: C++ File Operations.

## Phase 4: Platform Expansion

**Goal**: Give back something to the original GPL crew.

4.1 **Linux Port** (`docs/features/linux-port.md`) — NICE TO HAVE
    - **Why**: Open-source ethos, but I am not using Linux on the Desktop, so why would I care?
    - **Tech**: Flatpak, ALSA/PipeWire.


## Unsorted ideas, not planned in at this stage

- Internationalization support: architecture for language translation
- Evaluate feasability of theme support
- 
