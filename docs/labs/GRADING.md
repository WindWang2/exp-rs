# Lab Auto-Grading — authoring guide (D4, ADR 0150)

The platform's known-answer tradition (`docs/verification/KNOWN_ANSWER_MATRIX.md`)
proves the platform; lab grading spends it in the classroom. A teacher authors
one rules file per lab; students submit artifacts; the grader returns an
explainable 100-point transcript instead of a binary verdict.

```
sicnu_geo_rs_cli lab --lab <id|.rules.json> --grade <artifact> [--out report.json] [--max-bytes <n>]
sicnu_geo_rs_cli lab --lab <id> --batch <dir> [--csv g.csv] [--roster r.csv] [--json s.json] [--html s.html] [--max-submissions <n>]
sicnu_geo_rs_cli lab --report --experiment-db <db> --experiment <id> --report-out <base> [--grade transcript.json]
sicnu_geo_rs_cli lab --self-check [--pack-root <root>] [--lab-id <id>]
```

Exit codes: **0** pass · **1** fail · **2** usage (bad flags, unknown lab,
invalid rules, missing artifact path) · **3** unverifiable (the file exists but
cannot be graded as a raster). Wrong CRS / wrong band count / wrong values are
*graded* blocking failures (exit 1) — a wrong answer must lower the score, not
excuse the submission.

## Rules file — `data/labs/grading/<lab_id>.rules.json`

Schema: `sicnu.lab.rules/1` (authoring contract: `lab_rules.schema.json` next to
the rules). The grader validates in code; violations are usage errors.

```json
{
  "schema_version": "sicnu.lab.rules/1",
  "lab_id": "ndvi_basics",
  "title": "…",
  "artifact": { "kind": "raster" },
  "passing_score": 60,
  "assertions": [
    {
      "id": "range.ndvi",
      "kind": "range",
      "weight": 15,
      "severity": "normal",
      "params": { "band": 1, "min": -1.0, "max": 1.0, "max_violation_ratio": 0.0 },
      "derivation": "why this threshold is right (closed form or documented bound)"
    }
  ]
}
```

Authoring rules the grader enforces:

- assertion `id`s unique; `weight` > 0; **weights sum to exactly 100** (never
  silently normalized — explainability first);
- `severity` ∈ `normal` | `blocking`; a **blocking** failure caps the score at
  `passing_score − 1` and forces `verdict: fail`;
- tolerances are per-assertion and explicit; prefer closed-form known answers
  (NDVI bounds, atan(2), the 300 K Planck round-trip) and write the derivation
  into the `derivation` field;
- score `= 100 − Σ weight(failed assertions)`, floored at 0; every deduction
  carries `{assertion_id, observed, expected, delta, message}` — a score
  without evidence is a defect.

## Assertion kernels

