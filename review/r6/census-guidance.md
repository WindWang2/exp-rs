# WP-D — Guidance sidecar census (`data/explain/guidance/*.json`, 16 files), @ 1e28de867

## 1. Canonical keyset — derived from the loader (normative) + schema (documentation) + README

Loader: `src/explain/guidance_store.cpp` (schema constant `exp.step_guidance.v1` at :19).
Schema doc: `data/schemas/exp_step_guidance.schema.json` (`additionalProperties: false`,
`required: ["schema","operatorId","purpose"]`). README: `data/explain/guidance/README.md`.

Loader code proving requiredness:

- **Closed keyset (unknown keys refuse the file)** — `readAllowedKeys` called on the root at
  `guidance_store.cpp:226-231`:
  ```cpp
  if ( !readAllowedKeys( root,
                         { "schema", "operatorId", "role", "purpose", "whenToUse",
                           "prerequisitesNote", "assumptions", "parameterRationale",
                           "stateNarrative", "skipConsequence", "references", "teachingNote" },
                         error ) )
  ```
  An unknown key produces problem code `unknown_key` (:378) and the file is skipped (fail-closed,
  :375-385).
- **Required**: `schema` (:233-240, must equal `exp.step_guidance.v1`), `operatorId` (:243-249,
  non-empty, no whitespace; the coverage oracle additionally requires it to resolve in the live
  `RSOperatorRegistry` — README "Live-schema coupling", enforced by
  `tests/test_explain_guidance_coverage`), `purpose` (:257-262, non-empty).
- **Optional scalar strings**: `role` (:250-256; "The committed corpus ships generic entries only" —
  README), `whenToUse` (:263-264), `teachingNote` (:319-320).
- **Optional string arrays** (absent = OK; present entries must be non-empty strings):
  `prerequisitesNote` (:265-266), `assumptions` (:267-268), `downstreamRoles` inside
  skipConsequence (:183).
- **Optional object `parameterRationale`** (:270-289, array, ≤64 entries `kMaxEntriesPerFile` :20);
  entries require non-empty `parameter` + `rationale`, optional `misconfigurationConsequence`
  (:112-136); `parameter` must exist in the operator's live schema (README coupling).
- **Optional object `stateNarrative`** (:290-296): requires `before` + `after`
  (:138-163), each either empty ("deliberately unspecified") or a token of the closed state
  vocabulary `DN|Radiance|TOA|BOA|Index|Mask|*|None` (`src/explain/state_vocabulary.h:23-31`;
  check at guidance_store.cpp:152-161). README: avoid `*` (builder compares by equality).
- **Optional object `skipConsequence`** (:297-303): requires non-empty `summary`, optional
  `detail`, optional `downstreamRoles` array (:165-184).
- **Optional array `references`** (:304-318): entries require non-empty `title`, `kind` ∈
  `{textbook, paper, standard, doc}` (`isKnownReferenceKind`, checked :204-208), `locator`;
  optional `note` (:186-217).
- **Bounds**: ≤64 rationale entries per file (:20, :277-281), ≤1024 total entries (:21, :352-357).
- **Duplicate (operatorId, role) → `duplicate_entry`, first wins** (:387-394).

### Key classification

| Key | Class | Proof |
|---|---|---|
| `schema` | required (const) | :233-240 |
| `operatorId` | required | :243-249 + coverage oracle |
| `purpose` | required (non-empty) | :257-262 |
| `role` | optional (corpus: absent everywhere) | :250-256 |
| `whenToUse` | optional | :263-264 |
| `teachingNote` | optional | :319-320 |
| `prerequisitesNote` | optional array | :265-266 |
| `assumptions` | optional array | :267-268 |
| `parameterRationale` | optional array of objects (parameter+rationale required inside) | :270-289 |
| `stateNarrative` | optional object (before+after required inside) | :290-296 |
| `skipConsequence` | optional object (summary required inside) | :297-303 |
| `references` | optional array of objects (title+kind+locator required inside) | :304-318 |
| anything else | forbidden (`unknown_key` → file skipped) | :97-110, :226-231, :378 |

There are **no deprecated keys** and **no loader-known keys beyond this list** — the 12-key set above
is complete as of this commit.

## 2. Per-file key census (16 files)

All 16 parse, resolve, and share the required trio. "Full" = every optional key present (the
10-file complete pattern). `whenToUse`, `parameterRationale`, `skipConsequence`, `teachingNote`
are present in ALL 16 files; the gaps are only in `prerequisitesNote` / `assumptions` /
`stateNarrative` / `references`.

