# BASELINE — audit of origin/master @ 2d4f0daedd (2026-09-11)

## Repository / environment state

- Branch `feat/cartography-platform-8` created from **origin/master @
  `2d4f0daedd`** (Merge PR #836 fix/ci-ipc-jsoncpp). Master is read-only;
  all work happens in the worktree
  `/home/kevin/projects/rs-studio/exp-rs-cartography-platform-8`.
- **0 open PRs, 0 open issues** at execution time.
- Latest 30 merged PRs audited. Direct predecessor of this track:
  **PR #832** *feat(cartography): Intelligent Cartography / Template / Recipe
  Platform 7.0* (merged 2026-09-10, tip `c9bc384914`). Cartography lineage:
  #763 (4.0 Design System) → #768 (5.0 Solution Templates) → #820 (6.0
  Knowledge Platform) → #832 (7.0 Explainable Solver). Post-#832 merges
  (#833–#836) are CI fixes only (ipc_channel/jsoncpp/ExternalProcess/
  range_cache) — no cartography overlap.
- Sibling in-flight 8.0 worktrees present locally: execution-plane-8,
  geospatial-data-fabric-8, model-runtime-8, scientific-processing-8,
  verification-platform-8, dataset-experiment-mlops-8 (all target other
  domains; `git diff --name-only` spot checks show no overlap with
  `src/agent/mapspec`, `src/agent/cartography`, `data/cartography`,
  cartography tests/docs).
- **Environment is Linux** (previous platforms 4–7 were developed on
  Windows-headless). QGIS system install with headers at `/usr/include/qgis`,
  Qt6 at `/usr/lib/cmake/Qt6`, Ninja, gcc `/usr/bin/c++`, 16 cores, 62 GB
  RAM. Existing master build dir (`main/build`) is valid and reused for
  baseline evidence only.
- **Pivotal verification**: the `[visual]` PNG cases PASS on this machine
  (ctest #314 "Rendering is deterministic: identical spec → identical PNG
  hash" — 31 s real QgsLayoutExporter render; #316 golden comparison). The
  7.0 known limitation "headless Windows QgsLayoutExporter hard stop" does
  not apply here → desktop-capable PNG golden evidence is genuinely
  closable in this track.

## Code baseline (this track's ownership)

| Module | Files | Notes |
| --- | --- | --- |
| `src/agent/mapspec/` | 6 | MapSpec v4 document model, compiler⇄QgsPrintLayout bridge (LayoutService), bounded conditional visibility |
| `src/agent/cartography/` | 20 | registries (component/template/token/style/solution/chart), composition solver (hard/soft/priority/unsat-cores), preflight/repair (37 rules), style compiler, typography engine |
| `src/agent/layout_tools/` | 6 | LayoutService — programmatic QGIS layout seam (addItem types: map/legend/scalebar/northarrow/chart/picture/label/shape/title) |
| `src/agent/harness/` | 22 | AgentPlan v2 (`map_output`, `verification.final_map`), `confirmMapOutput` compose→preflight→repair(≤3) loop in plan_tools.cpp |
| tests | `test_mapspec` bundle: 11 files | tags [mapspec][cartography][platform5][platform6][platform7][drift][visual][determinism][golden] |
| data catalog | components 57, templates 56, styles 17, solutions 44 | `data/cartography/**`, drift-enforced `index.json` |

## Architecture facts (authoritative seams — unchanged by this track)

- ADR 0127 (MapSpec declarative cartography), ADR 0131 (v2 composition),
  ADR 0130 (design tokens). QGIS is the only rendering engine; MapSpec
  compiles through `MapSpecCompiler → LayoutService → QgsPrintLayout`.
- Solver: bounded relaxation (≤24 hard passes, ≤8 soft passes), canonical
  ordering, decisions ledger ≤256, bounded unsat-core search
  (≤8 candidates × ≤3 subset × ≤32 simulations). Cycle detection over
  leader→follower edges covers keep_with.
- Preflight/repair: `preflightRuleCatalog()` machine catalog; deterministic
  bounded repair; typography engine shared by preflight and repair so
  repairs provably converge.
- StyleSpec → QGIS renderer primitives (StyleCompiler); token resolution
  transitive ≤8 hops; applicability + 7.0 semantics (scheme/nodata/
  uncertainty/contrast) validated at load/apply.
- Visual evidence: 3 layers (PNG determinism hash, geometry contracts,
  opt-in goldens at 25% downscale MAD<12) + rendering-free structural
  digest (SHA-256 over canonical resolved geometry).

## Confirmed gaps → Platform 8.0 work packages (evidence-backed)

| # | Gap (evidence on master) | Package |
| --- | --- | --- |
| G1 | `style_spec.cpp` validates `raster.nodata {value,transparent,label}` but `style_compiler.cpp applyRasterBlock`/`buildRasterRenderer` never touch it — no `QgsRasterDataProvider::setUserNoDataValue`, no `QgsRasterRenderer::setNodataColor` (grep: zero nodata matches in style_compiler.cpp; 7.0 FINAL_REPORT "Known limitations" admits it) | A |
| G2 | Locator insets compile to QGIS map-overview extent indicators + caption, but **no connector line** from the inset to the referenced frame (`docs/cartography/limitations.md` "Remaining nuance: connector lines … are not drawn") | A |
| G3 | Accuracy-summary component descriptor **exists and is complete** (`data/cartography/components/accuracy--report.json`: roles, bounds, style_tokens, data_bindings) → **already satisfied** | A |
| G4 | PNG golden evidence blocked on Windows-headless in 7.0 → **Linux renders**; produce real golden evidence + record methodology | A |
| G5 | 7.0 accepted P3s re-evaluated: 3-digit hex not contrast-checked (#rrggbb documented) — now fixable cheaply; mixed v1/v2 required_slots stale key — pre-existing, document; others remain accepted | A |
| G6 | MapSpec has **no output/export declarations** (no `output`/`export` fields anywhere in mapspec.cpp/validate); export gating in harness mentions but cannot read a declaration | B |
| G7 | `binding` per-item field validated only as "must be an object" (mapspec.cpp:125) — no shape/size bounds, unlike chart binding validation | B |
| G8 | Solver keep_with/avoid_overlap/inside pin followers in page-mm space with **no page-height awareness** — a pin near a page bottom produces off-page geometry silently (MAP_OFF_PAGE catches it only later in preflight) | C |
| G9 | Typography: kinsoku forbids breaks but no hanging-punctuation policy (trailing closing punctuation can overflow; leading opening punctuation can dangle); no declared line-height override | D |
| G10 | Chart/table render path draws raw labels with `painter.drawText` — no declared label budget; long CJK labels overflow cells silently (pixel path, so bounded by px, but no policy) | H |
| G11 | Preflight has no NoData-legend rule: a legend referencing a style that declares `raster.nodata` is not checked for any nodata mention | I |
| G12 | `cartography:compose` output carries quality but not the **structural digest**; `confirmMapOutput` cannot identify *what* was composed (7.0 shipped `structuralDigest` but only visual tests consume it) | J |
| G13 | mapspec-reference.md headline still says "current: 3.0" and the envelope example says `spec_version ≤ 3` — stale vs code v4 (docs drift) | docs |

## Duplicate-development check

None of G1–G13 is present on master or any surviving remote branch (checked
merged 7.0/6.0 branches + open `feat/*-8` worktrees; no branch touches these
files). No open issue/PR requests this work. No duplication risk.