Value kernels run in ONE windowed streaming pass under `--max-bytes` (default
64 MiB; the #808 `RasterReader::readWindow` budget contract). A kernel never
materializes the whole raster.

| kind | params (required **bold**) | fails when |
|---|---|---|
| `range` | **band** (or **index**:`"ndvi"`+**bands**), **min**, **max**, max_violation_ratio | > ratio of valid pixels outside `[min,max]` |
| `mean_sigma` | **band** or **index**+**bands**; mean+mean_tolerance; sigma_min/sigma_max; sigma_tolerance | declared bounds violated (population σ) |
| `gain_invariance` | **bands**:[nir,red], **tolerance**, max_violation_ratio, expected_mean+mean_tolerance | per-pixel \|idx(stored) − idx(metadata-calibrated)\| > tol, or index mean off — pure gains cancel in ratio indices, per-band gain mismatches and declared offsets do not |
| `nodata_ratio` | min_ratio, **max_ratio**, bands? | ratio outside `[min,max]` — catches "forgot to mask" AND "masked everything" |
| `histogram_shape` | **band**, **bins**, **min**, **max**, **shape** (`bimodal` \| `monotone_increasing` \| `monotone_decreasing`), min_mode_fraction, min_separation | shape mismatch (right-open bins, last bin closed; out-of-range values counted separately) |
| `classification_kappa` | **band**, **truth** (`{path}` \| `{inline}`), **labels**, **kappa_min**, oa_min, nodata_class | OA/kappa below floor (truth-NoData pixels excluded; unlabelled predictions counted and penalized) |
| `confusion_marginals` | same + **expected_proportions**, **tolerance** | predicted-side class proportions deviate beyond tolerance (systematic over/under-mapping) |
| `change_area_interval` | **band**, **value**, **min_px**, **max_px** | changed-pixel count outside the known interval (inclusive threshold, closed-form area) |
| `crs_grid` | **epsg**, **width**, **height**, **pixel_size_x/y**, band_count?, pixel_size_tolerance? | CRS/grid/band-count incompatibility — usually `blocking` |

### Grader 2.0 kernels (11.0)

The kernels below run their OWN windowed streaming pass (per-assertion byte
budget) and may open aux rasters (zones / truth) resolved relative to the
rules file — the same policy as the classification truth walk.

| kind | params (required **bold**) | fails when |
|---|---|---|
| `zone_stats` | **band**, **zones**:{path}, **stat** (`mean`\|`enl`\|`fisher`); mean: **bounds**:[{zone, band?, min?, max?}], zone_deltas?:[{zone, below_zone, min_delta}]; enl: **reference**:{path}, **enl_min_ratio**, max_relative_mean_shift?; fisher: **compare_zone**, **min_fisher**, zones_nodata? | any declared (band, zone) mean outside its bounds, a delta below its floor, the ENL ratio under the floor, or Fisher separability under the floor |
| `band_layout` | band_count? \| **min_valid_fraction**? \| expected_valid_pixels?:{band:n}; bands? | band count, per-band valid fraction or exact valid-pixel count violated (stack integrity, NoData holes) |
| `spatial_agreement` | **band**, **truth**:{path}, truth_nodata?, and EXACTLY ONE mode: zone_expected:[{zone,label}]+**min_accuracy** \| truth_positive+artifact_positive+**min_hit_rate**(+max_false_alarm_rate) \| **tolerance**+**max_exceed_fraction** | POSITION-SENSITIVE: per-zone label accuracy, binary hit / false-alarm rates, or the fraction of |artifact−truth|>tolerance pixels outside its bound — right-area-wrong-place finally scores below the pass line |
| `series_separation` | **zones**:{path}, **x**:[≥2 axis values], bounds?/separations?:[{zone, below_zone, min_delta}] | per-zone least-squares slope over the declared axis outside bounds, or slope separation below the floor |
| `spectral_signature` | **bands**, **references**:[{name?, spectrum}], zones?:{path}, zone_reference?, sam_max_mean_degrees?/sam_max_degrees? | mean/max SAM spectral angle of pixel spectra vs the declared reference exceeds the bound |
| `file_check` (artifact `kind: "file"`) | exists? \| min_bytes?/max_bytes? \| png:{page_width_mm, page_height_mm, dpi, size_tolerance} \| mapspec:{max_problems} \| preflight:{max_problems, forbidden_codes?}; path? (sibling file) | the submitted FILE is absent, mis-sized, the PNG page geometry (parsed from IHDR vs the declared dpi) does not match, or the MapSpec fails the platform's validateMapSpec / preflight catalog |

## Report and determinism

The transcript is `{schema: "sicnu.lab.grade/1", digest, generated_utc, report}`.
`generated_utc` is header-only metadata; the **body** (`report`) and its sha256
`digest` are byte-identical for identical inputs — evidence doubles are rounded
to 12 significant digits and no wall-clock value enters the body. `--out`
writes the same document to a file.

## Fixtures and corpora (tests/fixtures/lab/)

`generate_fixtures.py` (python3-osgeo) regenerates the committed GeoTIFFs
deterministically — closed-form scenes, no RNG, no timestamps.
`reference_corpus.json` and `wrong_answer_corpus.json` declare the expected
scores: references score 100, every deliberately wrong product declares a score
band strictly below the 60-point pass line plus the assertion ids that must
fire. `ctest -R lab_grading` asserts the whole corpus contract.

## Known limits (by design)

- **Position sensitivity is opt-in per kernel**: the classic value kernels
  grade the distribution of the answer (ranges, moments, histogram shape,
  calibration identity). Since 11.0, `spatial_agreement` grades placement
  against a truth raster (hit/false-alarm, per-zone accuracy, continuous
  |diff|), and `zone_stats`/`series_separation` grade zone-joined behaviour.
  Full per-pixel diff grading stays rejected (ADR 0150: brittle).
- **Unlabelled truth+prediction pairs** (both outside the legend) count as
  agreement in OA and as their own marginal column; exclude such values via
  `nodata_class` / declared NoData instead of relying on that column.
- **NoData matching** compares stored values against the declared sentinel at
  storage precision; declare sentinels that are representable in the band
  dtype (−9999, 0, 255 all are).

## Batch classroom 2.0 (`--batch`)

`lab --batch <dir>` still streams one submission at a time (memory bounded by
the largest artifact, CSV flushed per row). Since 11.0 it also accepts:

- `--roster <csv>` — `student_id,display_name` (UTF-8, BOM tolerated, `#`
  comments). Submitters absent from the roster are flagged `unknown`;
  roster students without a submission are listed as missing. No score is
  ever invented or withheld for roster mismatches — the teacher judges.
- identity — every submission is hashed (streaming sha256); identical content
  from two files is graded per file but flagged `duplicate_of` in the
  summaries. The machine never enforces a plagiarism policy.
- `--max-submissions <n>` — caps runaway input; the CSV keeps every row
  written. Exit code 1 signals isolated/cancelled/capped runs.
- `--json <s.json>` / `--html <s.html>` — deterministic summaries
  (`sicnu.lab.batch-summary/1`; rows sorted by student_id; NO wall-clock
  values, so identical submissions regrade to byte-identical summaries).
  Both are written atomically (tmp + rename).

## Headless reproducibility report (`--report`)

`lab --report --experiment-db <db> --experiment <id> --report-out <base>`
exports the D5 `sicnu.labreport.v1` projection (runs, operation trail,
lineage slice, replay readiness, redacted environment) as
`<base>.json/.md/.html` — the exact documents the GUI produces, from the same
builder. With `--grade transcript.json` (a `lab --grade --out` document) the
report embeds the RECORDED grade: `gradingRef` is the transcript's digest,
so the inline copy is verifiable against the original, never orphaned.

## Environment self-check (`--self-check`)

`lab --self-check` prints a deterministic `sicnu.lab.self-check/1` diagnostic:
offline-gate state (and what a remote open would return), EPSG:4326 authority
sanity, lab data pack verification (see `docs/labs/DATA_PACKS.md`), and a
parse check of every grading rules file THROUGH the real grading seam. Exit 0
only when nothing failed; degraded regenerable inputs are reported honestly.

## Extending

- New lab: copy a rules file, keep the weight-sum and derivation discipline,
  regenerate fixtures if the scene changes, extend the corpus manifests.
- D7 `--batch` extension point: batch grading / CSV export wraps
  `OutputVerifier::gradeArtifact()` and serializes `LabGradeResult` — do not
  grow side entry points into `cli_lab_commands.cpp`.
- Deployment: rules resolve from `--lab` path > `LabGradeOptions::rulesDir` >
  `SICNU_LAB_RULES_DIR` > source-tree `data/labs/grading`.
