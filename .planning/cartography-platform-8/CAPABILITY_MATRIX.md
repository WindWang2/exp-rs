# CAPABILITY MATRIX — cartography platform (master @ 2d4f0daedd)

Legend: Implemented (local evidence) / Partial (subset) / Stub / Missing /
Refused-by-contract / Duplicated / Unverified (no local execution evidence).

| Capability | State | Evidence / note |
| --- | --- | --- |
| MapSpec v4 document model + validation + migration v0→v4 | Implemented | mapspec.{h,cpp}; 52/52 baseline ctest |
| Compiler MapSpec→QgsPrintLayout via LayoutService | Implemented | test #275 roundtrip |
| Conditional visibility (visible_if/content_if/page_if) | Implemented | v3 surface, bounded |
| Atlas (coverage/filter/sort/margins/numbering) | Implemented | atlas-guide.md; per-feature symbology refused-by-contract (QGIS authority) |
| Multi-page documents (≤10 extra pages, page roles) | Implemented | v3 |
| Insets + locator overview extent indicators + caption | Implemented | mapspec_compiler.cpp:380+ |
| Locator **connector/relationship graphics** | **Missing** | limitations.md; G2 → M2 |
| Nested insets | Implemented | depth author-declared |
| Solver: hard/soft, priority, weight, canonical order | Implemented | composition.cpp; P7 tests |
| Solver: bounded unsat cores, decisions ledger, winner evidence | Implemented | P7 tests |
| Solver: **page-height-aware relative constraints** | **Missing** | G8 → M4 |
| Typography: width classes, wrap, kinsoku, truncation policies, fit reports | Implemented | typography.cpp; P7 tests |
| Typography: **hanging punctuation / declared line-height** | **Missing** | G9 → M5 |
| StyleSpec → QGIS renderers (gray/pseudocolor/paletted/multiband; vector families) | Implemented | style_compiler.cpp |
| Style semantics: scheme/nodata/uncertainty/applicability/contrast/ontology | Implemented (validation) | P7 tests |
| **NoData applied to QGIS renderer** | **Missing** | G1 → M1 (7.0 admitted gap) |
| Charts: 15 kinds + accuracy_summary + justified dual-axis | Implemented | chart_registry.cpp; P7 tests |
| Charts: **declared label budget for long/CJK labels** | **Missing** | G10 → M7 |
| Components: 57 descriptors, variants, composites, style_tokens | Implemented | registry.cpp; drift tests |
| Accuracy-summary component | Implemented | accuracy--report.json (G3 closed) |
| Templates: 56, facets, extends multi-parent, variants | Implemented | registry.cpp; P7 tests |
| Template clone audit (Track F) | Implemented (no action needed) | 7.0 facet/variant consolidation; audit sample found no conflicting clones; filename variants (a4l/a4p/a3l) are distinct page-geometry artifacts, not clones |
| Preflight: 37 rules + bounded converging repair | Implemented | quality.cpp |
| Preflight: **NoData legend rule** | **Missing** | G11 → M6 |
| Structural digest (rendering-free known-answer layout id) | Implemented | quality.cpp; P7 pinned |
| PNG determinism + opt-in goldens | Implemented / **Unverified on Windows** | **Verified on Linux this track** (#314/#316 pass) → M8 records evidence |
| Harness `confirmMapOutput` (compose→preflight→repair loop) | Implemented | plan_tools.cpp:826+ |
| Compose report → **structural digest + provenance in confirmation** | **Missing** | G12 → M7 |
| Output/export declarations in MapSpec | **Missing** | G6 → M3 |
| Deeper `binding` validation | **Missing** (object-only) | G7 → M3 |
| Solution search explanations | Implemented | searchSolutions match.reasons (solution_registry.h) |
| Workbench inspection of declarative components | Implemented | styledock/templates UI + registries (4.0/6.0) |
| Style:apply edit-back tracking | Refused-by-contract | one-way compilation documented (limitations.md) |
| Per-glyph font metrics | Refused-by-contract | platform-independent width classes are the documented model |
