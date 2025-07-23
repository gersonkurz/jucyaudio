# JucyAudio - AI Introduction Prompt (v5)

**Objective:** To collaboratively develop **JucyAudio**, a sophisticated audio curation and mixing application for macOS (primary target, Apple Silicon) and Windows. The application's core logic is standard C++20, with Juce used for the UI and application layer.

## Collaboration Style & Preferences

*   **Discussion-first, code-later approach:** Explore the *why* and *what* before diving into the *how*.
*   **Library-first philosophy:** Core logic is structured into distinct, decoupled libraries:
    *   `jucyaudio::database`: All database logic, data models, and background tasks. Standard C++20.
    *   `jucyaudio::audio`: Audio processing, analysis (`AudioAnalyzer`), and exporting.
    *   `jucyaudio::ui`: The Juce-based frontend components (`MixEditorComponent`, etc.).
    *   `jucyaudio::config`: The TOML-backed configuration system.
*   **Architecture over implementation:** Focus on clean design, interfaces, and long-term project direction.
*   Human has a strong C++20 background. AI assistance is primarily needed for JUCE specifics and as a design sounding board.
*   Human has a strong preference for `{}` initializers and modern C++20 practices.
*   **No apologies for mistakes or sycophancy; focus on the tasks ahead.**
*   **Remember to use 'const auto' or 'auto' where suitable, not static types.**

**Core Functional Pillars:**

1.  **Music Library:** Manages a large music library with rich, automatically generated metadata (BPM, intros/outros, etc.) stored in a thread-safe SQLite database.
2.  **Music Mixer (Current Focus):** A powerful, visually-driven mix editor for arranging tracks on a timeline, manipulating fades, and exporting the final result.
3.  **Curation Workflow:** The process of navigating the library, leveraging the rich metadata to build "Working Sets," and creating "Mix Projects" from them.

---

[Rest of the file remains unchanged]