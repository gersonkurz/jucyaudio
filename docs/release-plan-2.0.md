# JucyAudio 2.0 Joint Release Plan

Date: 2026-03-07
Branch baseline: `dev/2.0`
Scope: align and ship 2.0 with clear cross-agent review.

## 1. Objective

Ship 2.0 once all MUST-HAVE features are complete and a minimum quality gate is met.

## 2. Remaining MUST-HAVE Features

- 2.2 Dedupe System
- Windows installer migration from NSIS to MSIS (`setup/*.nsi`)

**Completed MUST-HAVE Features:**

- 1.1 ProjectM Integration
- 1.2 VST3 Support
- 2.3 Smart Automix

**Deferred to 2.x:**

- 2.1 User Manual — low demand, high manual effort (screenshots, etc.)
- 3.3 Library Organizer — design not mature enough for 2.0 scope

Reference: `docs/ROADMAP.md`

## 3. Release Gates (minimum)

- Feature completeness: all MUST-HAVE items marked done.
- Build health: successful non-GUI builds on Windows and macOS paths.
- Packaging health: release artifacts generated for target architectures.
- Manual QA: core GUI workflows smoke-tested by a human.
- Documentation consistency: roadmap/changelog/features reflect shipped state.

## 4. Branch And Merge Policy

- Active implementation continues on `dev/2.0`.
- `main` remains release-only until 2.0 cut.
- 1.x hotfix flow is optional and only activated if real issues are reported.
- 2.0 ship cut:
  1. Stabilization window on `dev/2.0`.
  2. Merge `dev/2.0` to `main`.
  3. Tag `v2.0.0`.
  4. Create `release/2.x` for 2.0.x maintenance.

## 5. Workstreams

1. Dedupe System
- Finalize matching strategy (hash + acoustic constraints where needed).
- Add safety UX for conflict review before destructive actions.

2. Windows Installer (NSIS → MSIS)
- Migrate `setup/*.nsi` scripts to MSIS equivalents.
- Validate installer for x64, x86, and arm64 targets.

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

### Gemini Review

- Pending.

## 11. Decision Log

- 2026-03-07: Prioritized quick wins first (docs sync + branch workflow clarification) before deep feature strategy.
- 2026-03-07: Expanded joint release plan into phased execution strategy with explicit acceptance criteria and cross-agent revision protocol.
- 2026-03-07: Descoped User Manual and Library Organizer from 2.0 — Manual has low demand and high manual effort; Organizer design needs more time to mature. Dedupe is the only remaining blocker.
- 2026-03-07: Added Windows installer migration (NSIS → MSIS) as 2.0 MUST-HAVE.
