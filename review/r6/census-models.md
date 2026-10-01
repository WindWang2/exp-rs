# WP-E — Model manifest census (`models/*/model.json`, 26 manifests vs `scripts/gen_model_library.py`), @ 1e28de867

## 1. Method

- Read `scripts/gen_model_library.py` (391 lines): `main()` (:375-387) writes each `MODELS[]` entry
  to `<script_dir>/../models/<name>/model.json` — the output root is hard-coded relative to the
  script with NO CLI flag. To avoid touching the worktree, the script was copied to
  `/tmp/r6-modelgen/scripts/` and run there, so it wrote to `/tmp/r6-modelgen/models/` (24
  manifests). Disk `models/` was then compared with `cmp` / `diff`. Nothing was written in-repo.
- Generator emits 24 manifests (`wrote 24 manifests`); disk has 26 model dirs (25 + `README.md`).

## 2. Full manifest table (26 rows)

| Manifest | Generator-produced? | disk == generator? | Diff summary |
|---|---|---|---|
| dlinknet-roads-hr | yes | SAME | — |
| sam-building | **NO — disk-only** | n/a | v1-era template manifest (see §3.1) |
| sam-buildings-hr | yes | SAME | — |
| segformer-landcover-s2 | yes | SAME | — |
| segformer-roads | yes | SAME | — |
| siamese-change-buildings | yes | SAME | — |
| siamese-change-deforestation | yes | SAME | — |
| ssl-embedding-encoder | yes | **DIFF** | disk adds `"id": "rs/ssl-embedding-encoder"`, `"model_version": "1.0.0"`, `"license": "unspecified"` (model.json:79-81) |
| swin-landcover-hr | yes | **DIFF** | disk `preprocess.mean`/`std` have 4 values (matching its 4-band `red,green,blue,nir` input); generator emits 3 values against the same 4-band input |
| temporal-siamese-crop-change | yes | SAME | — |
| temporal-transformer-crop | yes | SAME | — |
| unet-buildings-s2 | yes | **DIFF** | disk adds `"id": "rs/unet-buildings-s2"`, `"model_version": "1.0.0"`, `"license": "unspecified"`, `"source": …/unet-buildings-s2` |
| unet-cloud-s2 | yes | SAME | — |
| unet-crop-parcel | yes | SAME | — |
| unet-deeplabv3-landcover | yes | SAME | — |
| unet-forest-s1s2 | yes | SAME | — |
| unet-generic-binary | yes | SAME | — |
| unet-roads-s2 | yes | SAME | — |
| unet-sar-flood-s1 | yes | SAME | — |
| unet-sar-ship-s1 | yes | SAME | — |
| unet-water-s2 | yes | SAME | — |
| unet-water-sar | yes | SAME | — |
| yolo-airplane-detection | yes | SAME | — |
| yolo-building-detection | yes | SAME | — |
| yolo-buildings | **NO — disk-only** | n/a | v1-era template manifest (see §3.2) |
| yolo-ship-detection | yes | **DIFF** | disk adds `input.width/height: 640`, `preprocess.resize: "to_input"`, `output.detection` decode block (`xywh_objectness`, conf 0.25, nms 0.45, classes ["ship"]), plus `id/model_version/license/source` |

Tally: 20 byte-identical; 4 disk-newer (ssl-embedding-encoder, swin-landcover-hr,
unet-buildings-s2, yolo-ship-detection); 2 disk-only v1 templates (sam-building, yolo-buildings);
0 generator-only. **Direction of every diff: DISK is newer than SPEC** — a regenerate would
REGRESS all 4 deviating manifests. (See §5: this contradicts the round baseline, which flagged only
swin + yolo-ship.)

## 3. The four specials in detail

### 3.1 `sam-building` (disk-only, v1 shape)

- Shape: `output: "polygon"` as a STRING, `input` without `inputs[]`, no `artifact`/`preprocess`/
  `tiling`/`runtime`-extension blocks — the documented "v1 (backward compatible)" shape
  (`models/README.md` "Manifest schema §v1"; parser infers manifest_version 1).
- Runtime references: NONE hard-wired. `src/operators/framework/model_catalog.h:486` uses
  `"sam-building"` only as a doc-comment example ("Unique id, e.g. \"sam-building\"").
- Tests: `tests/test_model_catalog_v2.cpp:58-72` and `tests/test_spatial_tools.cpp:239-273` WRITE
  THEIR OWN `sam-building/model.json` into temp dirs — they do not read the committed manifest.
  Deleting the committed dir breaks no test.
- Superseded by `sam-buildings-hr` (v2 shape: artifact block, tiling, preprocess, `output.type`).
- **Delete** = safe (doc-comment example and README v1 example become stale prose only).
  **Alias** = no alias mechanism exists in the catalog (lookup is by exact `name`);
  aliasing would mean a note in README. **Regenerate** = impossible (not in `MODELS[]`).

### 3.2 `yolo-buildings` (disk-only, v1 shape)

- Same v1 shape; `output: "vector"`, gpu:false. No src/ or test references found at all.
- Superseded by `yolo-building-detection` (v2). **Delete** = safe; nothing else to update except
  history.

### 3.3 `swin-landcover-hr` (disk newer: 4-band normalization)

- Disk: `input.band_roles = [red, green, blue, nir]`, `mean = [0.485, 0.456, 0.406, 0.552]`,
  `std = [0.229, 0.224, 0.225, 0.231]`. Generator SPEC emits only 3 mean/std values for the SAME
  4-band input — the generator's own output is internally inconsistent (per-channel normalization
  with a missing 4th channel).
