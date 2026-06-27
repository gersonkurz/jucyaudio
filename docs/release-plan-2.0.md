# JucyAudio 2.0 Joint Release Plan

Date: 2026-03-07 (branch policy updated 2026-06-27)
Branch baseline: `main` (the 2.0 line was consolidated from `dev/2.0` onto `main` on 2026-06-27; `dev/2.0` deleted)
Scope: align and ship 2.0 with clear cross-agent review.

## 1. Objective

Ship 2.0 once all MUST-HAVE features are complete and a minimum quality gate is met.

## 2. Remaining 2.0 Work

2.0 is **feature-complete**. What remains is packaging plus a bug tail:

- Windows MSI installer via the `msis` tool (`C:\NGBT\MSIS\msis-3.x`, WiX 6 backend) — replaces the
  legacy NSIS scripts under `setup/*.nsi`. See Section 5.
- Bug fixes tracked in `tasks.md`.

**Completed MUST-HAVE Features:**

- 1.1 ProjectM Integration
- 1.2 VST3 Support
- 2.3 Smart Automix

**Deferred to 2.x:**

- 2.1 User Manual — low demand, high manual effort (screenshots, etc.)
- 2.2 Dedupe System (full) — descoped to 2.1 (2026-06-07); 2.0 keeps only the working-set metadata
  dedup. SHA-256/Chromaprint/library review move to 2.1.
- 3.3 Library Organizer — design not mature enough for 2.0 scope

Reference: `docs/ROADMAP.md`

## 3. Release Gates (minimum)

- Feature completeness: all MUST-HAVE items marked done.
- Build health: successful non-GUI builds on Windows and macOS paths.
- Packaging health: release artifacts generated for target architectures.
- Manual QA: core GUI workflows smoke-tested by a human.
- Documentation consistency: roadmap/changelog/features reflect shipped state.

## 4. Branch And Merge Policy

- Active implementation is on `main` (the 2.0 line was consolidated from `dev/2.0` onto `main` on
  2026-06-27; `dev/2.0` is deleted).
- 1.x hotfix flow is optional and only activated if real issues are reported; forward-port from
  `release/1.x` to `main`.
- 2.0 ship cut (remaining):
  1. Stabilization window on `main`.
  2. Tag `v2.0.0`.
  3. Create `release/2.x` for 2.0.x maintenance.

## 5. Workstreams

1. Dedupe System
- Finalize matching strategy (hash + acoustic constraints where needed).
- Add safety UX for conflict review before destructive actions.

2. Windows Installer (NSIS → MSI via `msis`)
- Author `.msis` scripts and build MSIs with the `msis` tool (WiX 6 backend) at `C:\NGBT\MSIS\msis-3.x`.
- Payload (from `build-<arch>-release/jucyaudio_artefacts/Release/`): `JucyAudio.exe`, `glew.dll`,
  `projectM-4.dll`, `presets/` (~9,800 files, ~115 MB), `themes/`, and `licenses/`. Exclude the `.pdb`.
- Replicate NSIS behaviour: desktop + Start Menu shortcuts, "open with jucyaudio" shell extension,
  Add/Remove Programs entry, license page.
- Handle the MSVC runtime via a `vcredist 2022` bundle prerequisite (do not hand-ship runtime DLLs).
- Build a universal bundle `.exe` wrapping the per-arch MSIs; validate x64, x86, and arm64.
- NOTE: the legacy NSIS script (`setup/setup-x64.nsi`) is stale — it predates projectM and ships only
  `JucyAudio.exe` + themes, omitting `glew.dll`, `projectM-4.dll`, and `presets/`. The MSI must not
  inherit that gap.

## 6. Execution Strategy (remaining MUST-HAVE scope)

## Phase A: Design Lock (1 week)

- Finalize acceptance criteria for each feature in writing.
- Freeze schema impacts and migration approach before implementation.
- Identify hard blockers early (dependency decisions, performance constraints, data safety constraints).

Exit criteria:
- Each feature has: scope boundary, non-goals, acceptance tests, rollback/fallback behavior.

## Phase B: Implementation

1. Dedupe System
- Implement read-only detection/reporting mode first.
- Add action modes second (mark/merge/remove) with explicit confirmation paths.

