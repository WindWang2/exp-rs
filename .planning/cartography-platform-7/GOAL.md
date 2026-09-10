# GOAL — Intelligent Cartography / Template / Recipe Platform 7.0

Push the cartography platform from "knowledge + bounded solver" (Platform 6.0) to an
**explainable, componentized, composable, visually verifiable declarative cartography
system**. MapSpec must not only "render", but explain: why a template was chosen, why
this layout was produced, which constraints conflict, and how to repair.

## Non-negotiables

- QGIS stays the single rendering/layout authority; MapSpecCompiler compiles through
  LayoutService item factories; no second render engine.
- Everything bounded: relaxation passes, repair iterations, drift scans, search pages.
  Every automatic repair/relaxation is explainable, deterministic and cancellable.
- No silent moves: every geometry change traces to a declared constraint/anchor; every
  failure reports *why* (unsat core, conflicting identities), never a guessed fix.
- FAIL is never wrapped as success: unverifiable checks report warning/unknown.
- Master is read-only; all work on `feat/cartography-platform-7` in
  `C:/Users/wangj.KEVIN/projects/exp-rs-cartography-platform-7`.
- Builds: `-j2` max (`CMAKE_BUILD_PARALLEL_LEVEL=2`), tests `CTEST_PARALLEL_LEVEL=1`.
- At most 2 subagents total (read-only audit / adversarial review). Main agent owns all
  implementation and final merge responsibility.

## Work packages (from the goal definition)

| ID | Package | Key deliverables |
| --- | --- | --- |
| A | Constraint Solver 7.0 | hard/soft constraints, priority, weighted objective, unsat core explanation, anchor authority, multi-fixpoint deterministic policy, collision resolution, bounded solve budget, explain-failure reports |
| B | Template inheritance/variants | base/domain/page-medium/sensor-task layers, facets merge policy, explicit override, cycle detection, migration away from filename-encoded duplicates |
| C | Component system | title/subtitle, legend composite, color ramp, north arrow, scale bar, coordinate/grid, locator/overview, source/metadata, uncertainty note, statistics panel, accuracy report, temporal chart, class composition chart, logo/footer — all with role/bounds/style-token/applicability |
| D | Typography / text layout | font metrics, wrapping, multi-line, truncation policy, fitting, overflow diagnostics, legend text, CJK, missing-font fallback, deterministic geometry |
| E | Style semantics 7.0 | categorical/continuous/diverging, NoData, uncertainty/confidence, multi-band, SAR backscatter, DEM/hillshade, class ontology mapping, accessible contrast checks, data applicability; wrong style → reject or deterministic repair |
| F | Charts | unified chart spec: histogram, line/time-series, bar/class composition, scatter, accuracy/confusion summary; dual axis only when semantically justified; layout placement + Harness adjustability |
| G | Map QA / preflight / repair | missing/invisible layer, legend mismatch, off-page, overlap, tiny text, clipped title, missing source/time, uncertainty requirement, empty output; bounded explainable repair |
| H | Visual regression | structural hash, geometry known-answer, compile determinism, render golden, platform tolerance; root-cause the headless-Windows PNG crash or provide honest alternative evidence |
| I | Knowledge drift | mechanical validation solution→recipe/template/style, recipe→operator/model, template→component, component→token/style, style→data applicability; no dangling refs on catalog growth |

## Completion definition

MapSpec is a real declarative cartography language: it explains why the template was
chosen, why the layout looks like this, where conflicts are, and how to fix them — with
local build/test evidence for every claim, adversarial review remediated, PR submitted.
