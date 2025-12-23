# JucyAudio - AI Development Guide

**JucyAudio** is an audio curation and mixing application for macOS (Apple Silicon) and Windows. Core logic is standard C++20, with JUCE for the UI layer.

## Collaboration Style

- **Discussion-first, code-later:** Explore the *why* before the *how*.
- **Library-first philosophy:** Core logic in decoupled libraries:
  - `jucyaudio::database`: Database logic, data models, background tasks (standard C++20)
  - `jucyaudio::audio`: Audio processing, analysis, exporting
  - `jucyaudio::ui`: JUCE-based frontend components
  - `jucyaudio::config`: TOML-backed configuration
- **Modern C++20:** Prefer `{}` initializers, `const auto`/`auto`, modern idioms.
- **No apologies or sycophancy** - focus on tasks ahead.

## Core Architecture

**The "Pure Cache" Model:**
- `Folders` table with `parent_id` and cached `path` column for hierarchy
- `Tracks` table stores `filename` and `folder_id` only
- Case-insensitive lookups via `normalizeForCache()` (ICU-powered)
- In-memory cache built on startup for instant navigation

**Navigation System:**
- Manual retain/release with atomic reference counting
- Match retains with releases!

## For AI Assistants

- **NEVER run or build the application yourself** - it's a GUI app requiring human interaction
- After code changes, ask the user to build and test
- The human will test UI features and report back issues

