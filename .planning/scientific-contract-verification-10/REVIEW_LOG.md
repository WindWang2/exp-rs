# REVIEW_LOG — scientific-contract-verification-10

## Round 0 — incoming findings (whole-repo-line-review)

| Finding | Sev | Disposition | Where |
|---|---|---|---|
| F-OPS-4 io:reproject srcCrsOverride dead | P1 | **fixed** — WarpOptions::sourceCrsOverride → `-s_srs`; regression test pins real transform + output CRS (OSRIsSame) | 7e117d29f7 |
| F-OPS-1 class_mapping vs encoding | P2 | **fixed** — engine encodes by 1+max(classMapping) (Byte→UInt16), catalog refuses >65534; escalation + refusal tests | 4e7b6a55f9 |
| F-OPS-3 qa_mask fail-open | P2 | **fixed** — fail-closed unknown flags OR-ed into mask; SCL "all" masks class 0; unreadableSamples result field; #699 tests updated | d3418d7fce |
| F-OPS-5 NMS O(n²) uncancellable | P2 | **fixed** — exact-result grid bucketing + CancelProbe; differential test vs dense reference; 100k-budget lane | 4a914d4083 |
| F-OPS-2 TensorBlob ND non-contiguous | P3 | **fixed** — clone() fallback; byte-equality ROI test | 9ede2eec66 |
| F-PI-1 pi desync zombie | P2 | **fixed** — failDesyncedStream kills child (+SIGKILL escalation per round 2); respawn framing reset + generation-aware stdout (replay bug found while testing) | 297d5b0c36 |
| F-PI-2 startup-deadline drift | P2 | **fixed** — try/finally backport + structural parity test | 297d5b0c36 |

## Round 1 — independent review (2 read-only subagents, adversarial)

Reviewer A (architecture/science) + Reviewer B (concurrency/test-trust). Dispositions:

| ID | Sev | Finding | Disposition |
|---|---|---|---|
| R-C1 (A) | P0 | test_capability_knowledge hardcodes 111 vs live 114 — CI red | **FIXED**: monotone floor ≥114 + registry==catalog equality |
| R-B1 (A) | P1 | classificationFamily encoding/nodata contradicts pipeline (escalating, NoData=0) | **FIXED**: `escalating_nodata_0` vocabulary value + per-operator honesty (sam -9999, change-map UInt16/65535, recode note) |
| R-B2 (A) | P1 | matched_filter/ace are continuous Float32 scores, not classes | **FIXED**: probability + none |
| R-B3 (A) | P1 | connected_components is Float32/NaN, ids exact only ≤2²⁴ | **FIXED**: `float32_nodata_nan`, range 1..16777216 + note |
| R-B4 (A) | P1 | rs:segment pins byte/254, contradicting this branch's own F-OPS-1 escalation | **FIXED**: `byte_uint16_escalating` (infer + segment), range 0..65534 |
| R-B5 (A) | P2 | zonal/segment-stats/extract-series write CSVs, not json-only | **FIXED**: direct_write + writer-review evidence |
| R-B6 (A) | P2 | rasterize is an attribute grid, not mask | **FIXED**: any + note |
| R-B7 (A) | P2 | atmospheric input is DN for dn_to_radiance | **FIXED**: inputDomain dn + method note |
| R-B8 (A) | P2 | sar_calibrate note overstates param domain | **FIXED**: linear_power\|db note |
| R-B9 (A) | P2 | qa_mask refusalCodes missing FileNotFound | **FIXED** (+ taxonomy test stays green) |
| R-A1 (A) | P2 | SCL out-of-domain words (16..65535) truncate → fail-open corner | **FIXED**: >15 SCL words flagged unknown; >65535 clamp flags unknown |
| R-C2 (A) | P3 | drift gate skips stamp-less operators | **FIXED**: gate compares the determinismGrade() virtual (binds every operator) |
| R-A2 (A) | P3 | fuzz misses the newly plumbed srcCrsOverride; stale kmeans comment | **FIXED**: fuzz drives io:reproject srcCrsOverride too; comment corrected |
| R-B10 (A) | P3 | mosaic timeAlignment single_scene untruthful (array input) | **FIXED**: not_applicable |
| R-B1 (B) | P2 | parity kill-regex mutation-blind (matches stop()'s kill) | **FIXED**: body-scoped extraction + SIGKILL assertion |
| R-B2 (B) | P2 | io test CRS-tag check inspects GDAL constant, not the output | **FIXED**: OSRImportFromWkt(output) + OSRIsSame(EPSG:32633) |
| R-B3 (B) | P3 | fuzz publish branch lacks CRS check; NDVI passes constant operator | **FIXED**: published outputs must open + carry projection; NDVI ≈ 1/3 pinned |
| R-B4 (B) | P3 | desync kill lacks SIGKILL escalation (SIGTERM-stall zombie variant) | **FIXED**: 5s escalation ladder in both bridges |
| R-B5 (B) | P3 | spatial exit handler generation-blind (latent spawn-over-live) | **FIXED**: exit handlers generation-aware in both files |
| R-B6 (B) | P3 | flood test fixed 100ms sleep | **FIXED**: bounded poll |
| R-B7 (B) | P3 | cellOf UB beyond int64 range; one-giant-box degradation undocumented | **FIXED**: ±1e15 park guard + honest comment |

Accepted debt: none for P0/P1. P2/P3 all fixed except: none — every finding has a disposition with a code change.
Reviewer-verified clean lenses (no action): NMS exactness proof, -s_srs GDAL usage, F-OPS-1 boundaries, qa_mask per-dtype fail-closed paths, CancelProbe exception safety, WarpOptions API/ABI (no aggregate initializers), buffer-reset generation semantics.

## Round 2 — re-review of fixes

Pending (re-run suites + spot-verify each fix at final HEAD; results in EVIDENCE.md).
