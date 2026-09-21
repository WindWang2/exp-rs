# DEDUP — flash-spectral-intelligence-13

Source-level census (3 parallel read-only agents, Phase 0). Verdicts: what master
already ships, what is a real gap, and the pivot rules if a new PR preempts.

## Already implemented on master (DO NOT redo)

| Capability | Where | Evidence |
|---|---|---|
| MF / ACE kernels + streaming operator | `src/processing/algorithms/spectral_detection.{h,cpp}`, `rs_spectral_detection_operators.cpp` | `runDetector(kind="mf"/"ace")` |
| CEM kernel + operator + closed-form tests | `spectral_cem.{h,cpp}` | `SpectralCem::buildFilter/cemScore`, tests/test_spectral_cem.cpp |
| RX anomaly + local RX (dual window, halo streaming precedent) | `spectral_anomaly.{h,cpp}`, `spectral_local_rx.{h,cpp}` | `rs_local_rx_operator.cpp` halo pattern |
| Mean-window spectral-spatial fusion + operator | `spectral_spatial_fusion.{h,cpp}`, `rs_spectral_spatial_fuse_operator.{h,cpp}` | convex window mean, halo-equivalence tests |
| MatchIndex scale index over library matching | `spectral_library_scale.{h,cpp}` | bit-exact vs brute force, 1202-entry tests |
| Library domain v2 (provenance, sensor registry, taxonomy, priors) | `src/processing/algorithms/spectral_library.{h,cpp}` + `data/spectral/*` | ADR 0081/0156 |
| Second-raster input precedent (grid preflight) | `rs_sar_ratio_operator.cpp` (inputA/inputB + `compareGrids`) | the model for background-raster wiring |
| Wavelength grid read + reference reconciliation seam | `rs_spectral_reference_input.{h,cpp}` (`RasterWavelengthGrid`, `resolveSpectralReference`) | shared by detection operators |
| Registration surface machinery | `rs_operators_init.cpp`, `scientific_contract.cpp`, `algorithm_meta` generator, `capability_catalog`, `data/agent/capabilities/*` | gates: meta drift (pin 55), capability drift, surface parity, census |

## Real gaps (this track's scope)

1. **TCIMF** — no code anywhere (`git grep -i tcimf` on origin/master: only a
   literature citation in `spectral_detection.h:9` and ADR 0165:99-101). New kernel.
2. **OSP** — no code anywhere (same evidence). New kernel.
3. **Independent background raster** — operators hard-code scene background;
   metadata explicitly documents the future-extension limitation. New optional
   `background` parameter + grid reconciliation + diagnostics.
4. **Edge-preserving fusion** — only the mean window exists. New `method` on the
   same kernel/operator surface (mean stays default = backward compatible).
5. **Library consolidation** — two parallel implementations:
   - A: `exp_spectral::SpectralLibrary` (`src/core/spectral_library.{h,cpp}`) —
     production-dead (no `src/` caller), own JSON dialect (`{"format":1,...}`,
     `wavelengthsNm`), own hand-rolled Gaussian SRF, no dedupe/provenance.
   - B: `SpectralLibrary::Library` (`src/processing/algorithms/spectral_library.{h,cpp}`) —
     de-facto authority (agent tool, `rs:library_select`, `rs_spectral_reference_input`,
     dialog), v2 provenance schema, ships `data/spectral/library.json` (25 entries).
   Consolidation: B is the authority; A is deleted and its two test-only call sites
   rewired onto B's API; a 4th SAM re-implementation
   (`rs_library_select_operator.cpp:47-67` `findNearDuplicates`) is rewired onto the
   shared kernel; agent-tool hardcoded relative path unified to
   `resolveRuntimeDataPath`.
6. **Registration surface debt on master** (blocking gates, in our ownership):
   D8 capability sidecars `data/processing/algorithm_meta/capability/rs-cem-detection.json`
   and `.../rs-spectral-spatial-fuse.json` do not exist on master (PR #1135 adds them).
   `tests/test_capability_surface_parity` is red on master for that reason. This track
   regenerates them with the repo generator (identical bytes to #1135's output if both
   run the same tool), then adds its own new rows on top.

## Known pre-existing defects observed (not this track's scope unless touched)

- `src/cli/cli_commands.cpp` failed to compile on master at 12.0 time (per #1119 body);
  re-verified at Phase 0 — see DECISIONS D9.
- MF/ACE have no min-sample gate (only `count == 0`); CEM fails closed at 2B+2.
  TCIMF/OSP inherit CEM's fail-closed floor (DECISIONS D2).
- Detection score pass leaves declared-NoData pixels finite (documented intentional
  in test_spectral_detection_streaming.cpp:10-13); background raster makes this a
  cross-raster question — see DECISIONS D5.
- `spectral_anomaly.h:14-16` doc says "biased" but implementation is unbiased
  (/(N−1)); CEM second moment is biased (/N). Doc-only defect in a file this track
  touches indirectly — fixed if the same lines are edited, otherwise recorded.

## Dynamic dedup rules (checked again at each milestone)

- New open PR touching `src/processing/algorithms/spectral_*`,
  `src/operators/rs/rs_spectral_*`, `src/core/spectral_library*`, or the generated
  surfaces → pivot per OWNERSHIP, record here.
- #1135 is the only overlapping PR and only on generated surfaces; mitigation is
  regenerate-wholesale at PR time from the union tree (never hand-merge JSON).
- If another track lands TCIMF/OSP first: this track pivots to background raster +
  edge-preserving fusion + library consolidation (the remaining gaps) and records it.

## Dynamic dedup — milestone checks during the track

- After WP-C/D (mid-implementation): origin/master unchanged (79adfe78); new open
  PR #1138 (plugin lifecycle) — zero spectral overlap (only src/sdk/CMakeLists.txt).
- Before independent review: master unchanged; PRs #1139-#1143 opened — overlap scan
  on this track's source files reports 0 hits; #1140 (capability-search-13)
  regenerates the same capability sidecars/knowledge pages → union-regeneration
  rule recorded.
- Before PR (final): origin/master moved to d7f99fb5b with #1140 MERGED. The union
  was taken (merge commit 335b41eb0) and ALL machine surfaces regenerated from the
  merged tree with the repo tools (export-catalog, gen-meta, gen-pages,
  contract_inventory). #1140's authored sidecar enrichment was adopted into this
  branch's superset versions of rs-cem-detection.json / rs-spectral-spatial-fuse.json
  (updating the fuse failure mode for the 13.0 Streaming policy) so no authored
  content is lost by regeneration.
- Master defect found while merging: origin/master does not configure
  (sicnu_add_mission_runtime_test references mission-runtime-gate/tests/ sources via
  a bare ${NAME}.cpp). Repaired minimally in tests/CMakeLists.txt so this branch's
  union tree builds; flagged for the mission-runtime owner in the PR body.
- No pivot was needed: no open PR or merged PR implements TCIMF/OSP, the background
  raster, edge-preserving fusion, or the library consolidation.
