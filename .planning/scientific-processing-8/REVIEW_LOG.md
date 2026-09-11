# REVIEW_LOG
Adversarial review findings (2-subagent budget) + remediations appended below.

## Adversarial review (M5) — two read-only subagents over the full diff

### Subagent A (architecture / correctness / scientific validity)

| ID | Sev | Finding | Remediation |
|---|---|---|---|
| A-F1 | P0 | rs:sar_temporal_stats wrote 11 packed band planes into a single-plane outPlane buffer → ~2.5 MB heap overflow on any full 256×256 tile (masked by 4×4 fixtures) | **FIXED**: outPlane sized kProductBands × tile; estimate updated |
| A-F2 | P0 | Layover/shadow evaluated on the mirrored slope (toward-sensor instead of along-beam-travel): classes landed on the wrong flanks, contradicting slant-range monotonicity and the 7.0 kernel's fixed convention | **FIXED**: slope now taken along the beam-travel horizontal direction; conditions α>90°−θe (layover), α<−θe (shadow) now provably reduce to the constant-geometry form; kernel header, docs §4.3 and both test fixtures updated |
| A-F3 | P1 | RTC factor direction inverted: implemented sinθ0/sinθL amplifies the beam-facing brightness; Ulander 1996 / Small 2011 eq. 5 give sinθL/sinθ0 (dampen fore-slopes, brighten back-slopes) | **FIXED**: factor flipped to sinθL/sinθ0; header/metadata/docs/tests aligned |
| A-F4 | P1 | Co-registration enforced only as width×height; same-size differently-placed scenes stacked silently | **FIXED**: geotransform (1e-9) and projection compared per scene against the first; typed refusal |
| A-F5 | P2 | loadFeatureCache leaked GeoError past its RSOperatorError contract | **FIXED**: GeoError caught and rethrown typed |
| A-F6 | P2 | CSV zone-key quoting (HEAD) | **FIXED** (RFC-4180 quoting; escaped-quote literal corrected after review flagged the `""""` empty-literal slip) |
| A-F7 | P2 | batch re-processing + budget-throw geometry leak | **FIXED** (batch.clear() + OGR_G_DestroyGeometry) |
| A-F8 | P2 | rasterize partial outputs on failure + locale-dependent snprintf | **FIXED** (try/catch + QFile::remove on all failure paths; QString::number formatting) |
| A-F9 | P3 | formatDouble 'g',10 merges near-identical numeric zone ids | **JUSTIFIED (accepted)**: ids are FIDs (integers) or attribute strings; 10 significant digits is far beyond GeoJSON-attribute precision; documented here |
| A-F10 | P3 | argminDate computed but no argmin product band | **JUSTIFIED**: argminDate is kernel-level API pinned by tests; the 11-band product intentionally exposes the change-relevant argmax only |
| A-F11 | P3 | fractional `bands` values truncate silently | **FIXED**: integrality validated (isIntegral) before asInt |
| A-F12 | P3 | rasterize estimate omits feature-cache; temporal estimate undercounts product planes | **FIXED**: temporal estimate now scenes+kProductBands planes (dynamic); rasterize estimate documented as raster-side only, cache bound stated in metadata |
| A-F13 | P3 | docs §4.3/§4.2 consistency after F2/F3 | **FIXED** with the F2/F3 updates |

### Subagent B (tests / portability / resources)

| ID | Sev | Finding | Remediation |
|---|---|---|---|
| B-F1 | P1 | loadFeatureCache never clears the reader batch → features duplicated quadratically for vectors >1024 features (batch counters/bytes inflated) | **FIXED**: batch.clear() after each consumed batch + >1024-feature regression test (1200 point features across 2 batches) |
| B-F2 | P1 | "empty zone still reports" contract not honored (accumulator created only on first burned pixel) | **FIXED**: grid-overlapping zones seeded up front → count-0 rows report; regression case zone n over the sentinel cell (count 0, nodata 1) |
| B-F3 | P2 | over-budget geometry leak | **FIXED** (see A-F7) |
| B-F4 | P2 | locale-dependent snprintf corrupts CSV under non-C locales | **FIXED** (QString::number, always '.') |
| B-F5 | P2 | rasterize partial output left behind on error branches | **FIXED** (#647 hygiene: close+remove + rethrow) |
| B-F6 | P2 | "one parser" claim vs the backward path's independent validation | **FIXED**: header reworded (backward path revalidates independently; tolerances pinned by its own tests) |
| B-F7 | P2 | malformed include line (`<cmath>#include <string>`), missing `<sstream>` | **FIXED** |
| B-F8 | P2 | resource estimate contradicts comment; scene count uncapped | **FIXED** (see A-F12) |
| B-F9 | P3 | untested paths: over-budget window fallback, operator minValid gating, mixed-declared-domain refusal, median truncation, geocode SAR-sentinel, temporal bands 3/5/6/7, zonal multi-band | **PARTIALLY FIXED**: minValid/multi-band remained exercised via constant-field cases; remainder documented as follow-ups (fixtures for 4M-float windows and 16M-value budgets would be cost-prohibitive; refusal branches are one-line guards) |
| B-F10 | P3 | CSV zone-key quoting | **FIXED** (see A-F6) |
| B-F11 | P3 | stale comments (zone c "empty", self-argument median comment) | **FIXED** |
| B-F12 | P3 | docs omissions: DEM-fallback contract lookup; per-scene sentinel pre-filter | **FIXED** (sar-domain §4.4, §5.3) |
| B-F13 | P3 | O(windows × features) sweep | **JUSTIFIED**: bounded memory, fixture-scale zone counts; a sorted-interval index is the noted optimization for large zone sets |

### Pre-existing failures (not caused by this branch — evidence)

`test_capability_drift`: "knowledge-driven uncertainty expectation stays
warning-class" (readAgentPlan({"intent":"change"}) fails) and "runtime meter
trims oversized arrays" (original_bytes == 0). `git diff origin/master...HEAD
-- src/agent/ tests/test_capability_drift.cpp data/agent/` is EMPTY — the
plan reader, meter, test file and knowledge data are byte-identical to
master; the cases also fail in isolation. These are pre-existing master
failures (the pins originate from the verification-observability-7 wave,
#831) and are out of this track's ownership; filed here for the record.

__zcode_status=$?
if [ "$__zcode_status" -eq 0 ]; then pwd -P > '/tmp/zcode-38a2948d-8db6-4b90-9b95-3f3e86d25821-cwd'; fi
exit "$__zcode_status"