- Runtime: `parsePreprocessContract` reads `mean`/`std` via `parseDoubleArray`
  (`src/operators/framework/model_catalog.cpp:94-95`) — length is array-agnostic, and the provider
  normalizes per channel against the 4-band tensor, so disk is correct and the SPEC is the stale
  side.
- References: no tests or src code pin swin by id. **Regenerate** would corrupt normalization
  (3 values vs 4 channels) — do not. **Fix direction: patch the generator SPEC** (add the 4th
  values) if byte-parity is wanted.

### 3.4 `yolo-ship-detection` (disk newer: resize + detection decode contract)

- Disk extras, all consumed by the runtime parser:
  - `input.width/height: 640` — parsed as part of the input contract (v3 input keys).
  - `preprocess.resize: "to_input"` — `model_catalog.cpp:98` (`pre.resize`).
  - `output.detection { layout: xywh_objectness, tensor_layout: auto, conf_threshold: 0.25,
    nms_iou: 0.45, max_detections: 100000, classes: ["ship"] }` — parsed at
    `model_catalog.cpp:279-299` (`output.detectionDeclared`, decode contract with typed defaults).
  - `id: rs/yolo-ship-detection`, `model_version`, `license`, `source` — parsed at
    `model_catalog.cpp:211-213` into `ModelInfo` identity (surfaced by `spatial:list_models`).
- Documented as the reference example: `docs/models/model-manifest.md:126-131` shows exactly these
  keys; `docs/adr/0130-model-runtime-platform-4.md:83` cites "the shipped `yolo-ship-detection`
  declared vector contract".
- **Regenerate** would strip the resize/detection contract → detection decoding falls back to
  defaults and the documented shipped contract disappears. **Fix direction: port the extras into
  the generator SPEC** (or accept the generator only covers the "template library" subset and
  document that).

### 3.5 The two metadata-key diffs (ssl-embedding-encoder, unet-buildings-s2)

- Same class as yolo-ship's identity keys: disk carries `id`/`model_version`/`license`(+
  `source`) that the generator never emits; all four keys are read by the parser
  (`model_catalog.cpp:211-213`, known-key list :643-644). Regenerating silently blanks
  `ModelInfo::id/modelVersion/license` for these models.
- Not flagged by the round baseline. **Fix direction: either add these keys to the generator SPEC
  for all 24 (consistent identity metadata) or strip them from disk (LOSE data — not
  recommended).**

## 4. Runtime / test consumption map (what a delete/regenerate would break)

| Consumer | Site | Sensitivity |
|---|---|---|
| `ModelCatalog` loader | `src/operators/framework/model_catalog.{h,cpp}`; readiness states (`ready / missing_artifact / invalid_manifest / checksum_mismatch`) per `models/README.md` | reads every `models/*/model.json` from `$SICNU_MODELS_DIR` or `<cwd>/models` (README:207). All 26 parse; all are `missing_artifact` templates (weights never committed) |
| P1-8 manifest error contract (PR #1353) | `model_catalog.cpp:205,420,522,540,585,2019,2852` — path-prefixed typed findings, manifest_version 1..6 order contract | shape-agnostic; v1 manifests (sam-building, yolo-buildings) parse under the backward-compat path |
| Inference path | `rs:infer` (`rs_inference_operator.cpp`) + `model_runtime`/providers (`src/operators/runtime/*`), ensemble (`model_ensemble.cpp`) | refuses non-ready models with readiness explanation; no model id is hard-wired in src |
| Agent surfaces | `spatial:list_models` / `spatial:select_model` (`model_catalog_tool.cpp`), `model_catalog_tool` ranking | id-driven at runtime; the tool description says manifests live at `models/*/model.json` |
| Manifest tests | `tests/test_model_catalog_v2.cpp`, `test_model_manifest7.cpp`, `test_model_failure_matrix.cpp`, `test_model_ensemble.cpp`, `test_provider_*.cpp` | all build SYNTHETIC manifests in temp dirs; none read committed `models/` (verified for sam-building; the rest reference no committed ids — grep over tests/ for the 26 names returns nothing outside the two temp-dir writers) |
| Docs | `models/README.md` (v1 example), `docs/models/model-manifest.md` (yolo-ship example), ADR 0130 | prose only |

**Delete/alias/regenerate impact matrix**

| Action | sam-building | yolo-buildings | swin-landcover-hr | yolo-ship-detection | ssl-embedding-encoder / unet-buildings-s2 |
|---|---|---|---|---|---|
| delete | safe (doc-comment + README v1 example go stale) | safe (zero refs) | loses the only correct 4-band normalization | breaks ADR 0130 claim + manifest-doc example; detection contract gone | loses identity metadata consumers may rank/display |
| regenerate (run generator in-repo) | no-op (not in SPEC — dir left orphaned) | no-op (orphaned) | **regression**: 3-value mean/std vs 4-band input | **regression**: resize+detection+identity keys stripped | **regression**: id/version/license keys stripped |
| update SPEC then regenerate | n/a | n/a | correct fix (add 4th values) | correct fix (port width/height/resize/detection/id keys) | correct fix (add identity keys) |

## 5. Contradictions vs the round baseline

1. Baseline: "`models/sam-building` + `models/yolo-buildings` … only 2 of 26 not generator-produced"
   — **confirmed** (24 dirs have generator entries; 2 disk-only).
2. Baseline: "swin-landcover-hr + yolo-ship-detection deviate … DISK newer than SPEC" — **confirmed
   but INCOMPLETE**: `ssl-embedding-encoder` and `unet-buildings-s2` also deviate (identity-key
   drift, same DISK>SPEC direction). Four DIFFs, not two.
3. Baseline implies a regenerate is viable — **a blind regenerate today regresses 4 manifests**;
   the SPEC must be updated first (§4 matrix).
