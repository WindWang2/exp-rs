# feat(labs): teaching lab, grading & reproducibility platform 11.0

**Baseline:** `origin/master@a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
(2026-09-16; PRs #991/#992 already merged into it and re-audited as master fact).
**Branch:** `zcode/teaching-lab-platform-11` — worktree `../exp-rs-teaching-lab-platform-11`.
**Local evidence only; no online CI dependency.** All claims below come from
runs executed on this workstation (Windows 11, MSVC 2022, Release, ninja -j2,
ctest/tests -j1, QT_QPA_PLATFORM=offscreen).

## Mission

实验数据、批改、复现报告、离线课堂和教师工作流完善 — converge the
data-pack → execute → grade → report → classroom-batch → reproduce chain into
a reliable teaching platform, without re-touching D18's LabSpec UI or D19's
foundry/benchmark code.

## Dedupe / parallel ownership (audited at start, PARALLEL_OWNERSHIP.md)

- Open PRs at audit time: #1009 (execution runtime), #1008 (radiometric/
  spectral) — **zero changed-file overlap** with this track's business files.
  Shared integration files (`tests/CMakeLists.txt`, `src/*/CMakeLists.txt`,
  `.gitignore`, `CHANGELOG.md`) touched append-only/minimally.
- Open issues #1001–#1007: platform-domain defects (io/workflow/dataset/
  georef) owned by other tracks — recorded in BASELINE.md, deliberately not
  fixed, not closed, not commented.
- `ISSUES.md` (old D3 operator-gap backlog): treated as historical线索 only;
  nothing implemented from it.

## Delivered capabilities (work packages A–H)

- **A — lab data pack contract** (`sicnu.lab-pack/1`): 17 per-lab deployment
  manifests in `data/labs/packs/` — sha256-pinned committed fixtures,
  presence/soft-size checks for regenerable inputs, license + sensor-truth
  declarations, declared offline bytes. Read-only streaming loader/verifier
  (`src/agent/lab_data_pack.*`, 1 MiB chunks, Unicode-safe, never writes);
  deterministic authoring tool `scripts/gen_lab_packs.py` with `--check`
  zero-diff drift gate; docs `docs/labs/DATA_PACKS.md`.
- **B — grader 2.0**: six new assertion kernels behind the UNCHANGED
  `OutputVerifier::gradeArtifact` seam (ADR 0150) — `zone_stats` (per-zone/
  band mean bounds, cross-zone deltas, ENL ratio vs a reference raster,
  Fisher separability), `band_layout` (band count, per-band valid fractions,
  exact valid-pixel counts), `spatial_agreement` (**position-sensitive**:
  binary hit/false-alarm rates, per-zone label accuracy, continuous |diff|
  tolerance), `series_separation` (weighted pooled OLS slope over a declared
  axis), `spectral_signature` (SAM spectral angle), and file-mode
  `file_check` (PNG page geometry from IHDR, MapSpec structural validation +
  cartography preflight catalog). Rules files for labspec labs 8–11
  (`temporal_analysis`, `sar_processing`, `hyperspectral_analysis`, file-mode
  `cartographic_mapping`) implement the D3 grading intents — weights sum to
  exactly 100, derivations quote intent tolerances.
- **Deterministic grading fixtures**: closed-form committed scenes
  (`tests/lab_grading_fixture_gen.cpp`, no RNG, no timestamps, EPSG:4326
  hard-required; osgeo-free regeneration path) + reference/wrong corpus
  entries wired into the existing `test_lab_grading` known-answer machinery
  (10 labs now grade through it, wrong answers land in declared score bands
  with declared fired assertions).
- **C — batch classroom 2.0**: `lab --batch` gains `--roster`
  (student_id,display_name cross-check with unknown/missing flags), sha256
  submission identity with `duplicate_of` flags (machine flags, teacher
  judges), `--max-submissions` caps, a cancel probe, and deterministic
  JSON/HTML summaries (`sicnu.lab.batch-summary/1`, rows sorted by student,
  no wall-clock values, atomic QSaveFile writes). CSV schema unchanged; D7
  streaming/bounded-memory semantics unchanged.
- **D — replay/report**: `lab --report --experiment-db <db> --experiment <id>
  --report-out <base>` exports the D5 `sicnu.labreport.v1` projection (same
  LabReportBuilder + writers as the GUI) as json/md/html.
  `--grade-transcript` embeds a `lab --grade --out` document as the RECORDED
  grade variant (gradingRef = the transcript digest — verifiable, never
  orphaned). The report grade seam is no longer hardcoded to "unavailable".
- **E — copilot safety evals**: code-resident injection/leak corpus
  (`lab_injection_corpus.*`, 16 adversarial cases EN+ZH) driven through
  `harness:lab_ask`: teacher-surface attacks must produce the typed
  TEACHING_REFUSAL; leak attempts must never expose fixture solution values.
  English execution verbs added to the D9 intent classifier ("execute step",
  "execute the lab", "run every step").
- **F — offline lab runner**: `lab --self-check` — offline-gate state,
  EPSG:4326 authority sanity, lab pack verification, and parse-probing of
  every rules file THROUGH the real grading seam; typed
  `sicnu.lab.self-check/1`, deterministic bytes, honest ok/degraded/failed.
- **G — content refresh / drift guards**: pack↔lab id coverage guard,
  committed-fixture checksum guard, `gen_lab_packs.py --check` zero-diff
  gate; the existing `test_labspec` operator-id guards unchanged and green
  (modulo two pre-existing failures documented below).
- **H — classroom scale**: `test_lab_scale` — 100-submission deterministic
  gate (byte-identical rerun), 25 real-grader submissions, cancel preserves
  the CSV prefix, and a 1000-submission opt-in evidence run
  (`SICNU_LAB_SCALE_1000=1`) detecting 19/19 planted duplicates.

## Architecture decisions (DECISIONS.md D1–D17)

Kernels live in NEW `src/agent/lab_grader_kernels.*` with a single dispatch
site in `output_verifier.cpp` (no second grading engine); batch extends D7's
`lab_batch_runner` in place; the report CLI is a shell over the D5 builder
(recorded grade built FROM the transcript digest — reports stay projections
of recorded truth, never recomputations); packs are the deployment authority
alongside the untouched data-spec/rule authorities.

## Compatibility

- All new CLI flags are additive; existing `--grade/--batch/--csv` behavior
  and exit contracts unchanged; grading CSV schema unchanged.
- Rules schema accepts `artifact.kind: "file"` additively; raster rules
  behave identically (kernel walks are additive/no-op for old kinds; the
  corpus still demands exact 100s for references).
- No changes to `src/app/**`, D18 LabSpec UI, D19 foundry/benchmark, or any
  open-PR file territory.

## ⚠️ Two one-line master build-unblock fixes (out of scope, required)

`origin/master@a5b11b7f` does not compile on Windows/MSVC; the sibling
execution-runtime track already carries both fixes locally (their commit
23b326a4 marks them "master build-unblock … P0, out of scope"):
1. `src/workflow/pipeline_run_coordinator.cpp` — `fsyncFile()` uses
   `_O_WRONLY/_O_BINARY` but the Q_OS_WIN branch omits `<fcntl.h>`.
2. `src/agent/data_platform_tools.cpp` — uses `sicnu::experiment::
   BenchmarkService` (D19) without `using namespace sicnu::experiment;`.
Both are recorded in EVIDENCE.md; drop them if the other track lands first.

## Local tests (twice-verified)

Full targeted matrix run twice back-to-back after the final rebase, all
green both times (EVIDENCE.md):

```
ctest-equivalent (direct catch2 runs, -j1, offscreen):
test_lab_data_pack 164 · test_lab_grader_kernels 131 · test_lab_grading 542
test_lab_batch 49 · test_lab_batch_v2 95 · test_lab_report 339
test_lab_report_cli 62 · test_harness_lab_injection 93
test_lab_self_check 95 · test_lab_offline_e2e 78 · test_lab_scale 1665
test_harness_lab_evals 561   (all assertions, all cases)
```
Opt-in scale evidence: 1000-submission run green (9819 assertions, 19/19
duplicates detected). Hygiene gates: `git diff --check` clean, no conflict
markers, secret scan clean, `gen_lab_packs.py --check` zero-diff.

## Review findings (REVIEW_LOG.md)

Independent adversarial review verdict **P0=0 P1=3**; all 13 findings fixed
and re-verified: stale corpus-pack checksum; series_separation slope
mis-scaled by band count (weighted pooled OLS + regression test);
spectral_signature reference-length/index OOB; typed rules-param validation
(type confusion could crash the CLI); isArray enforcement; pack loader
never-throws contract; classifier signal precision; doc/flag drift;
file_check path containment; Windows case-insensitive batch exclusions;
self-check evidence keys; ENL/SAM closed-form test assertions.

## Known limitations / pre-existing (documented, not introduced here)

- `test_labspec` has 2 pre-existing failures on master@a5b11b7f: D16's
  `lab8_temporal_analysis.lab.json` id `temporal_phenology_timeline` fails
  D18's stricter `^lab[0-9]{2}_…` id check, and `gen_lab_docs.py` crashes
  with a TypeError on the same D16 file (prerequisites as strings vs dicts;
  the probe also hardcodes `python3`, absent on this host). Both files are
  untouched by this branch; renaming the id belongs to the D16/D18 owners.
- Derived-artifact grading (trend/phenology/anomaly as separate artifacts)
  follows DECISIONS D16: the four PRIMARY artifacts carry the canonical
  `<lab>.rules.json`; derived rules can follow as `<lab>_<artifact>.rules.json`.
- Fixtures for the new labs regenerate via the C++ tool (this host has no
  GDAL python bindings); the python generators remain for hosts that do.

## Follow-ups

- Wire `lab --self-check` into `packaging/bundle/VERIFY.cmd` (bundle-side).
- Add `<lab>_<artifact>.rules.json` files for trend/phenology/anomaly.
- Consider teaching `test_labspec`'s docs probe the python3/python fallback
  used by this track's pack drift test (fix belongs with the pre-existing
  D16 file fix).
