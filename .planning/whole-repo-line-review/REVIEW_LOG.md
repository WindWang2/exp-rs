# REVIEW_LOG — two-pass adjudication record

Format per entry: `[date] <finding-id> verdict: KEPT|RETRACTED reason: … reviewer: main|V|C`

## Round 1 (main agent, inline self-review while writing findings)
(entries appended as findings are written)

## Round 2 (Phase 7: subagent V false-positive sweep; subagent C coverage audit)
(pending)

## False-positive tally
- submitted: 0
- retracted: 0

## Round 1 — operators area method note (2026-09-13)
Deep line-by-line reads: framework/(12), runtime/(20 incl. tile_inference_engine 3304, model_runtime, providers, planner, blob), rs/: spectral_index, change_streaming (1355), raster_spatial_operators (776), sar_geocode (603), obia_classify core, qa_mask, mosaic (head+schema), io_operators, gdal warp utils + orthorectification (#694 verify), raster_spatial ops. Remaining operators files (schemas/metadata/init/declaration headers, small adapters): structural pass + lens-driven risk scan (memcpy/threads/divisions/at()/resource-close sites), flagged lines read with context. Recorded per DECISIONS D-004.

## Considered-and-dropped (operators)
- `ModelCatalog::resolve()` bypasses mUnregistered (model_catalog.cpp:2074-2083) while findLocked() honors it — contract inconsistency, but `unregister()` has zero production callers → unreachable state; dropped (P3-if-anything).
- `rs_feature_normalize_operator.cpp:314-317` `(v-lo)/span` with span==0 → NaN→int cast UB, but result std::clamp'd immediately; benign on all supported targets; dropped.
- `bit-exact` (schema root, stampDeterminismGrade) vs `bit_exact` (algorithm_descriptor) dual vocabulary — separate surfaces, no cross-feeding site; dropped.
- rs_change_streaming IR-MAD weights frame lacks the 2^31-px guard the mask path has — bad_alloc path surfaces as std::exception (logged+rethrown by execute()); behavior acceptable; dropped.