Exit criteria:
- All MUST-HAVE code paths implemented behind stable UI/UX flows.
- All destructive operations protected by preview/confirmation.

## Phase C: Stabilization (1-2 weeks)

- Regression pass on library scan, playback, export, plugins, and settings persistence.
- Large-library smoke validation (performance and memory sanity).
- Packaging rehearsal for Windows and macOS artifacts.

Exit criteria:
- No release-blocking defects open.
- Release notes draft complete and reviewed.

## 7. Feature Acceptance Criteria (minimum)

1. Dedupe System
- Duplicate candidates are reproducible across runs.
- False-positive rate is acceptable on test corpus.
- No file-destructive action without explicit user confirmation.

## 8. Risks And Mitigations

- Risk: User trust risk from dedupe destructive paths.
- Mitigation: default read-only previews, explicit confirmations, detailed logs.

- Risk: Documentation drift from UI.
- Mitigation: manual doc review pass during stabilization, before tag.

- Risk: Large-library regressions.
- Mitigation: run smoke checks on representative large DB before release cut.

## 9. Cross-Agent Revision Protocol

Use this sequence for consistent revisions:

1. Codex pass (this doc baseline).
2. Claude pass (architecture/risk/quality-bar edits).
3. Gemini pass (workflow/completeness/readability edits).
4. Final Codex merge pass to resolve conflicts and normalize wording.

Prompt template for each assistant:
- "Revise `docs/release-plan-2.0.md` for 2.0 ship readiness. Keep MUST-HAVE scope fixed. Tighten acceptance criteria, risk controls, and release gates. Avoid adding net-new features."

## 10. Cross-Agent Review Ledger

Use this section for iterative revisions from each assistant. Keep entries short and dated.

### Codex Review

- 2026-03-07: Added initial joint plan, release gates, and branch policy aligned to current `dev/2.0` workflow.

### Claude Review

- 2026-03-07: Moved Smart Automix (2.3) to completed — fully implemented and in active use. Fixed Section 4 list indentation.
- 2026-03-07: Deferred User Manual (2.1) and Library Organizer (3.3) to 2.x. Dedupe System is the sole remaining MUST-HAVE for 2.0.
- 2026-06-07: Descoped full Dedupe System to 2.1 — 2.0 is now feature-complete. Replaced "NSIS → MSIS"
  with concrete MSI plan (`msis` tool / WiX 6), documented the real install payload, and flagged that
  the legacy NSIS script ships a broken (projectM-less) payload.
- 2026-06-27: Consolidated the 2.0 line onto `main` and deleted `dev/2.0`. Forward-ported the 1.x
  accidental-reorder fix (the undo-deadlock fix was already present in 2.0 in another form). Shipped the
  x64 MSI build, and migrated theming to base16 (20 schemes + an orange brand default).

### Gemini Review

- Pending.

## 11. Decision Log

- 2026-03-07: Prioritized quick wins first (docs sync + branch workflow clarification) before deep feature strategy.
- 2026-03-07: Expanded joint release plan into phased execution strategy with explicit acceptance criteria and cross-agent revision protocol.
- 2026-03-07: Descoped User Manual and Library Organizer from 2.0 — Manual has low demand and high manual effort; Organizer design needs more time to mature. Dedupe is the only remaining blocker.
- 2026-03-07: Added Windows installer migration (NSIS → MSIS) as 2.0 MUST-HAVE.
- 2026-06-07: Descoped the full Dedupe System to 2.1; 2.0 keeps only the working-set metadata dedup.
  With that, all 2.0 MUST-HAVE features are DONE and 2.0 is feature-complete.
- 2026-06-07: Confirmed installer direction is MSI built with the in-house `msis` tool (WiX 6), not a
  hand-written WiX/MSIX migration. Per-arch MSIs + a universal bundle with a vcredist 2022 prerequisite.
- 2026-06-27: 2.0 ships x64-only and self-contained (app-local MSVC runtime), so no vcredist
  prerequisite and no bundle — a single `jucyaudio-<version>-x64.msi`.
- 2026-06-27: Merged `dev/2.0` into `main` (fast-forward via reset + force-push) and deleted `dev/2.0`;
  `main` is now the active branch. `v2.0.0` tag and `release/2.x` deferred until the release gates close.
