# FINAL REPORT — Cartography Platform 9.0

Branch `feat/cartography-platform-9` (base `origin/master` @ `f316dfdbb4`,
PR base for the 9.0 series). Worktree
`/home/kevin/projects/rs-studio/exp-rs-cartography-platform-9`.
Environment: Linux, system QGIS + Qt 6, Ninja/Release, gcc; builds ≤ -j4,
tests -j1. **Online CI/CD was not required and was not waited on** — all
evidence below is local and reproducible.

## Baseline & overlap audit

0 open cartography issues; the five goal-prompt leads (#864/#865/#866/#867/
#877) re-verified CLOSED on the new master with per-fix evidence
(ISSUE_TRIAGE.md). Five sibling `-9` PRs audited for overlap: none touches
owned files with divergent content (OVERLAP_MAP.md).

## Shipped capability deltas (verified gaps → commits)

- **M0** — regression corpus pinning the four closed solver/condition fixes
  as old-fails/new-passes tests; ±Inf/mixed-kind ordering totality pinned;
  ledger dedupe (P2-1) and the searchCores rollback mirror (P2-2, found by
  the new corpus) fixed.
- **M1** — solver 9.0: `oscillations` (the fighters still writing at budget
  exhaustion), bounded per-pass `trace`, `fit_content.text_ref` text-driven
  sizing (deterministic typography model, no Qt), scoped re-solve
  (`resolveCompositionScoped`, anchor authority preserved).
- **M2** — multi-page: `page_break` (validated, solver-applied once with
  provenance stamp), `pages[].furniture` master furniture clones, atlas
  `expression` → QGIS-native `[% … %]` markup (parse-checked), continuation
  labels with resolved display page numbers.
- **M3** — `table--accuracy-matrix` component; catalog audit: no category
  gaps; decorative accessibility blocks rejected (unread fields = fake
  compliance; the real mechanisms stay MAP_TINY_FONT + checkStyleContrast).
- **M4** — `diffTemplates` bounded semantic diff + `cartography:
  diff_templates`; `template_provenance` stamping echoed by compose;
  multi-parent extends verified already-shipped (7.0) and pinned by test.
- **M5** — raster `scale_ranges`; `bivariate` declaration-gated by an
  explicit semantic contract; change-map assets verified already-shipped
  (styles + templates existed; E2E covered by the drift/visual suites).
- **M6** — `MAP_CHART_OVER_MAP` + converging repair (grid relocation,
  `overlay_on` declaration fallback); `MAP_DUAL_AXIS_UNSUPPORTED` honesty
  rule; locale-independent formatting pinned.
- **M7** — kinsoku line-end guard verified already-shipped and pinned;
  ellipsis truncation evidence pinned; CJK scene already in the visual
  harness.
- **M8** — `repairMapSpecWithLedger` (applied / still_reported per finding);
  repair tool surfaces the per-pass ledger; map-frame `crs` satisfies the
  report CRS obligation.
- **M9** — `cartography:export`: atomic (temp → verify → sha256 → rename),
  png page selection, honest pdf/svg all-pages refusal, font substitution
  diagnostics, export-scale render determinism test.
- **M10** — `cartography:explain` bounded per-item evidence tool; tool
  surface test pins the closed loop compose→preflight→repair→export→confirm.

## Test evidence (all local, reproducible)

- Full `test_mapspec` suite (Release, this worktree): **206 cases / 2492
  assertions — ALL PASSED**, including the new `test_platform9.cpp` corpus
  (43 cases across M0–M10).
- Harness-adjacent regressions (tool surfaces touched by M10):
  `test_harness_catalog` **93 assertions PASS**,
  `test_agent_tools_3` **126 assertions PASS**.
- PNG determinism and opt-in golden cases re-run green inside the suite;
  goldens stay out-of-tree by the documented design
  (docs/cartography/visual-regression.md).
- Catalog drift: index regenerated and committed; gallery.md updated;
  drift tests green.
- Not run (honest): Windows/macOS builds and platform CI — online CI/CD was
  explicitly not a completion condition; no network-dependent tests exist.

## Adversarial review & remediation

Two read-only subagents (A: architecture/correctness/concurrency/scientific/
security; B: tests/performance/portability/docs-drift/cmake) reviewed the
full branch diff. Findings and remediation recorded in REVIEW_LOG.md;
P0/P1 all fixed and the affected suites re-run green (FINAL numbers above
reflect the post-remediation state).

## Known limitations (honest)

- page-scoped (`page > 0` declared) charts are skipped by the
  MAP_CHART_OVER_MAP repair (page-0 page geometry only); the finding stays
  reported for the agent.
- pdf/svg page selection is refused rather than emulated — this QGIS build's
  PdfExportSettings/SvgExportSettings have no page selection.
- `fit_content.text_ref` measures with the deterministic model, not the
  renderer's font metrics; visual deltas are caught by the render-determinism
  and golden paths, not by the solver.
- Continuation labels resolve page numbers at compile; atlas-rendered page
  shifts (atlas feature count) are a QGIS render-time concern and out of
  scope for the declarative document.
- Export SVG is offered only because this build's exporter supports it;
  capability probing is per-build and reported honestly.

## Performance / resource notes

All new hot-path costs are bounded and small: trace ≤ 24 entries × ≤ 32
cids; oscillation derivation O(passes); text_ref = one bounded fit
evaluation; scoped re-solve ≤ full solve; grid repair ≤ 64 candidates; the
repair ledger adds one preflight per repair pass. No new caches, queues, or
threads. See PERFORMANCE.md.
