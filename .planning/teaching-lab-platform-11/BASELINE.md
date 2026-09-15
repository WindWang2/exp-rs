# BASELINE — Phase 0 audit (2026-09-16, executed from main repo before worktree creation)

## Git facts (refreshed at start)

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  (`fix: fail-closed fixes for review issues #994–#999 (#1000)`)
- The prompt-generation snapshot (2026-09-15, `ebcafb4d`) is stale in two ways:
  - **PR #991 (D18 Unified Mission Workbench) MERGED** as `c5d4aafe`.
  - **PR #992 (D19 Dataset Foundry/Benchmark) MERGED** as `1cea9892`.
  → Per the ownership rule ("若该 PR 已合并，立即以新的 origin/master 为事实源重新审计"),
  D18/D18+D19 code is now master fact, no longer treated as a moving open PR.
- Recent master history (top 20): a5b11b7f (#1000 fail-closed fixes), 1cea9892 (#992 merge),
  c5d4aafe (#991 merge), 77e178ac (#993 CI fixes), then D19/D14/D15/D16/D17 landing commits.

## Open PRs at start (gh pr list, 2026-09-16)

| PR | branch | title | ownership overlap with this track |
|---|---|---|---|
| #1009 | `zcode/execution-runtime-convergence-11` | execution runtime convergence (authority, chunk contract, resume, governance, worker lease, telemetry) | none on `*lab*` files; touches `tests/CMakeLists.txt`, `CHANGELOG.md`, `.gitignore`, `src/agent/data_platform_tools.cpp` (shared integration files only) |
| #1008 | `zcode/radiometric-spectral-workbench` | Day 13 radiometric/6S/spectral workbench | none on `*lab*` files; touches `tests/CMakeLists.txt`, `.gitignore`, `src/agent/CMakeLists.txt` |

Both are UNSTABLE/DIRTY merge-state; neither touches `data/labs/**`, `docs/labs/**`,
`src/experiment/bridge/*lab*`, `src/cli/*lab*`, or any `*lab*` source file.
→ This track's business body is conflict-free; shared integration files get
minimal append-only edits (runbook step 5).

## Open issues at start (gh issue list, 2026-09-16)

#1001 (io:clip CRS override), #1002 (workflow registry node executor fail-open),
#1003 (dataset joinFeatures null columns), #1004 (dataset:qa scan_capped identity),
#1005 (georef mapPick CRS transform), #1006 (PipelineRunCoordinator syntheticExecute),
#1007 (dataset:qa CRS audit).
**Dedupe verdict:** all seven are D19-foundry/workflow/io/georef platform defects
owned by other tracks' domains. None is in `data/labs` / `src/cli/*lab*` /
`src/experiment/bridge/*lab*` / grading / batch / report / copilot territory.
→ OUT_OF_SCOPE for this track; recorded, not implemented, not closed.

## ISSUES.md (repo root) verdict

Old D3 operator-gap backlog (T-1..T-3, S-1..S-2, H-1..H-3, C-1..C-2). Spot-checked
against current master: these are `src/operators`/`src/processing` platform gaps,
explicitly outside this track's write scope and outside its packages. NOT treated
as live backlog. (Where a lab's grading needs a capability, this track grades
artifacts with independent kernels — it does not add operators.)

## Existing capability map (code-read evidence)

### Grading (D4, ADR 0150) — `src/agent/output_verifier.{h,cpp}` (2413 lines)
- `LabGradeResult` (verdict pass/fail/unverifiable, score, deductions w/ evidence,
  evidence for every assertion, digest over canonical body, errorClass usage/artifact).
- 9 kernels: `range`, `mean_sigma`, `gain_invariance`, `nodata_ratio`,
  `histogram_shape`, `classification_kappa`, `confusion_marginals`,
  `change_area_interval`, `crs_grid`. One windowed streaming pass under
  `--max-bytes` (64 MiB default).
- Rules schema `sicnu.lab.rules/1`, weights sum to exactly 100, blocking severity
  caps at passing_score−1, `derivation` field per assertion.
- CLI: `sicnu_geo_rs_cli lab --lab <id> --grade <artifact> [--out] [--max-bytes]`,
  exit contract 0/1/2/3.

### Batch (D7, ADR 0147) — `src/cli/lab_batch_runner.{h,cpp}` + `cli_lab_commands.cpp`
- Streams submissions dir → CSV (UTF-8 BOM, flushed per row), student_id = file
  stem, error rows isolated, exit 0/1/2. CSV only; no roster, no duplicate
  detection, no JSON/HTML summary.

### Report (D5) — `src/experiment/bridge/lab_report.{h,cpp}` + writers + run_recorder
- `sicnu.labreport.v1` projection: runs from ExperimentStore, steps from
  operation trail with declared attribution policy, lineage slice,
  `ReplayReadiness::assess`, `deepRedactSecretKeys`, deterministic bytes,
  Markdown/HTML renderers.
- **Grade embedding exists as a typed seam but is NEVER "recorded":** the only
  GUI construction site hardcodes `LabGradeEmbedding::unavailable()`
  (`src/app/main_window_project.cpp:446`). No CLI surface at all
  (`LabReportBuilder` referenced only by src/app + its test).

### Copilot (D9) — `src/agent/harness/lab_{copilot,spec,tools,glossary,intent,diagnostics}.*`
- `labAsk`/`labReference`, role from session only, TEACHING_REFUSAL typed envelope,
  LabSpecCatalog step-anchored answers (param values never leave lab_spec),
  role-gated action twin, Chinese-first glossary.
- Evals: `tests/test_harness_lab_evals.cpp` (1216 lines) — refusal matrix,
  role-escalation (incl. `claimed_role` authority-claim attack), taxonomy closure,
  diagnostics. No dedicated prompt-injection corpus beyond the role-claim case.

### Offline (D7, ADR 0147/0153)
- `src/data/offline_mode.{h,cpp}` process gate (`--offline`/SICNU_OFFLINE),
  three enforcement depths incl. GDAL network deny; CLI wires it (main_cli.cpp:180-209).
- `packaging/OFFLINE_BUNDLE.md` contract + `packaging/bundle/{RUN,GRADE_ALL,VERIFY,...}`
  + `scripts/build_offline_bundle.{sh,cmd}`, `bundle_manifest.ps1`,
  `scripts/offline_smoke.sh`, `docs/deployment/lab-offline.md`.

### LabSpec / content (D2/D3/D18)
- 17 labs in `data/labs/`: 11 classic `.lab.json` + 4 labspec `.labspec.json`
  (temporal/SAR/hyperspectral/cartographic) + schema `labspec.schema.json`.
- `tests/test_labspec.cpp` drift guards: every operator_id resolves in registry,
  params validate against operator descriptors, grading_ref resolves, committed
  docs == generated docs.
- Grading coverage gap (verified by file listing):
  - Executable `.rules.json` exist for 6 grading labs: ndvi_basics, ndvi_bandpair,
    landcover_classify, change_detect, terrain_slope, planck_temperature
    (≈ labs 01/02/03/04/05/08-atmos).
  - Labspec labs 8/9/10/11 have `grading_ref` + `data/labs/grading/*.intent.json`
    that explicitly say "只声明断言意图，不实现判分器" — **no executable rules**.
  - lab06_georeferencing, lab07_image_fusion, lab09_pca_analysis, lab10_mosaic:
    no grading_ref, no rules.
- Fixtures: `tests/fixtures/lab/` committed GeoTIFFs + `reference_corpus.json` /
  `wrong_answer_corpus.json` + `generate_fixtures.py` (6 grading labs only).
- Data specs: `data/labs/data-specs/*.json` (sicnu.lab-data-spec.v1) exist only for
  the 4 labspec labs; no checksums/licenses/sizes; nothing validates that a lab's
  `prerequisites[].path` files exist with declared content.

## Gaps chosen for 11.0 (mapped to work packages)

| Pkg | Gap (evidence above) | Deliverable |
|---|---|---|
| A | data-specs cover 4/17 labs; no checksum/license/size contract; no validator | `sicnu.lab-pack/1` manifest + loader/validator + full-lab coverage |
| B | labspec labs 8–11 intents unimplemented; no position-sensitive / zone-join / temporal kernels | new kernels (in `src/agent/lab_grader_kernels.*`) + rules.json for labs 8–11 + fixtures + corpus |
| C | CSV-only batch; no roster/identity/dedupe/summary/caps | batch 2.0: identity, duplicate detection, JSON/HTML summary, caps |
| D | grade seam never "recorded"; report GUI-only | `lab --report` CLI + recorded grade embedding + secret-redaction regression |
| E | role-claim attack covered; broader injection/leak corpus missing | injection regression corpus + answer-leak evals |
| F | bundle exists; no in-process environment self-check with typed diagnostics | `lab --self-check` (offline gate, registry, pack integrity) |
| G | labspec drift guards exist; labs 06/07/09/10-mosaic ungraded; content vs 11.0 registry | grading_ref/rules where science allows; drift test extension |
| H | no batch scale evidence | synthetic 1000-submission scale test (opt-in), bounded default gate |

## Architecture authorities (do not duplicate)

- Grading authority: `OutputVerifier::gradeArtifact` seam (ADR 0150) — batch/report
  must wrap it, never re-implement scoring.
- Lab spec authority: `data/labs/*.lab.json|.labspec.json` + loader in
  `src/app/widgets/lab_spec_loader.h` (GUI) + `LabSpecCatalog` (agent harness).
- Report authority: `sicnu.labreport.v1` builder (projection of recorded truth).
- Offline authority: `sicnu::data::offline` gate + `packaging/OFFLINE_BUNDLE.md`.
- Registry authority: `ProcessingRegistry`/`AtomicAlgorithmRegistry` (operator ids).
- No second scheduler / second model catalog / second grading engine.
