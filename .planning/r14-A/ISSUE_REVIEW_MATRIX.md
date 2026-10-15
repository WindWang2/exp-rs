# R14-A Issue / PR review matrix

Legend: status ∈ OPEN / FIXED_ON_MASTER / FIXED_IN_OPEN_PR / STALE / UNRELATED / NEEDS_REPRO.
Every "fixed" claim below was verified by reading BASE_SHA source, not by PR description.

| Ref | Old root cause | Current code evidence (BASE_SHA) | Status | R14-A action |
|---|---|---|---|---|
| #1450 (radiometric vocabulary double-track) | SAR tokens (gamma0…) unknown to FSM → degraded to DN → double-calibration risk | src/core/radiometric_state.h `vocabulary::kTokens` covers all writer tokens; FSM has SAR edges; tests/test_radiometric_vocabulary_contract.cpp pins writers | FIXED_ON_MASTER | Run contract test green locally (baseline verification); no code change |
| #1467 (radiometric preflight opens provider URIs) | GDALOpenEx on layer->source() verbatim | radiometric_state.cpp routes through local_source::isPlainLocalPath | FIXED_ON_MASTER | none (read-only file) |
| #1467 (temporal_cube catch-all → NaN composites) | typed read failure swallowed | #1505 file list contains src/core/temporal_cube.cpp | FIXED_IN_OPEN_PR (#1505) | none — out of writable scope |
| #1501 (sar per-band NoData; band-select NoData propagation) | single-sentinel / missing propagation | rs_sar_calibrate_operator.cpp:326-331 per-band; rs_spectral_band_select_operator.cpp:250-258 propagation | FIXED_ON_MASTER | none — dedup honored |
| NEW (audit, no issue filed) rs_temporal_index DN probe folds undeclared negative sentinels | probe uses std::abs; single-scene twin fixed positive-only in #856 | rs_temporal_index_operator.cpp:291-302 vs rs_spectral_index_operator.cpp:407-416 | OPEN (found by R14-A audit) | WP-A1 fix + regression test |
| NEW (audit) local RX diagonal zero-variance aborts whole raster | var[b]+load==0 → Inf/NaN → NonFiniteScore → ComputationError; header says per-window degeneracy is not an error | spectral_local_rx.cpp:203-213, 326-333; full mode already returns SingularBackground | OPEN | WP-A2 fix + contract test |
| NEW (audit) sam_classify single first-band sentinel | heterogeneously declared NoData passes as data | rs_sam_classify_operator.cpp:141-145; sibling rs_spectral_similarity got the per-band wash (R5) | OPEN | WP-A3 wash + test |
| NEW (audit) continuum_removal band-1 sentinel for whole spectrum | band-k voids (−9999) enter the convex hull | rs_continuum_removal_operator.cpp:98-103,197,246 | OPEN | WP-A4 wash + test |
| NEW (audit) radiometric_qa ignores NoData entirely | −9999 sentinel enters stats as FlagNegative; counted as evaluated; output declares no NoData | rs_radiometric_qa_operator.cpp:218-269, radiometric_qa.cpp:42-53 | OPEN | WP-A5 mask + stats exclusion + test |
| NEW (audit) image_fusion single working sentinel for pan+MS | per-band finite sentinels differing from chosen one pass as data | image_fusion.cpp:782-805 + kernels at 106-108,147,400-402,494,613-618 | OPEN | WP-A6 assess + fix + test |
| #1455/#1456/#1462 etc. | operator runtime / python / geospatial clusters | file lists of #1504/#1505 cover the named files | UNRELATED / other tracks | none |
| #1478–#1482 review threads | Qt/agent/io hardening | closed by merged PRs of R10–R13 rounds per BASE_SHA log | STALE (no repro in scope) | none |
