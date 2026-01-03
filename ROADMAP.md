# JucyAudio Development Roadmap

## Phase 1: Immediate Impact (Visuals & FX)
**Goal:** Drastically improve the user experience and creative possibilities with "low hanging fruit" integrations.

1.  **ProjectM Integration** (`projectm-integration.md`)
    *   **Why**: Brings professional-grade, hardware-accelerated visuals. High "wow" factor, straightforward integration via CMake.
    *   **Tech**: OpenGL, projectM v4.
2.  **VST3 Support** (`docs/vst-integration.md`)
    *   **Why**: Unlocks infinite audio processing possibilities (EQ, Compression, creative FX) using the industry standard.
    *   **Tech**: JUCE Plugin Hosting, VST3 SDK (MIT).

## Phase 2: Core Enhancements & Usability
**Goal**: Refine the existing toolset and help users manage their libraries better.

3.  **User Manual** (`docs/features/user-manual.md`)
    *   **Why**: Essential for onboarding. Low technical risk.
    *   **Tech**: MkDocs, GitHub Pages.
4.  **Dedupe System** (`docs/features/dedupe.md`)
    *   **Why**: Solves a major pain point for users with large libraries.
    *   **Tech**: SHA-256, Chromaprint.
5.  **Smart Automix** (`docs/automix-improvement.md`)
    *   **Why**: Upgrades the "Auto-DJ" from a toy to a useful tool.
    *   **Tech**: BTrack (Beat Detection), SoundTouch (Time Stretch).

## Phase 3: AI & Advanced Features (Medium Term)
**Goal**: Introduce cutting-edge features that require external dependencies or APIs.

6.  **AI Stem Separation** (`docs/features/stems.md`)
    *   **Why**: The "Killer Feature" of modern DJ software.
    *   **Tech**: ONNX Runtime, Demucs.
7.  **AI Metadata Enrichment** (`docs/features/enrich.md`)
    *   **Why**: Automates the tedious task of tagging.
    *   **Tech**: Python, Claude API / Ollama (Local).
8.  **Library Organizer** (`docs/features/library-org.md`)
    *   **Why**: Physical file management based on metadata.
    *   **Tech**: C++ File Operations.

## Phase 4: Platform Expansion
**Goal**: Broaden the user base after the core feature set is mature.

9.  **Linux Port** (`docs/linux-port.md`)
    *   **Why**: Open-source ethos, but lower immediate ROI than features.
    *   **Tech**: Flatpak, ALSA/PipeWire.

# Codex Comments
- Roadmap links point to `projectm-integration.md` and `docs/vst-integration.md`, but the actual docs are under `docs/features/`; consider updating paths.
- Consider adding per-item status/owner to track progress and dependencies.
