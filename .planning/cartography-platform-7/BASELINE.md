# BASELINE — audit of master @ c731e3e7 (2026-09-10)

## Repository / environment state

- Local `master` was 1 commit behind `origin/master`; branch created from
  **origin/master @ c731e3e7** (fix(help) GCC QString ambiguity).
- **0 open PRs, 0 open issues.** PR #820 *feat(cartography): Knowledge, Template &
  Recipe Platform 6.0* merged 2026-09-09 — the direct predecessor of this task.
- Sibling in-flight tracks (checked for file overlap — **none** with this task's
  ownership):
  - `feat/cloud-geospatial-io-7` — active, touches `src/geospatial/**` +
    `tests/test_io_*` only (33 files, no cartography/mapspec overlap).
  - `feat/dataset-experiment-7` — planning only.
  - `feat/execution-plane-runtime-7`, `feat/pi-spatial-scientist-harness-7` — no commits.
- Older merged branch worktrees still present (cartography-knowledge-platform-6 etc.)
  — merged residue, not active development; their remote branches exist but are merged.
- Build environment (discovered by configuring this worktree): MSVC 2022 + Ninja,
  Qt 6.8.0 at `C:\deps\Qt\6.8.0\msvc2022_64`, vcpkg toolchain
  `C:\deps\vcpkg`, winflexbison at `C:\deps\winflexbison`, shared vcpkg installed dir
  reused from the platform-6 worktree (vcpkg.json identical, verified by diff).

## Code baseline (this task's ownership)

| Module | Files | LOC | Notes |
| --- | --- | --- | --- |
| `src/agent/mapspec/` | 3 | ~2,650 | MapSpec v3 document model, compiler⇄QgsPrintLayout bridge, bounded conditional-visibility expressions |
| `src/agent/cartography/` | 16 | ~10,100 | registries (component/template/token/style/solution/chart), composition solver, preflight/repair, style compiler, tools |
| `src/agent/layout_tools/` | 6 | ~2,300 | LayoutService — the programmatic QGIS layout seam |
| tests | test_mapspec bundle: 10 files | — | tags `[mapspec][cartography][platform5][platform6][drift][visual][determinism][golden]` |
| data catalog | components 52, templates 56, styles 17, tokens 2, solutions ~20 | — | `data/cartography/**`, `data/agent/solutions/**`, drift-enforced `index.json` |

## Architecture facts (authoritative seams)

- MapSpec v3: collections ×15, stable ids, semantic roles, anchors/min-max/z/page,
  slots, typed constraints (13 solver kinds), conditional visibility
  (`visible_if/content_if/page_if`), atlas surface.
- Composition solver (Platform 6.0): normalize → dependency graph + cycle detection →
  intrinsic sizes → constraint propagation → bounded relaxation (≤24 passes) →
  collision handling → quality/convergence report. Deterministic per input; anchor
  authority rule exists (anchor wins, constraint disabled with targeted report).
- Preflight/repair: rule catalog with code/severity/repairable/suggested_action;
  deterministic bounded repair pass; platform-independent text-width estimator
  (CJK 1 em, other 0.55 em, space 0.35 em).
- Style: StyleSpec → QGIS renderer primitives only; token: references resolved
  transitively (≤8 hops, cycle-safe); applicability block (value_domain/band_count/
  modalities/semantics) from 6.0.
- Templates: facets {tasks, medium, purpose} closed vocabularies + faceted search with
  match.reasons; `extends` resolution with cycle detection.
- Solutions: recipe + map template + report template + style refs, validated via
  pluggable RefResolvers.

## Confirmed gaps → Platform 7.0 work packages

From code reading + `docs/cartography/limitations.md` + the 6.0 REVIEW_LOG accepted
P3s (each entry is evidence-backed):

| # | Gap (evidence) | Package |
| --- | --- | --- |
| G1 | Solver has no hard/soft distinction, no priority, no weighted objective, no unsat-core/minimal-conflict explanation; over-determined multi-fixpoint systems are deterministic-per-input but order-sensitive (composition.h contract; 6.0 REVIEW_LOG) | A |
| G2 | `resolveTemplateChain` replaces child `facets`/`variants` wholesale — no merge policy; template inheritance is single-axis (6.0 REVIEW_LOG accepted P3; 56 templates still encode sensor variants as separate files) | B |
| G3 | Component catalog is flat-ish; missing uncertainty note, accuracy report, temporal chart component descriptors, locator/overview composite, grid component semantics; components lack a formal applicability surface | C |
| G4 | Text model is single-line estimator only; no wrap simulation, no per-glyph metrics, no truncation policy, no fitting/overflow diagnostics beyond widest-line (limitations.md "Deferred (Platform 5.0)") | D |
| G5 | Style semantics lack diverging/NoData/uncertainty/multi-band/SAR/DEM dedicated validation, class ontology mapping, accessible contrast checks (style_spec.h applicability covers only value_domain/band_count/modality/semantics) | E |
| G6 | Chart kinds cover many primitives but no accuracy/confusion summary semantics, no dual-axis policy (deferred in 5.0), no histogram/time-series first-class documentation; placement not Harness-adjustable as a spec surface | F |
| G7 | Preflight lacks missing/invisible layer, legend mismatch, tiny text, clipped title, missing source/time, uncertainty requirement, empty-output rules (preflight-rules.md catalog is geometry/style-centric) | G |
| G8 | Visual regression: `[visual][determinism]`/`[visual][golden]` PNG cases crash in headless Windows sessions (0xC0000135-class) — 6.0 shipped with them unexercised locally; golden comparison "opt-in with generous tolerance" | H |
| G9 | Drift validation covers catalog index; no mechanical solution→recipe/template/style, recipe→operator/model, template→component, component→token/style, style→data-applicability closure checks (test_knowledge_drift.cpp is index-focused) | I |

## Duplicate-development check

All 9 gaps are absent from master and from all sibling branches (checked
`git diff --name-only master...<branch>` per branch; no branch touches
`src/agent/mapspec`, `src/agent/cartography`, or cartography data/docs/tests).
No open issue or PR requests this work. No duplication risk.