| File | operatorId | Complete? | Missing optional keys |
|---|---|---|---|
| rs_apply_mask.json | `rs:apply_mask` | no | `assumptions`, `references` |
| rs_atmospheric_correction.json | `rs:atmospheric_correction` | YES | — |
| rs_band_math.json | `rs:band_math` | no | `prerequisitesNote`, `stateNarrative`, `references` |
| rs_change_detection.json | `rs:change_detection` | YES | — |
| rs_continuum_removal.json | `rs:continuum_removal` | YES | — |
| rs_extract_bands.json | `rs:extract_bands` | no | `assumptions`, `stateNarrative`, `references` |
| rs_image_fusion.json | `rs:image_fusion` | no | `stateNarrative`, `references` |
| rs_ndvi.json | `rs:ndvi` | YES | — |
| rs_qa_mask.json | `rs:qa_mask` | no | `assumptions` |
| rs_radiometric_calibration.json | `rs:radiometric_calibration` | YES | — |
| rs_sam_classify.json | `rs:sam_classify` | YES | — |
| rs_sar_calibrate.json | `rs:sar_calibrate` | YES | — |
| rs_sar_speckle.json | `rs:sar_speckle` | YES | — |
| rs_spectral_index.json | `rs:spectral_index` | YES | — |
| rs_spectral_resample.json | `rs:spectral_resample` | no | `stateNarrative`, `references` |
| rs_threshold_raster.json | `rs:threshold_raster` | YES | — |

Tally: 10 complete, 6 with gaps, 13 missing (file, key) pairs:
`assumptions` ×3 (apply_mask, extract_bands, qa_mask), `references` ×5 (apply_mask, band_math,
extract_bands, image_fusion, spectral_resample), `stateNarrative` ×4 (band_math, extract_bands,
image_fusion, spectral_resample), `prerequisitesNote` ×1 (band_math). Exact per-file lists are in
the table above.

## 3. Authored-content vs structural

- **All gaps are authored-content gaps, not structural.** Every missing key is loader-OPTIONAL
  (§1), so all 16 files load and serve guidance; no `unknown_key`/`missing_field` problems exist
  (verified by the census parse: 16/16 valid). Nothing here is a schema violation.
- The 10 complete files establish the *de-facto* complete-entry shape — but per instructions the
  canonical expectation comes from the loader/docs, which make completeness a quality bar, not a
  contract. The 6 incomplete files are earlier/leaner authoring (apply_mask, qa_mask, extract_bands,
  band_math, image_fusion, spectral_resample — plausibly the first-authored operators; band_math and
  extract_bands also predate the stateNarrative convention).
- Authoring a missing key is zero-risk to the loader; the only hard constraints when filling them:
  `stateNarrative` tokens must be in the closed vocabulary and should avoid `*`;
  `references[].kind` must be one of the four kinds; `assumptions`/`prerequisitesNote` entries must
  be non-empty strings.
- Coverage oracle (`tests/test_explain_guidance_coverage`) pins operatorId→registry and
  parameterRationale→live-schema coupling only; **no test enforces optional-key completeness**, so
  nothing fails CI today for these gaps (that is why they persist).

## 4. Gaps summary for the fix PR

Recommended completion set (matching the 10-file complete shape):

| File | Add | Notes for the author |
|---|---|---|
| rs_apply_mask.json | `assumptions`, `references` | mask-source assumptions (QA band declared vs derived); reference the QA-band doc |
| rs_band_math.json | `prerequisitesNote`, `stateNarrative`, `references` | expression-injection bounds, band-count/radiometric-state expectations |
| rs_extract_bands.json | `assumptions`, `stateNarrative`, `references` | band-role availability assumptions; DN→Index narrative |
| rs_image_fusion.json | `stateNarrative`, `references` | Radiance-state expectation before fusion (fusion of DN is meaningless); pansharpening references |
| rs_qa_mask.json | `assumptions` | QA bit-decoding assumptions per sensor |
| rs_spectral_resample.json | `stateNarrative`, `references` | reflectance-domain resampling assumption; spectral response references |

(Descriptions here are hints from each operator's registered `metadata()`/schema prose, not authored
copy — the PR author writes the actual sentences.)

## 5. Related infrastructure (context; do not duplicate)

- Loader fail-closed behavior + problem codes: `guidance_store.cpp:327-400`
  (`parse_failed | unknown_key | schema_version_unsupported | missing_field | invalid_value |
  entry_limit_exceeded | duplicate_entry | file_unreadable | directory_missing`).
- Builder/validator separation of authored guidance vs runtime facts:
  `src/explain/authored_guidance.h:4-11` ("always presented as authored_guidance provenance — never
  as a runtime fact"), `src/explain/explanation_validator.cpp`,
  `src/explain/explanation_builder.h`.
- `src/app/help/workbench_guidance.cpp` surfaces the same store in the workbench help.
