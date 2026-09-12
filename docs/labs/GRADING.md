# Lab Auto-Grading — authoring guide (D4, ADR 0146)

The platform's known-answer tradition (`docs/verification/KNOWN_ANSWER_MATRIX.md`)
proves the platform; lab grading spends it in the classroom. A teacher authors
one rules file per lab; students submit artifacts; the grader returns an
explainable 100-point transcript instead of a binary verdict.

```
sicnu_geo_rs_cli lab --lab <id|.rules.json> --grade <artifact> [--out report.json] [--max-bytes <n>]
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

## Extending

- New lab: copy a rules file, keep the weight-sum and derivation discipline,
  regenerate fixtures if the scene changes, extend the corpus manifests.
- D7 `--batch` extension point: batch grading / CSV export wraps
  `OutputVerifier::gradeArtifact()` and serializes `LabGradeResult` — do not
  grow side entry points into `cli_lab_commands.cpp`.
- Deployment: rules resolve from `--lab` path > `LabGradeOptions::rulesDir` >
  `SICNU_LAB_RULES_DIR` > source-tree `data/labs/grading`.
