# PLAN — teaching-lab-platform-11

Baseline: `origin/master@a5b11b7f`. Full audit: BASELINE.md. Ownership: PARALLEL_OWNERSHIP.md.

## Vertical-slice order (each slice = implement → test → commit)

### Phase 1 — contracts/authority (packages A, C foundations)
1. **A: `sicnu.lab-pack/1`** — lab data pack manifest contract:
   - `data/labs/packs/<lab_id>.pack.json` per lab: inputs[] {path, sha256, bytes,
     role, license, generator, sensor_truth}, declared offline size, version.
   - Loader/validator `src/agent/lab_data_pack.{h,cpp}` (namespace
     `sicnu::agent`): parse, validate, verify files on disk (existence+size+sha256),
     typed result (ok / missing / checksum-mismatch / size-ceiling-exceeded),
     deterministic JSON summary. Reuses existing sha256 utility (find in src).
   - Write packs for all 17 labs' real prerequisites (data/samples/*) — compute
     checksums from committed files at authoring time.
   - Tests `tests/test_lab_data_pack.cpp`: known-answer (valid pack), negative
     (corrupt byte → mismatch; missing file; oversize), unicode path, read-only dir.
2. **C foundation: submission identity model** in `src/cli/lab_batch_runner.*`
   (append): `SubmissionIdentity {studentId, artifactPath, sha256, bytes}` +
   content-hash duplicate detection (same sha256 twice → flagged duplicate, graded
   once, reported in summary; identity = student_id from filename, verified against
   optional roster). Roster CSV loader (student_id,display_name) with BOM handling.

### Phase 2 — grader 2.0 kernels (package B)
3. New module `src/agent/lab_grader_kernels.{h,cpp}` (namespace sicnu::agent):
   - `zone_stats` kernel: join aux zones raster (same grid) → per-zone mean/range
     assertions (drives lab8 zone-NDVI intent T2).
   - `band_layout` kernel: band count/valid-fraction-per-band (lab8 T1, lab9 stack
     integrity) — extends crs_grid to multi-band validity.
   - `series_separation` kernel: per-zone temporal separation (lab8 T3 trend
     slope: disturbance zone ≤ bound AND separated from stable zones by ≥ margin).
   - `spectral_signature` kernel: spectral angle mapper distance of pixel spectra
     to declared reference spectrum (lab10; SAM closed form, tolerance from
     intent).
   - `spatial_agreement` kernel: position-sensitive agreement — fraction of pixels
     whose class/zone equals declared truth within tolerance of confusion-based
     bound (fixes GRADING.md "known limit": right-area-wrong-place now scores
     below pass; used with zones truth raster).
   - Each kernel: windowed streaming, budget-capped, evidence JSON, deterministic;
     independent oracle in tests (recompute expected from fixtures in test code,
     NOT by calling the grader).
4. Minimal additive dispatch in `src/agent/output_verifier.cpp` for new kinds
   (separate streaming pass per new kernel family, outside the value-kernel pass).
5. Rules files: `data/labs/grading/{temporal_analysis,sar_processing,
   hyperspectral_analysis,cartographic_mapping}.rules.json` implementing the
   corresponding `.intent.json` (weights sum 100, derivation fields quoting the
   intent tolerances). Fixtures via `scripts/gen_lab_fixtures.py` extension
   (`scripts/gen_lab_grading_fixtures.py` new — committed GeoTIFFs under
   `tests/fixtures/lab/` follow existing deterministic closed-form tradition) +
   corpus manifest entries (reference=100, wrong variants with declared fired
   assertion ids).

### Phase 3 — batch classroom 2.0 (C) + report CLI (D)
6. `lab --batch` v2: `--roster <csv>`, `--json <summary.json>`, `--html
   <summary.html>`; per-run summary {counts, duplicates[], per-student rows,
   caps}; identity checks (unknown student in roster → flagged, still graded;
   duplicate sha256 → single grade + duplicate record); atomic summary writes
   (tmp+rename); caps: max submissions per run (default none, env/flag), maxBytes
   per artifact (existing), wall-clock-free determinism (summary has no
   timestamps in body).
7. `lab --report`: headless `sicnu.labreport.v1` export via LabReportBuilder —
   `--experiment <id>`, `--student`, `--grade <transcript.json>` (wires
   LabGradeEmbedding recorded from an existing grade transcript file: gradingRef
   = transcript digest/path, inlineResult = report body), `--out` md/html/json;
   additive registered in cli_lab_commands parser delegating to new
   `src/cli/lab_report_runner.{h,cpp}`; secret-redaction regression test (trail
   with credential-looking keys must come out redacted in all 3 renderings).

### Phase 4 — surfaces (E, F, G integration)
8. **E**: injection/leak corpus `src/agent/harness/lab_injection_corpus.{h,cpp}` +
   evals in test_harness_lab_evals.cpp: ignore-previous-instructions, teacher
   impersonation via message, "print the params/solution" extraction attempts,
   suggestion smuggling — all must end in TEACHING_REFUSAL or answer without
   param values. Chinese terminology verbatim checks (already partly covered).
9. **F**: `lab --self-check`: environment diagnostic — offline gate state,
   GDAL data dir resolvable, registry loads, lab pack verification (Phase 1
   validator) for N labs or `--lab`, grading rules parse, fixture presence;
   typed JSON diagnostic + exit contract; offline-mode test proves zero network
   (existing gate) and self-check passes offline.
10. **G**: extend `tests/test_labspec.cpp`-style drift guard to rules/packs:
    every rules.json references existing fixture paths in corpus manifests; pack
    manifests cover every lab dir entry (no unlisted lab); labspec grading_ref
    → rules presence where declared. Content refresh only where machine-validated.

### Phase 5 — scale/failure (H)
11. `tests/test_lab_scale.cpp` (opt-in label `[scale]`): synthetic 1000
    submissions (tiny valid/invalid GeoTIFFs generated in tmp), bounded memory
    via existing streaming, deterministic scores (same bytes → same digest),
    duplicate storm, cancel mid-run → CSV prefix intact + summary records
    interrupted state; default CI gate uses 100-submission bounded scale.
    Record RSS evidence in PERFORMANCE.md.

### Phase 6 — E2E + docs
12. E2E `lab` chain: fixtures → grade (pass) → wrong corpus (fails as declared)
    → batch v2 with roster/duplicates → report with recorded grade → self-check;
    offline: whole chain under `--offline`.
13. Docs: `docs/labs/GRADING.md` (new kernels, batch v2, report CLI),
    `docs/labs/DATA_PACKS.md`, `docs/deployment/lab-offline.md` (self-check),
    regenerate `docs/labs/*.md` via gen_lab_docs.py, CHANGELOG entry.

### Phase 7 — review; Phase 8 — double-verify + PR
Per runbook: full-diff self review → independent read-only reviewer subagent →
P0/P1 fixes → rebase → targeted gates twice → push → `gh pr create`.

## Oracle mapping

1. reference 100 / corpus deductions independently explained → TEST_MATRIX rows
   for corpus contract (test_lab_grading extended to new labs)
2. student cannot reach teacher surfaces → existing evals + new injection corpus
3. offline zero-network + grading/batch runnable → self-check test + offline E2E
4. lab suites pass twice + report secret-clean → Phase 8 double run + redaction test
5. diff hygiene gates → Phase 8
6. double validation → Phase 8
7. review dispositions → REVIEW_LOG.md
