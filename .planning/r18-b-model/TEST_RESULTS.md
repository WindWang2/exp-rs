# R18-B Test Results

Host: Linux x64 (cachyos 7.2.9-1), GCC, Qt6 system, GDAL/GEOS/PROJ from the
shared vendor prefix (`paleo-singlefactor-closure`), Catch2 v3.7.1 (local
tarball — FetchContent gitclone blocked by flaky github.com connectivity).
Build: `build-r18-b`, Release, `-j2` under `/tmp/exp-rs-five-tracks-build.lock`
(shared with the four parallel R18 tracks — queue waits observed and honored).
Env: `SICNU_BUILD_REQUIRE_GUARD=1`, `SICNU_BUILD_JOBS_CAP=2`,
`CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`.
Host link/ABI workarounds (build-dir only, no repo changes): `libwrapper2stub.so`
for the vendored libgdal's LAPACK `wrapper2_*` symbols; system shared
libexpat/libsqlite3/libz (vendor statics not -fPIC; vendor .so symlinks
dangling); `-Wl,-rpath-link,/usr/lib,/usr/lib/pulseaudio` so Qt/pulse resolve
against the system before the vendor prefix; `/tmp/r18b-deb/libcblas.so.3` →
vendor OpenBLAS (system libcblas lacks the 64_ LAPACK symbols OpenCV needs);
empty-SONAME stub for `libarmadillo.so.15` (vendored libgdal lists it as NEEDED
but references zero of its symbols — `nm -D` evidence).
Run env for binaries: `LD_LIBRARY_PATH=/usr/lib/pulseaudio:/usr/lib:<vendor>:/tmp/r18b-deb`.

## Round 0 — restoration self-consistency (restored r15-C suite vs restored source)

All six restored `test_r15_C_*` binaries GREEN on the restored source (before
any R18-B fix): 26 test cases / 179 assertions.

## Round RED — new W-tests against restored-but-unfixed source

| Test | Result on unfixed code |
|---|---|
| `#r18B multi-input EO preflight refuses before any product exists` (test_multimodal_inference) | **RED** — 3 assertions failed: product + sidecar + parked stage left at the output path after the typed refusal |
| `#r18B feather blending never inpaints…` (test_model_runtime_9) | **RED** — 200 assertions failed: fabricated predictions over NoData rows (≈10.0 inpaints) and tail rows reading 0 (out-of-window reads) |
| `#r18B scene wavelength windows verify the selected physical band` (test_eo_platform_10) | **RED** — the in-window selection `bands={2}` wrongly refused ("band 1 … 490 nm") — the predicted false-refusal direction |
| `#r18B the sink participates in the fan-out guard…` (test_chunk_graph) | **RED** — `REQUIRE_THROWS_AS` got no exception (both orders) |
| `#r18B …source returning true without a pixel buffer…` (test_chunk_graph) | **RED** — no typed refusal; the graph WEDGED (null payloads flow into the sink; bounded-producer version asserts delivered nulls) |
| `#r18B OpenCV infer detaches…` (test_r14_B_runtime_providers) | NOT reproducible locally — see W1 note below |
| `#r18B a live worker that stops accepting writes…` (test_provider_python) | NOT reproducible — see W2 note below |

### W1 note (contract pin, not a reproduced race)

This host runs OpenCV **5.0.0**, whose graph engine allocates a FRESH buffer
per `forward()` — probe (`/tmp/cvprobe.cpp` in session evidence): two forwards
return distinct data pointers and the first result is unchanged after the
second forward. The defect is real under OpenCV 4.x semantics (the code's own
comment and the r14-B `inferMulti` fix describe exactly that); locally the test
pins the CONTRACT (detachment under the session lock), mirroring the r14-B
precedent's documented identity-fixture caveat. No RED is claimed.

### W2 note (defensive parity, branch unreachable)

The write-deadline branch cannot fire on the current implementation:
`waitForBytesWritten` returns as soon as ANY bytes drain (partial progress), so
the read deadline always expires first — and the read-side branch is already
fail-closed (stops the worker + marks it permanently dead, pre-dating this
track). The W2 change closes the write branch for parity so a future refactor
cannot re-open the fail-open window; the test pins the fail-closed state. No
RED is claimed.

## Round GREEN ×2 — after the fixes (slide fix + deferred-mark v2 + all W fixes)

Both rounds identical and fully green:

