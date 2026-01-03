# JucyAudio - User Manual & Documentation Strategy

## Goal
Provide comprehensive, searchable, and visually appealing documentation for end-users, accessible both online (web) and offline (PDF/In-App).

## 1. Technology Selection

We will use **MkDocs** with the **Material for MkDocs** theme.

**Why?**
*   **Markdown-based**: Developers and contributors can easily write docs alongside code.
*   **Modern UI**: "Material for MkDocs" is the industry standard for beautiful, responsive OSS documentation.
*   **Search**: Built-in, fast client-side search.
*   **PDF Support**: via `mkdocs-with-pdf` plugin, allowing us to bundle a manual with the installer.
*   **Hosting**: Free via GitHub Pages.

## 2. Directory Structure

```
jucyaudio/
├── docs/               # Source markdown files
│   ├── assets/         # Screenshots, diagrams
│   ├── guide/          # User Guide
│   │   ├── installation.md
│   │   ├── library.md
│   │   ├── mixing.md
│   │   └── vtags.md
│   ├── features/       # (Our current feature plans move here)
│   ├── dev/            # Developer docs (building, architecture)
│   └── index.md        # Landing page
├── mkdocs.yml          # Configuration
└── .github/workflows/docs.yml
```

## 3. Implementation Steps

### Phase 1: Setup
1.  [ ] Create `mkdocs.yml` configuration.
2.  [ ] Set up `requirements.txt` (`mkdocs-material`, `mkdocs-with-pdf`).
3.  [ ] Create basic `index.md` and move existing Markdown files into `docs/`.

### Phase 2: Content Creation (The "Manual")
We need to write the following chapters:
1.  **Getting Started**: Installation, Audio Setup, Importing Music.
2.  **Library Management**: Database, Virtual Tags, Search.
3.  **The Mix Editor**: Timeline, Cues, Envelopes, Export.
4.  **Advanced Features**:
    *   **Auto-DJ**: How to use the Smart Automix.
    *   **Visuals**: Setting up ProjectM.
    *   **Plugins**: Loading VST3 effects.

### Phase 3: Deployment Pipeline
1.  [ ] **Web**: Configure GitHub Actions to build `gh-pages` branch on push to `main`.
2.  [ ] **Offline**: Configure CMake to:
    *   Check for `mkdocs`.
    *   Run `mkdocs build`.
    *   Install the generated PDF/HTML into the app's `Resources` folder.
3.  [ ] **In-App**: Add a "Help" menu item in JucyAudio that opens the local HTML or PDF.

## 4. Configuration (`mkdocs.yml` snippet)

```yaml
site_name: JucyAudio
theme:
  name: material
  palette: 
    scheme: slate  # Dark mode by default
  features:
    - navigation.tabs
    - search.suggest
    - search.highlight

plugins:
  - search
  - with-pdf:
      cover: true
      cover_title: JucyAudio User Manual
```

## 5. Maintenance
*   **Versioning**: We can use `mike` to manage versioned docs (v1.0, v2.0).
*   **Screenshots**: Need a standardized way to capture UI screenshots (e.g., a specific window size).

## 6. Risks
*   **Outdated Docs**: Code changes fast.
    *   *Mitigation*: Require doc updates in PR checklist.
*   **PDF Formatting**: PDF generation from HTML is often finicky.
    *   *Mitigation*: Keep layouts simple. Use `page-break` CSS classes.

## 7. Conclusion
MkDocs allows us to treat documentation like code. It's low-friction and high-quality.
