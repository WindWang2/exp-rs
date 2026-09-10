# Dataset / Experiment / Reproducibility Platform 7.0

Closes the goal "Dataset / Experiment / Reproducibility Platform 7.0" (sections A–G).

## Architecture

All additions are **thin adapters over the authoritative stores and engines**
(`DatasetStore`, `ExperimentStore`, SplitEngine, LeakageAuditor, evaluation,
RunComparison, ReproductionBundleExporter). No second scheduler, no new
persistence format; the store schema stays v1 with **additive tables only**
(`split_manifests`, `leakage_reports`, `sample_facets`, `quality_summaries`).
GUI untouched; QGIS untouched; workflow/jobs untouched.

New units:
| Unit | Goal | Purpose |
|---|---|---|
| `src/dataset/dataset_store_splits.cpp` | §18/§19 | Split manifest persistence (immutable per id, conflict on content change) + append-only leakage report history (digest-keyed) |
| `src/dataset/sample_promotion.{h,cpp}` | B | Classification/segmentation/annotation/pair/temporal → governed samples in DRAFT versions |
| `src/dataset/fold_audit.{h,cpp}` | D | Per-fold materialization + leakage audit, per-fold class balance with zero-ratio flags, deterministic replay check |
| `src/dataset/dataset_store_facets.cpp` | E | Facet side table (draft-only), SQL-side bounded distributions + cross-facet cells, quality cache with staleness stamps |
| `src/experiment/run_recorder.{h,cpp}` | C | TaskCenter/Workflow → ExperimentRun adapter with truthful terminal states and stale-run reconciliation |
| `src/experiment/replay_readiness.{h,cpp}` | F | Per-dependency replay checks, honest Exact/BestEffort/Impossible levels, equivalent-run (duplicate execution) lookup |
| `src/experiment/comparison_ext.{h,cpp}` | G | Evaluation-protocol + label-schema compatibility, paired run deltas with support gates |
| `src/agent/data_platform_tools.{h,cpp}` | A | 15 MCP tools: `dataset:*`, `experiment:*`, `reproducibility:*` |
| `src/cli/cli_dataset_commands.cpp` + `cli_commands.cpp` | A | CLI parity verbs + routing fix |

## Key design decisions

- **Class identity**: pipeline raw values (e.g. a cv::Mat label integer)
  exist only as promotion-time rules mapped to label-schema class codes;
  unmapped values refuse the promotion listing every gap — nothing silently
  dropped, and a raw value never becomes identity.
- **Split pins became real**: the audit found runs citing `splitManifestId`s
  no store could resolve. Splits are now persisted content (immutable per
  id; re-saving different content is a conflict), so dataset/split
  fingerprints in runs and bundles are verifiable.
- **Honesty gates everywhere**: replay levels never overstate (missing →
  Impossible, unknown → BestEffort, everything pinned → Exact); leakage
  audits cite only persisted evidence; paired comparison flags
  `insufficient_support` instead of inventing significance; a facet
  distribution's bounded tail is an explicit `(other)` bucket.
- **Truthful run states**: failures store error evidence, cancellations
  store reasons, and `reconcileStaleRuns` reports crash-orphaned runs
  read-only — nothing is ever auto-closed as success.

## Also fixed (pre-existing defects found by the audit)

- `isCliCommand` routing list lacked `dataset`/`experiment`/`reproduce` —
  the Foundation 5.0 CLI verbs were unreachable dead code.
- `LeakageReport` lost `sample_count`/`digest_unknown_count` on JSON
  round-trip (now serialized).

## MCP surface (A)

`dataset:list`, `dataset:inspect`, `dataset:version`, `dataset:diff`,
`dataset:stats`, `dataset:validate`, `dataset:label_schema`,
`dataset:split_inspect`, `dataset:leakage_audit` (stored|run),
`experiment:list`, `experiment:inspect`, `experiment:compare` (now with
protocol/schema compatibility + paired summary when metric records exist),
`reproducibility:inspect`, `reproducibility:export`, `reproducibility:validate`.
All outputs paged/bounded; environment/parameters/metrics re-redacted at
read time; unknown ids are typed errors, never crashes.

## Tests (local evidence, Windows/MSVC/Ninja)

| Suite | Result |
|---|---|
| `test_data_platform_surface` (new) | 101 assertions / 5 cases — green |
| `test_platform7_library` (new) | 228 assertions / 15 cases — green |
| `test_dataset_core` | 155 / 12 — green |
| `test_sample_label_annotation` | 98 / 11 — green |
| `test_split_leakage` | 672 / 20 — green |
| `test_experiment_evaluation` | 162 / 14 — green |
| `test_dataset_e2e_examples` | 57 / 4 — green |
| `test_adversarial_m2` | 1105 / 8 — green |
| `test_dataset_quality_scale` (100k) | 733 / 3 — green |

Adversarial review (1 read-only subagent): 0 P0; 3 P1 + CLI-twin P1 all
fixed (fake availability hook, mutating validate, Exact-overstatement);
P2/P3 fixed or documented (see `.planning/dataset-experiment-7/REVIEW_LOG.md`).
Build: Debug, Ninja, j2–j6 within observed RAM headroom (32 GB), CTest
parallelism 1; no CI dependency.

## Performance / resources

- Facet questions are SQL `GROUP BY` with bounded results; the bounded tail
  is reported, never hidden.
- Audits assemble through 500-row pages; pairwise checks stay bucketed
  (~linear at 100k, pinned by the existing 100k quality-scale suite).
- Promotion writes samples in one batch transaction before annotations; a
  partial failure reports exact committed counts.

## Compatibility

- Additive store tables only; older readers unaffected. All new symbols are
  additive; `mcp_server.cpp` change is a first-match dispatch branch plus a
  tools/list append, so existing tools are unaffected. Existing test suites
  pass unchanged.

## Known limitations / follow-ups

- CLI and MCP share verbs but not projection code (a shared projection would
  force the CLI to link the GUI-level agent library).
- `ExperimentRunRecorder` is the callable seam adapter; wiring it into every
  workflow state hook is follow-up integration work.
- `get_tool_schema` (Agent Tool Catalog) does not resolve the new
  namespaced tools; schemas come from `tools/list?includeSchemas=true`.
- Near-duplicate detection relies on caller-supplied digests (no invented
  perceptual hashing); comparison_ext drops nested non-numeric subtrees
  silently (documented).
- The CLI binary's cold start in the headless automation environment runs
  minutes (QGIS bootstrap); CLI logic is compile-verified and its store
  behavior is covered by the Catch2 suites.
