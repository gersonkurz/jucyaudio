# JucyAudio 2.2 Release Plan

Date: 2026-03-07 (branch policy updated 2026-06-27; renumbered to 2.2 on 2026-09-18)
Branch baseline: `main` (the 2.0 line was consolidated from `dev/2.0` onto `main` on 2026-06-27; `dev/2.0` deleted)
Scope: align and ship the first 2.x release with clear cross-agent review.

**This was `docs/release-plan-2.0.md`.** One unreleased line, renumbered twice and never tagged: the
tree built `2.0.0`, then `2.1.0` from `0671b4d` (2026-08-08), and `2.2.0` from 2026-09-18. Nothing has
shipped as any of them - the newest tag is `v1.1.0` - so this is still the same plan for the same
release, and Sections 10 and 11 keep the history under the numbers it was decided under.

## 1. Objective

Ship 2.2 once all MUST-HAVE features are complete and a minimum quality gate is met.

## 2. Remaining Work

2.2 is **feature-complete** and **packaged**. The x64 MSI shipped on 2026-06-27 and the legacy NSIS
scripts were deleted on 2026-06-28 (`1ef42a4`); Section 5 records what was built.

What remains is the release gates in Section 3:

- Bug fixes tracked in [GitHub issues](https://github.com/gersonkurz/jucyaudio/issues); the ones that
  must be fixed before tagging carry the `P1` label.
- A macOS build and self-test of the current tree. The last recorded one is issue #31; every change
  since has been verified on Windows only.
- Manual GUI QA (Section 3), and the tag decision in Section 4.

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
- 2.2 ship cut (remaining):
  1. Stabilization window on `main`.
  2. Tag `v2.2.0`.
  3. Create `release/2.x` for 2.2.x maintenance.

**Settled on 2026-09-18: the release is 2.2.0.** The build version had run ahead of the release
narrative - the tree built `2.1.0` from `0671b4d` while this plan still said "tag `v2.0.0`" and
`CHANGELOG.md` headed `[2.0.0] - Unreleased`. The four places that carry a version now agree:
`CMakeLists.txt` (the source of truth; everything else in the build derives from `${PROJECT_VERSION}`),
`setup/jucyaudio-x64.msis` `PRODUCT_VERSION`, `AGENTS.md`, and the `CHANGELOG.md` heading, which is
now `[2.2.0] - Unreleased` because nothing was ever released as 2.0.0 or 2.1.0.

## 5. Workstreams

1. Dedupe System — **descoped** (2026-06-07). 2.2 ships only the in-working-set metadata dedup.
   The design for the rest is `docs/features/dedupe.md`; it is not in this release and has no gate here.

2. Windows Installer (NSIS → MSI via `msis`) — **done** (2026-06-27).

- Built from `setup/jucyaudio-x64.msis` with the `msis` tool (WiX 6/7 backend) at
  `C:\NGBT\MSIS\msis-3.x`, by `just package-x64`: configure, build, `cmake --install`, then
  `msis /BUILD /STANDALONE`.
- Payload: everything `cmake --install` stages into `install-x64-release/bin/` — `JucyAudio.exe`,
  `projectM-4.dll`, `glew.dll`, the app-local MSVC runtime, `presets/` (~9,800 files, ~115 MB),
  `themes/` and `licenses/`. The install step stages a clean payload rather than the build tree, so
  the `.pdb` never enters it.
- Replicates the NSIS behaviour that mattered: desktop and Start Menu shortcuts, the "open with
  jucyaudio" shell entry (`setup/shell-integration.reg`), and an Add/Remove Programs entry.
- No `vcredist` prerequisite: the runtime ships app-local, so the payload is self-contained and a
  launch condition would only risk falsely blocking the install. This reverses the original plan,
  per the 2026-06-27 decision below.
- x64 only, and therefore no per-arch bundle: one `jucyaudio-<version>-x64.msi`. The x86 and arm64
  validation the original plan called for is not in this release. `CMakePresets.json` has x64 and x86
  presets; there is no Windows-arm64 preset.
- The legacy NSIS scripts were deleted on 2026-06-28 (`1ef42a4`). They had predated projectM and
  shipped only `JucyAudio.exe` + themes; the MSI payload above is what replaced them.

## 6. Execution Strategy (remaining scope)

Phases A and B are complete. They covered the MUST-HAVE feature work, whose only open item - the full
Dedupe System - was descoped to 2.1 on 2026-06-07, taking its implementation plan with it. Its
acceptance criteria are kept in Section 7, against 2.1. Only Phase C is left.

## Phase C: Stabilization (1-2 weeks)

- Regression pass on library scan, playback, export, plugins, and settings persistence.
- Large-library smoke validation (performance and memory sanity).
- Packaging rehearsal for Windows and macOS artifacts.

Exit criteria:
- No release-blocking defects open.
- Release notes draft complete and reviewed.

## 7. Feature Acceptance Criteria (minimum)

None outstanding for this release. The only entry was the Dedupe System, descoped on 2026-06-07 to the
milestone the roadmap still labels 2.1. Its criteria are kept below rather than deleted -
`docs/features/dedupe.md` is a design document and does not restate them - and they are a gate for
that milestone, not for this release.

1. Dedupe System — **deferred to 2.1**
- Duplicate candidates are reproducible across runs.
- False-positive rate is acceptable on test corpus.
- No file-destructive action without explicit user confirmation.

## 8. Risks And Mitigations

- Risk: Documentation drift from UI.
- Mitigation: manual doc review pass during stabilization, before tag.

- Risk: Large-library regressions.
- Mitigation: run smoke checks on representative large DB before release cut.

- Risk: macOS drift. Fixes are being written and verified on Windows; the macOS build and self-test
  are run separately and less often, so a regression there is found late.
- Mitigation: build and self-test macOS before the cut, and treat it as a gate in Section 3 rather
  than as a check done when convenient.

## 9. Cross-Agent Revision Protocol

Use this sequence for consistent revisions:

1. Codex pass (this doc baseline).
2. Claude pass (architecture/risk/quality-bar edits).
3. Gemini pass (workflow/completeness/readability edits).
4. Final Codex merge pass to resolve conflicts and normalize wording.

Prompt template for each assistant:
- "Revise `docs/release-plan-2.2.md` for ship readiness. Keep MUST-HAVE scope fixed. Tighten acceptance criteria, risk controls, and release gates. Avoid adding net-new features."

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
- 2026-09-18: Consistency pass, no new decisions. Sections 2, 5, 6 and 7 still described the Dedupe
  System as remaining 2.0 scope and the installer as unbuilt, with a `vcredist` prerequisite, a
  per-arch bundle and x86/arm64 validation - all three reversed by the 2026-06-27 decisions recorded
  in Section 11, which the body was never updated to match. Section 5 also still warned about NSIS
  scripts deleted on 2026-06-28 (`1ef42a4`). Added the macOS drift risk to Section 8, and recorded in
  Section 4 that the tree has built `2.1.0` since `0671b4d` while this plan and `CHANGELOG.md` still
  say 2.0 - a decision for the human, not taken here. Corrected in review: `docs/ROADMAP.md` still
  named the MSI as remaining work, and the first draft of this pass deleted the Dedupe acceptance
  criteria while claiming they lived in `docs/features/dedupe.md`, which they do not. They are kept
  in Section 7, marked deferred.
- 2026-09-18: Renumbered to 2.2. The release is `v2.2.0`, per the decision in Section 11; this file
  was `docs/release-plan-2.0.md` and the five references to that name were updated
  (`CLAUDE.md` Loop parameters, `docs/ROADMAP.md` x3, and the prompt template in Section 9). Sections
  10 and 11 keep their original numbers, because they record what was decided when, not what the
  release is called now.

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
- 2026-09-18: The first 2.x release is **2.2.0**, not 2.0.0. The build version had already moved twice
  without the release narrative following (`0671b4d`, 2026-08-08, deliberately left this plan and the
  CHANGELOG alone); the narrative now follows the build. Nothing was ever tagged as 2.0.0 or 2.1.0, so
  the accumulated unreleased work ships as 2.2.0 and `CHANGELOG.md` heads that section accordingly.
  Note for the feature table in `docs/ROADMAP.md`: its Target column still uses 2.0/2.1/2.2 as planning
  milestones from the original plan, which the shipping version has now overtaken - "Target 2.2" there
  means the second milestone after 2.0, not this release. Renumbering those is a product decision and
  was not taken here.
