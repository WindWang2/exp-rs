# GOAL — Intelligent Cartography, MapSpec & Template Platform 8.0

Advance the MapSpec/template/style/component system into a mature
declarative cartographic compiler and QA platform: publication, operational,
report, atlas, and screen maps composed through QGIS with explainable
constraints, semantic styling, componentized furniture, deterministic
validation, and Harness-ready final-map confirmation.

Branch `feat/cartography-platform-8` from origin/master @ `2d4f0daedd`.
Local verification only; online CI/CD not required and not waited on.
Subagent budget: ≤2 (adversarial review only).

## Milestones

- **M0 Baseline** — planning docs; worktree build green; cartography suite
  green on Linux (52/52 baseline ctest evidence).
- **M1 NoData wiring (A)** — `style:apply` pushes `raster.nodata` into the
  QGIS renderer: provider user-nodata ranges (+transparent) and renderer
  nodata shading color; report strings; applied to `buildRasterRenderer`
  headless path too; tests; limitations.md updated.
- **M2 Locator connectors (A)** — `inset_maps[].locator.connector` compiles
  to a QGIS-native polyline (`QgsLayoutItemShape` via LayoutService) from
  the inset frame to the referenced frame's projected extent anchor;
  deterministic geometry; validation; tests; docs.
- **M3 MapSpec v5 output declarations (B)** — additive, versioned: envelope
  `output {formats, dpi, dir}` with closed format vocabulary and bounded
  dpi; deeper per-item `binding` shape validation; `upgradeMapSpec` stamps
  v5; v≤5 all validate (strict superset); migration doc.
- **M4 Page-aware solver evidence (C)** — keep_with/avoid_overlap/inside
  respect the follower's page bottom: honest `unsatisfied` evidence
  (page overflow) instead of silently off-page geometry; bounded and
  deterministic; report carries the evidence; tests.
- **M5 Typography 2.0 (D)** — declared `break_policy` hanging-punctuation
  handling (push/pull/halfwidth; default preserves 7.0 output), declared
  `line_height` override; known-answer tests.
- **M6 NoData legend QA (I)** — `MAP_NODATA_LEGEND` rule (legend referencing
  a style with declared nodata must carry a nodata mention) + deterministic
  converging repair (stamp `legend.nodata` from the style); compiler renders
  the nodata entry as a QGIS-backed swatch composite; catalog + docs.
- **M7 Compose identity + chart label budget (J/H)** — `cartography:compose`
  report carries `structural_digest` + template/component provenance;
  `confirmMapOutput` includes the digest (what was composed);
  `chart.style.max_label_chars` deterministic truncation with ellipsis in
  the QPainter path.
- **M8 Golden evidence + docs (A/docs)** — opt-in golden PNGs generated and
  verified on Linux; visual-regression.md records the desktop-capable
  evidence path; docs drift fixed (mapspec-reference "current" header,
  envelope example); preflight-rules.md catalog sync.
- **M9 Adversarial review** — ≤2 read-only subagents over the full diff;
  P0/P1 fixed, P2 fixed or justified, P3 fixed or justified; REVIEW_LOG.
- **M10 Integration** — full targeted suite, diff inspection, FINAL_REPORT,
  push, PR.

## Explicit non-goals (refused with rationale)

- No second rendering path: every pixel still comes from QGIS (charts/
  colorbar/nodata-swatch composites are sanctioned QPainter→picture
  furniture in this codebase since 4.0).
- No unbounded optimization in the solver; no schema break: v5 is a strict
  superset of v4; v≤4 documents compile byte-identically.
- No per-feature atlas symbology (QGIS-native authority, unchanged).
- No template-count growth for its own sake (Track F: consolidation audit
  recorded in CAPABILITY_MATRIX; the 7.0 facet/variant machinery already
  rationalized the catalog — no clones with conflicting semantics found in
  the audit sample).
