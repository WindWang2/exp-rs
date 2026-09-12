# EVIDENCE — lab-content-expansion (local only; no CI citations)

All evidence generated locally. Build: CMAKE_BUILD_PARALLEL_LEVEL=2, Ninja -j2. Tests: CTEST_PARALLEL_LEVEL=1, QT_QPA_PLATFORM=offscreen.

(appended per phase)

## Phase 1-4 authored artifacts (commits 07451f10a3, 1514e947b5, 2f4526c9a9, 9f75487ad5, 97f0874998)

- All 4 LabSpecs parse + validate against labspec.schema.json (jsonschema, local).
- H6 zero-drift check passes: pipeline refs == spectral library spectra.
- SAR fixture statistics verified: water sigma0 mean 0.00635 (prior -22 dB), ENL 3.87 (4-look target ~4), forest 0.02507.
- All 18 referenced operator ids verified against rs_operators_init.cpp registrations (source-level; CLI `--list` check to follow once binary built).
- JSON validation + syntax checks run locally (no CI).

## Phase 5 in progress

- sicnu_geo_rs_cli build at -j2 (Ninja, Release, ccache). Two transient GCC 16.2.1 ICEs in qgis_core TUs under -j2+ccache; retried, one TU rebuilt clean directly; resumed. Resource log: logs/resources.log (60 s sampling).
- Run harness (scripts/run_lab_pipelines.py) + output verifier (scripts/verify_lab_outputs.py) committed; headless run evidence to be appended below once binary lands.

## Phase 5/7 — headless evidence (final, 2026-09-13)

Environment: sicnu_geo_rs_cli @ this branch, GCC 16.2.1, Ninja, Release.
`ctest -R test_lab_chains -j1` offscreen: 112 assertions / 4 test cases, ALL PASS
(includes registry resolution of every pipeline operator id, LabSpec artifact
consistency, lab11 MapSpec validate + compliance preflight + offscreen compile +
PNG export through QgsLayoutExporter).

Pipelines (QT_QPA_PLATFORM=offscreen, scripts/run_lab_pipelines.py):
- lab8 temporal:    Pipeline succeeded (6 steps)
- lab9 sar:         Pipeline succeeded (7 steps)
- lab10 hyperspec:  Pipeline succeeded (5 steps)
- lab11 cartography:Pipeline succeeded (3 steps)

Grading-intent verification (scripts/verify_lab_outputs.py): **23/23 PASS**.
Key measured values (full table in the committed tool output):
- T3 disturbance slope −4.76e-4 NDVI/day (≤ −3e-4 ✓), forest ≈ 0; T4 SOS 87/POS 199/EOS 315/LOS 228;
  T5 patch z −2.50 (baseline 2024-03-01..2024-08-31) ✓
- S2 Lee 5×5 ENL gain 6.16×, mean drift 3.0% ✓; S3 detection 99.4%, false alarm 0.11% ✓;
  S4 band2 valid 65136 = 65536−400 with NaN hole preserved (#803) ✓;
  S5 gamma0/σ0 = 2.28 with DEM slopes, 3 bands ✓ (#785)
- H2 PPI angles 20.1°/17.3°/1.2° (mean 12.9 ≤ 15 ✓); H3/H4 SAM/SID pure-zone accuracy 1.000 ✓;
  H5 own-abundance 0.99/0.95/0.95, sum dev 0, meanErr 0.012 ✓; H6 zero-drift ✓
- K2 vegetated fraction 90.2% ∈ [80,95] ✓; K3 Otsu threshold 0.144 ✓;
  K5 lab11_thematic_map.png exported 80970 bytes @200dpi ✓

## Master build blocker encountered & resolved locally (documented)

master @ 27b9aa0a63 does not compile: #943 left `auto *runtime = mHostProcessRuntime;`
(unique_ptr) in plugin_runtime_host.cpp:82/62 — GCC16 rejects auto* deduction from
unique_ptr; sicnu_geo_rs_cli links sicnu_plugins_framework unconditionally. Fixed
mechanically in commit c2247f651b (2 lines, `.get()`), disclosed in PR body for
possible upstream drop. Additionally GCC 16.2.1 served a cached ICE for this TU via
ccache (cached crash output), masking the real C++ error — CCACHE_DISABLE revealed it.

## Build-environment notes

- Resource sampling: logs/resources.log (60 s). Machine concurrently runs 3+ sibling
  track builds; transient GCC 16.2.1 segfault-ICEs on unrelated TUs occurred under that
  contention and cleared on retry (documented in logs/build.log ITER markers).
- data/labs/_tmp deleted before the final commit per runbook.