| Suite | Result |
|---|---|
| test_model_runtime_9 (incl. W4 + all M-suite regressions) | 17 cases / 1226 assertions |
| test_multimodal_inference (incl. W3) | 9 cases / 8332 assertions |
| test_eo_platform_10 (incl. W5 + restored C16) | 22 cases / 581 assertions |
| test_chunk_graph (incl. W7 ×3) | 34 cases / 371 assertions |
| test_r15_C_manifest_contracts | 6 / 18 |
| test_r15_C_publish_recovery | 4 / 62 |
| test_r15_C_chunk_admission | 5 / 34 |
| test_r15_C_provider_degradation | 4 / 36 |
| test_r15_C_runtime_contracts | 2 / 17 |
| test_r15_C_detection_decode | 5 / 12 |
| test_model_ensemble (restored r15-C version) | 15 / 120 |
| test_r14_B_runtime_providers (incl. W1 pin) | 5 / 18 |
| test_provider_python #r18B (W2 pin) | 1 / 5 |
| test_model_tasks (four-operator seam + runModelInference lanes) | 12815/12816 — 1 env-limited assertion (below) |

### Env-limited assertion (not a code failure)

`test_model_tasks` "detection product publishes a verifier-accepted provenance
sidecar": `prov["inputs"][0]["crs"] == "EPSG:4326"` got `""`. Root cause: the
host has **no proj.db anywhere** (verified `find /`), so GDAL's
`importFromEPSG` fails silently while the test raster is written and the raster
carries an empty projection — `crsDisplayName` then correctly returns "". The
assertion predates r15-C (introduced with the sidecar lane), is untouched by
this track's diff, and passed on the r15-C author's host. Class:
NOT_EXECUTED-environment, compensating evidence = the other 12815 assertions
of the same binary plus the projection-comparison suites (multimodal) green.

## Debugging evidence for the W4b slide defect (P0)

Instrumented builds (temporary fprintf, removed before commit) traced:
`flushThrough(exc=12) top=0` finalizing rows [0,12) while pending marks sat at
linear offsets 12-15 — after `m_top=12` those offsets named rows 24-27, i.e.
one row's weights/marks were reinterpreted as another's. The M5 regression
raster (16 rows) holds span = clamp(16+2·4, 1, 16) = 16 — exactly one window — and never slid, which is why the
defect was invisible since Platform 9.0. Fixed rows are now physically slid up
by the flushed amount (`memcpy` per accumulator row, tail zeroed).

## Benchmarks (Oracle-C) — test_model_runtime_bench, SICNU_MODEL_BENCH=1, ×3

Wall time for the gated suite: 1223 / 1027 / 1046 ms (runs 1-3; run 1 includes
cold page cache). Baseline JSON (benchmarks/model-runtime-4.json): cold load
400.5 ms, multi-input lane 721.3 tiles/s (88.7 ms for 64 tiles), cancel
latency 19.6 ms, 3.41 Mpx/s. CUDA-EP bench: gated `ORT provider not compiled
in` (expected — no ORT on host). No bench exercises the feather path; the
slide fix's added memcpy is bounded at ≈(slots+2)·(span−cleared)·W·4 B per
tile-row flush (~10 MB for a 512-tile/64-halo/4096-wide/3-class raster ≈ 2-3 ms
vs 100 ms+ per tile forward) — see PERFORMANCE.md.

## CLI smoke (B10)

`sicnu_geo_rs_cli --help` (rc=0) and `--operator-help` for `rs:infer`,
`rs:segment`, `rs:detect`, `rs:embedding` all rc=0 with real metadata. Deep
execution paths covered through `runModelInference` in test_model_tasks /
test_multimodal_inference (above).

## NOT_EXECUTED

- CUDA / TensorRT / OpenVINO / ONNX Runtime real-device lanes — no
  hardware/SDK on this host (CUDA-EP bench gate message shown above).
- Power-loss durability for the W6 fsync fix — needs VM-level fault injection;
  verified by code parity with the detection lane (#1174) and the publish
  suites green with fsync in the path.
- Windows-specific paths (loadJournal wipe-while-open, rename-no-overwrite) —
  no Windows host.
- `test_device_planner` — documented by r15-C as hanging on this host family
  (pre-existing environment issue; not re-run).
- Online CI — deliberately not waited for (issue #1491: master CI has had no
  completed runs since 2026-10-11; task book Oracle-G).
