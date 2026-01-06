# JucyAudio Development Roadmap

## Release Criteria

**Version 2.0 requires all MUST HAVE features. NICE TO HAVE features can ship in 2.x minor releases.**

| ID   | Feature              | Priority      | Target |
|------|----------------------|---------------|--------|
| 1.1  | ProjectM Integration | MUST HAVE     | 2.0    |
| 1.2  | VST3 Support         | MUST HAVE     | 2.0    |
| 2.1  | User Manual          | NICE TO HAVE  | 2.1    |
| 2.2  | Dedupe System        | NICE TO HAVE  | 2.1    |
| 2.3  | Smart Automix        | MUST HAVE     | 2.0    |
| 3.1  | AI Stem Separation   | NICE TO HAVE  | 2.2    |
| 3.2  | AI Metadata Enrichment | NICE TO HAVE | 2.1   |
| 3.3  | Library Organizer    | MUST HAVE     | 2.0    |
| 4.1  | Linux Port           | NICE TO HAVE  | 2.3    |

---

## Migration Strategy (1.x → 2.x)

### Branch Structure

```
main          ← 1.x stable releases (1.0.1, 1.0.2, ...)
  │
  └─ release/1.x  ← maintenance branch for 1.x hotfixes

dev/2.0       ← 2.0 development (feature work)
```

### Forward-Porting Workflow

When a bug is fixed in 1.x:

1. **Fix in `release/1.x`** → Tag and release (e.g., 1.0.3)
2. **Cherry-pick to `dev/2.0`**:
   ```bash
   git checkout dev/2.0
   git cherry-pick -x <commit-hash>   # -x adds reference to original commit
   ```
3. **Resolve conflicts** if 2.0 code has diverged. Document any adaptations in the commit message.

### Conflict Prevention

- Keep 1.x fixes **minimal and surgical** - avoid refactoring in hotfix commits
- Tag forward-ported commits with `[forward-port]` prefix in commit message
- Maintain a `CHANGELOG-2.0.md` noting which 1.x fixes are included

### When 2.0 Ships

1. Merge `dev/2.0` → `main`
2. Create `release/2.x` branch for future 2.0.x maintenance
3. Archive `release/1.x` (read-only, security fixes only)

---

## Phase 1: Immediate Impact (Visuals & FX)

**Goal:** Drastically improve the user experience and creative possibilities with "low hanging fruit" integrations.

1.1. **ProjectM Integration** (`docs/features/projectm-integration.md`) — MUST HAVE
    - **Why**: Brings professional-grade, hardware-accelerated visuals. High "wow" factor, straightforward integration via CMake.
    - **Tech**: OpenGL, projectM v4.
1.2. **VST3 Support** (`docs/features/vst-integration.md`) — MUST HAVE
    - **Why**: Unlocks infinite audio processing possibilities (EQ, Compression, creative FX) using the industry standard.
    - **Tech**: JUCE Plugin Hosting, VST3 SDK (MIT).

## Phase 2: Core Enhancements & Usability

**Goal**: Refine the existing toolset and help users manage their libraries better.

2.1. **User Manual** (`docs/features/user-manual.md`) — NICE TO HAVE
    - **Why**: Essential for onboarding. Low technical risk.
    - **Tech**: MkDocs, GitHub Pages.
2.2. **Dedupe System** (`docs/features/dedupe.md`) — NICE TO HAVE
    - **Why**: Solves a major pain point for users with large libraries.
    - **Tech**: SHA-256, Chromaprint.
2.3. **Smart Automix** (`docs/features/automix-improvement.md`) — MUST HAVE
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