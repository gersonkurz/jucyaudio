# JucyAudio 2.0 Joint Release Plan

Date: 2026-03-07
Branch baseline: `dev/2.0`
Scope: align and ship 2.0 with clear cross-agent review.

## 1. Objective

Ship 2.0 once all MUST-HAVE features are complete and a minimum quality gate is met.

## 2. Remaining MUST-HAVE Features

- 2.1 User Manual
- 2.2 Dedupe System
- 3.3 Library Organizer

**Completed MUST-HAVE Features:**

- 1.1 ProjectM Integration
- 1.2 VST3 Support
- 2.3 Smart Automix

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

1. User Manual
- Define structure and publishing path.
- Ensure coverage for library, mix editor, export, plugins, visualizer, settings.

2. Dedupe System
- Finalize matching strategy (hash + acoustic constraints where needed).
- Add safety UX for conflict review before destructive actions.

3. Library Organizer
- Define move/rename safety model, dry-run, rollback/logging strategy.
- Validate behavior against large-library edge cases.

## 6. Execution Strategy (remaining MUST-HAVE scope)

## Phase A: Design Lock (1 week)

- Finalize acceptance criteria for each feature in writing.
- Freeze schema impacts and migration approach before implementation.
- Identify hard blockers early (dependency decisions, performance constraints, data safety constraints).

Exit criteria:
- Each feature has: scope boundary, non-goals, acceptance tests, rollback/fallback behavior.

## Phase B: Implementation (2-4 weeks total, parallelized)

1. User Manual
- Deliver docs skeleton and top-level navigation first.
- Fill critical operator workflows before edge-case coverage.

2. Dedupe System
- Implement read-only detection/reporting mode first.
- Add action modes second (mark/merge/remove) with explicit confirmation paths.

3. Library Organizer
- Ship dry-run and preview output first.
- Ship write mode only after conflict handling and rollback logs are verified.

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

1. User Manual
- New user can complete: library setup, mix creation, export, plugin scan/use.
- Documentation matches current UI labels and behavior.

2. Dedupe System
- Duplicate candidates are reproducible across runs.
- False-positive rate is acceptable on test corpus.
- No file-destructive action without explicit user confirmation.

3. Library Organizer
- Dry-run output is accurate and complete.
- Collision handling is deterministic.
- Abort/recovery path is documented and tested.

## 8. Risks And Mitigations

- Risk: User trust risk from dedupe/organizer destructive paths.
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

- 2026-03-07: Moved Smart Automix (2.3) to completed — fully implemented and in active use. Reduced remaining MUST-HAVEs to 3: User Manual, Dedupe, Library Organizer. Fixed Section 4 list indentation. Removed stale Smart Automix workstream/acceptance/risk entries.

### Gemini Review

- Pending.

## 11. Decision Log

- 2026-03-07: Prioritized quick wins first (docs sync + branch workflow clarification) before deep feature strategy.
- 2026-03-07: Expanded joint release plan into phased execution strategy with explicit acceptance criteria and cross-agent revision protocol.
