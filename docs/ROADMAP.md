# JucyAudio Development Roadmap

## Current Working Reality (as of 2026-03-07)

- Active development happens on `dev/2.0`.
- `main` is not receiving ongoing feature work during 2.0 development.
- 1.x hotfixes are done only if real user-reported issues require them.

## Release Criteria

**Version 2.0 requires all MUST HAVE features. NICE TO HAVE features can ship in 2.x minor releases.**

| ID   | Feature                | Priority      | Target | Status |
|------|------------------------|---------------|--------|--------|
| 1.1  | ProjectM Integration   | MUST HAVE     | 2.0    | DONE   |
| 1.2  | VST3 Support           | MUST HAVE     | 2.0    | DONE   |
| 2.1  | User Manual            | MUST HAVE     | 2.0    |        |
| 2.2  | Dedupe System          | MUST HAVE     | 2.0    |        |
| 2.3  | Smart Automix          | MUST HAVE     | 2.0    | DONE   |
| 3.1  | AI Stem Separation     | NICE TO HAVE  | 2.2    |        |
| 3.2  | AI Metadata Enrichment | NICE TO HAVE  | 2.1    |        |
| 3.3  | Library Organizer      | MUST HAVE     | 2.0    |        |
| 4.1  | Linux Port             | NICE TO HAVE  | 2.3    |        |

---

## Migration Strategy (1.x → 2.x)

This section reflects the intended release flow. Current day-to-day work remains on `dev/2.0` until 2.0 is ready to ship.

### Branch Structure

```
main          ← stable releases only (currently no active 1.x stream)
  │
  └─ release/1.x  ← optional hotfix branch, created only if needed

dev/2.0       ← 2.0 development (feature work)
```

### Optional Forward-Porting Workflow (only when 1.x hotfixes exist)

When a bug is fixed in 1.x:

1. **Fix in `release/1.x`** → Tag and release (e.g., 1.0.3)
2. **Cherry-pick to `dev/2.0`**:
   ```bash
   git checkout dev/2.0
   git cherry-pick -x <commit-hash>   # -x adds reference to original commit
   ```
3. **Resolve conflicts** if 2.0 code has diverged. Document any adaptations in the commit message.

### Conflict Prevention (if forward-porting is active)

- Keep 1.x fixes **minimal and surgical** - avoid refactoring in hotfix commits
- Tag forward-ported commits with `[forward-port]` prefix in commit message
- Maintain a `CHANGELOG-2.0.md` noting which 1.x fixes are included

### When 2.0 Ships

1. Merge `dev/2.0` → `main`
2. Create `release/2.x` branch for future 2.0.x maintenance
3. Archive `release/1.x` if it was created

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

2.1. **User Manual** (`docs/features/user-manual.md`) — MUST HAVE
    - **Why**: Essential for onboarding. Low technical risk.
    - **Tech**: MkDocs, GitHub Pages.
2.2. **Dedupe System** (`docs/features/dedupe.md`) — MUST HAVE
    - **Why**: Solves a major pain point for users with large libraries.
    - **Tech**: SHA-256, Chromaprint.
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
3.3. **Library Organizer** (`docs/features/library-org.md`) — MUST HAVE
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
