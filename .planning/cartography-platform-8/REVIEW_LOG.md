# REVIEW LOG — cartography-platform-8

## Self-review during development (running log)

- Locator connector geometry: the first implementation computed the inset
  extent center with an operator-precedence bug (`xmin + xmax/2` instead of
  `(xmin+xmax)/2`). The exact-geometry test caught it (page position was off
  by exactly the mis-centering delta); fixed and covered.
- LayoutService polyline placement: `QgsLayoutNodesItem::setNodes` maps the
  polygon bounding box through the item's CURRENT position (mapToScene), so
  passing raw scene nodes after the default `attemptSetSceneRect(10,10,30,30)`
  contaminates the geometry. Final protocol: park the item at the polygon's
  bounding box and hand setNodes the LOCAL nodes — verified numerically.
- Test ids: `appendMapSpecItem` assigns ids (`source_note-1`, not a pre-set
  `note-1`); constraint references now use the RETURNED id. Two tests were
  silently referencing phantom ids before this was caught.
- kMapSpecCurrentVersion bump: the P7 suite pinned the constant to 4; the
  pin was updated to 5 together with the migration doc (intentional,
  documented).

## Final adversarial review (2 read-only subagents, both dispatched)

Reviewer 1 — architecture/correctness (solver, typography, mapspec v5,
compiler): **FAIL** — 0 P0, 1 P1, 3 P2, 8 P3.
Reviewer 2 — style/QA/harness/test-quality/docs: **FAIL** — 0 P0, 2 P1,
2 P2, 7 P3.

### Dispositions (all P1/P2 fixed; P3 fixed or justified)

- **R1-P1 Connector Y projection mirrored for north-up extents — FIXED**
  (north-up rendering puts ymin at the frame bottom: anchor page-y now
  `(ymax - mapY)/targetH`; new asymmetric known-answer test pins an
  off-center inset — the original symmetric test could not see the bug).
- **R2-P1 Invalid `nodata.color` silently rendered transparent while the
  report claimed "shaded" — FIXED** three ways: validation now rejects
  unparseable color strings; the live path falls back to the documented
  black default and appends an explicit problem; the knowledge path
  (`buildRasterRenderer`) applies the same fallback.
- **R2-P1 Docs overclaimed the page_overflow evidence surface (decisions
  ledger and unsat cores) — FIXED** both sides: the Failed branches now
  record the refusal as a decision (ledger carries the evidence), and the
  three docs now state honestly that permanent failures CANNOT appear in
  unsat cores (no subset removal changes the page bound).
- **R1-P2 Soft constraints never recorded failReason — FIXED** (applySofts
  preserves it; finalize's soft branch reports it verbatim).
- **R1-P2 Derived items ignored the parent's declared page — FIXED**
  (`placeWithParentPage` mirrors the v2 post-pass attemptMove for the
  connector, locator caption, nodata swatch and nodata label; the caption
  instance predates this diff but is fixed by the same helper).
- **R1-P2 Connector compile failure silently dropped — FIXED** (a declared
  relationship graphic that cannot materialize now fails the compile, like
  an inset that cannot compile).
- **R2-P2 / R1-P3-7 `legends[].nodata` unvalidated + rule predicate
  divergence — FIXED** (rule keeps firing on non-object declarations;
  `validateMapSpec` shape-checks `nodata {label?, color?}`; new test).
- **R2-P2 Compose test's registerBuiltinTools fallback could mask wiring
  regressions — JUSTIFIED with reduced risk**: the call is now unconditional
  documented test setup (the production harness path — confirmMapOutput via
  the registry — is exercised green by test_harness_evals/test_agent_tools_3,
  which find the tools through normal registration).
- **R2-P3-1 band extraction duplicated — FIXED** (`bandOf` helper shared by
  applyRasterBlock and applyRasterNodata).
- **R2-P3-2 misleading idempotency comment — FIXED** (rationale corrected:
  idempotency comes from renderer replacement; style-spec-reference sentence
  aligned).
- **R1-P3-2 header implied U+2026 is halfwidth-compressed — FIXED** (doc now
  states the ellipsis is not in the fullwidth class and never compressed).
- **R1-P3-3 textFitReportToJson missing breakPolicyApplied — FIXED.**
- **R1-P3-5 degenerate legend rects could push the swatch above the legend —
  FIXED** (y clamped into the rect).
- **R2-P3-3 multiband nodata is per-declared-band — DOCUMENTED** in
  style-spec-reference (QGIS applies nodata per band; the declared band only).
- **R1-P3-1 page_overflow is "permanent" at current geometry only (a later
  fit_content shrink would have re-enabled it under old semantics);
  already-off-page companions convert to violations — ACCEPTED with
  rationale**: both only trigger when page overflow exists, which is exactly
  the intended contract change; the pre-existing alternative (silent
  off-page geometry) is the defect this feature removes. Also fixed for
  soft constraints by the P2 remediation.
- **R1-P3-4 page-awareness covers push-down directions only — ACCEPTED**:
  `above/left_of/right_of/stack` pins stay within the leader's span, and
  left/right overflow remains MAP_OFF_PAGE territory; noted here.
- **R1-P3-6 mapspec.cpp includes cartography/typography.h — ACCEPTED**:
  single sicnu_agent target, no cycle; avoids a second break-policy
  vocabulary drifting apart from the engine's.
- **R1-P3-8 anchor inside the inset footprint draws a doubled-back leader —
  FIXED** (classic locator practice: the connector is suppressed when the
  projected anchor falls inside the inset rect; deterministic).
- **R2-P3-6 StyleRegistry global mutation in the preflight test not restored
  — ACCEPTED** (pre-existing suite-wide pattern; every registry consumer
  sets its directory explicitly before use).
- **R2-P3-7 elide change untested — ACCEPTED**: pixel-path behavior guarded
  by the `[visual][determinism]` render hash; both sites are width-guarded
  (> 12 px) so elide widths stay positive (verified by reviewer arithmetic).

## Post-remediation validation (Linux, QT_QPA_PLATFORM=offscreen)

- `[platform8]`: 17 cases — ALL PASSED.
- Full cartography sweep (MapSpec/Cartography/P5/P6/P7/visual/golden/
  symbology/knowledge/drift): ALL PASSED except pre-existing unrelated
  `_NOT_BUILT` targets (see FINAL_REPORT).
- Harness regressions (plan_tools touched): test_harness_catalog,
  test_harness_evals, test_agent_tools_3 — ALL PASSED.
