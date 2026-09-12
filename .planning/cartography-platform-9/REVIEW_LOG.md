# REVIEW LOG — self-review + adversarial review findings

## Self-review findings (pre-subagent)

### P2-1 — duplicate `failed` ledger entries after unsat-core search (M0, FIXED)
`sweepHardOnce()` records a `failed` decision unconditionally (no
`decided`-style dedupe, unlike reports which key-dedupe). When
`searchCores()` re-runs the real sweep to restore post-fixpoint state,
every permanently-failed constraint records a SECOND ledger entry.
Fix: dedupe `failed` decisions per runtime (mirrors `rejectedDecided`).
Evidence: test M0 ledger-dedupe.

### P2-2 — searchCores restore sweep parked mid-oscillation geometry (M0 corpus-found, FIXED)
The unsat-core search re-runs the real sweep from the pre-solve snapshot to
restore post-fixpoint state, but never re-applied the #864 rollback: a
document that exhausted the pass budget ended up at oscillated geometry
after the core search even though relaxHard had rolled it back. Fix: the
restore sweep mirrors relaxHard exactly — budget exhaustion re-rolls back.
Evidence: test_platform9 M0 rollback known-answer (old code fails it).

### P3-1 — page_break re-solve was not idempotent (M2, FIXED)
Re-solving a resolved document applied page_break a second time (page+1
again). Fix: applied breaks stamp `page_break_applied_by` on the item; the
solver treats a matching stamp as satisfied, and validation skips the
target-page check for stamped items.

### P3-2 — MAP_CHART_OVER_MAP repair could not clear real template layouts (M6, FIXED)
The shipped chart-overlay templates (e.g. sar-backscatter-a3l) leave NO free
slot for the chart — the overlay is the design. Fix: the repair prefers
relocation (8×8 bounded grid sweep) and, only when no free slot exists,
stamps `overlay_on` — converting silent coverage into declared intent.

## Test evidence (pre-subagent-review state)

- Full test_mapspec suite (worktree, Release): **206 cases / 2492
  assertions — ALL PASSED** (includes the 43 new platform9 cases).
- Catalog index regenerated (SICNU_CARTOGRAPHY_REGENERATE_INDEX) after the
  new component; gallery.md updated; drift tests green.
- Harness-adjacent regressions: run after the review fixes (see
  FINAL_REPORT).

## Adversarial review (2 read-only subagents, full-diff scope) — findings & remediation

Reviewer A: architecture / correctness / concurrency / scientific validity / security.
Reviewer B: tests / performance / portability / docs-drift / cmake.

| # | Sev | Area | Finding | Disposition |
|---|-----|------|---------|-------------|
| A-1 | P1 | correctness | #864 rollback restored rect_mm only — a page_break applied before an oscillation kept its `page` write and `page_break_applied_by` stamp (wrong-page layout; contradiction absorbed on re-solve) | **FIXED** — RectSnapshot covers page + stamp with null-means-absent restore; regression test `Review: #864 rollback also restores…` |
| A-2 | P1 | correctness | overlay_on deadlock fallback nested a pre-declared array (`[["map-a"], "map-b"]`) → repaired doc failed validation | **FIXED** — per-element merge with dedupe; regression test `Review: the overlay deadlock fallback merges per-element…` |
| A-3 | P2 | correctness | two page_breaks on one item defeat the per-item stamp across compose round trips | **FIXED** — validateMapSpec rejects a second page_break targeting the same item; regression test |
| A-4 | P2 | correctness | ExplainTool cid synthesis diverged from buildRuntimes for legacy (non-solver) constraint kinds | **FIXED** — the mirror now skips non-solver kinds exactly like buildRuntimes |
| A-5 | P2 | scientific | furniture clones existed only in the compile copy → pages[].furniture chart clones evaded every spec-level rule incl. MAP_CHART_OVER_MAP | **FIXED** — expansion extracted to `mapspec::expandMasterFurniture`; preflight expands its working copy before rule evaluation |
| A-6 | P2 | security | export file_name accepted absolute/`..` paths escaping the declared directory | **FIXED** — bare-name validation (no separators, no leading ..) in validateMapExportRequest |
| A-7 | P2 | security | remove-then-rename swap not atomic vs the header's claim | **FIXED** — rename-first (POSIX atomic replace), remove+rename fallback; wording scoped |
| A-8 | P2 | tests | raster scale_ranges apply path untested | **FIXED** — `M5: raster scale_ranges reach the layer's scale visibility` |
| A-9 | P2 | tests | overlay_on had zero direct tests | **FIXED** — declared-silence + dangling-ref tests, plus the deadlock-fallback merge test |
| B-1 | P1 | tests | M10 tools-surface test depended on ambient registerBuiltinTools call order (failed in isolation) | **FIXED** — the test registers builtins itself |
| B-2 | P2 | docs | gallery.md said 57 components vs regenerated 58-entry index | **FIXED** |
| B-3 | P2 | docs-drift | valid bivariate declaration rendered single-axis with NO advisory — silent-capability class | **FIXED** — applyStyleSpecToLayer reports the honesty problem; test pins it |
| B-4 | P2 | docs-drift | 9.0 docs/CHANGELOG sections existed only as working-tree changes | **FIXED** — committed (review-remediation commit) |
| A-10 | P3 | correctness | unreachable page_break equality short-circuit | **FIXED** (removed; stamp is the idempotency mechanism) |
| A-11 | P3 | correctness | trace.moves reported the cap, not the count, when truncated | **FIXED** — separate full counter |
| A-12 | P3 | scientific | text_ref wrap ignored font.break_policy ("same model" claim inexact) | **FIXED** — solver passes the source break_policy |
| B-5 | P3 | tests | still_reported ledger outcome never asserted | **FIXED** — page-scoped chart test asserts still_reported |
| B-6 | P3 | tests | validation edges untested (expression type, furniture budget, page-0 continuation) | **FIXED** — `M9: validation edges…` |
| B-7 | P3 | portability | export.cpp include hygiene (algorithm transitive, unused QRegularExpression) | **FIXED** |
| A-13 | P3 | docs | rule catalog text omitted the overlay_on fallback; reference wording on unsat cores loose; scoped-resolve claim drift; page_break stamp semantics clarified | **FIXED** (all wording) |
| B-8 | P3 | tests | M6 locale test pins Qt not product | **ACCEPTED** — the product paths are the cited explicit-format QString::number call sites in chart_registry; asserting them would require PNG OCR. Comment documents the intent. |
| B-9 | P3 | tests | M4 disk-catalog test lacks RAII restore on failure paths | **ACCEPTED** — Catch2 section reordering; restore is last-statement; noted for follow-up |
| A-14 | P3 | architecture | portability one-liners outside ownership lane (geospatial/experiment) | **ACCEPTED** — required to build on this host; identical fixes carried by sibling -9 PRs; documented for dedupe |
| A-15 | P3 | correctness | non-string page_break items[0] passed validation unflagged | **FIXED** — explicit type check |

All P0/P1/P2 findings are fixed with regression tests; P3s are fixed or
accepted with rationale above. Post-remediation verification: full
test_mapspec **214 cases / 2534 assertions ALL PASSED**; test_harness_catalog
93 PASS; test_agent_tools_3 126 PASS.
