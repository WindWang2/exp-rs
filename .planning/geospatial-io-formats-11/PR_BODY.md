# Geospatial I/O, COG & Interchange Platform 11.0

**Baseline:** origin/master `a5b11b7f10` (fix: fail-closed fixes for review issues #994–#999 #1000).
**Branch:** `zcode/geospatial-io-formats-11` · worktree `../exp-rs-geospatial-io-formats-11`.
**Local evidence only; no online CI dependency.**

## Scope / dedupe against concurrent PRs (audited at start, 2026-09-16)

Open PRs at start: **#1009** (execution runtime convergence — runtime/chunk, operators/framework, workflow) and **#1008** (radiometric spectral workbench — core/analysis/app). Neither touches `src/geospatial/io/**` (new), `src/geospatial/cog/**`, `src/geospatial/metadata/**`, `src/operators/io/io_operators.*` or `docs/io/**`. Shared integration files (`tests/CMakeLists.txt`, `src/geospatial/CMakeLists.txt`, `CHANGELOG.md`, `.gitignore`) are append-only here. `src/geospatial/convert/` received one additive refactor (makeCogWithOptions); no open PR owns it. Details: `.planning/geospatial-io-formats-11/PARALLEL_OWNERSHIP.md`.

Open issues #1001–#1007 deduped: **#1001 (io:clip srcCrsOverride misuse, P1/critical) fixed here**; #1002–#1007 are workflow/georef/dataset-foundry surfaces outside I/O scope (evidence in BASELINE.md).

## What this PR delivers (rescope to real gaps; no rebuilt wheels)

The 4.0–10.0 foundations already provide staged-atomic publishing, COG presets/validation, canonical metadata, ResourceUri, FormatRegistry. This track deepens the *next* verifiable gaps (audit in BASELINE.md):

**A/F — atomic writer 2 + resume seam**
- `io/finalize_manifest`: streamed-SHA-256 provenance sidecar published INSIDE the atomic group transaction (sidecar-first, main-last); `verifyDataset` recomputes digest/shape from disk and fails closed (missing manifest ≠ green; legacy tolerance is reported as `manifest_missing_allowed`).
- `io/stage_ledger`: dataset-level crash resume — runId journal next to the target, `attachExisting` trust boundary (staged file must open in GDAL and match declared driver/shape), `finalizeAttached` (digest → manifest → fsync → group publish → mark finalized), `discardAttached`, `sweepOrphans` (dry-run by default; `live_staged` transactions never swept). Library seam only — no scheduler; chunk-level resume stays in `src/runtime/chunk` (G01/PR#1009 domain).

**B — COG production**
- `io/cog_options`: explicit blocksize (pow2 128..4096), overview policy (NONE is validator-enforced fail-closed beyond the small-image allowance), deterministic mode (NUM_THREADS=1 + pinned DEFLATE level; byte-identity proven within one GDAL/libtiff build by double-create digest equality), extras merged with REPLACE semantics — the COG driver reads options first-match-wins, so appended `-co` duplicates are dead letters; every key's provenance explained in the returned `cog_plan`. `docs/io/cog-guide.md` OVERVIEWS=ALL→AUTO drift corrected. NoData/alpha deliberately not planner options (declared-only policy).

**C — vector interchange**: `io:convert_format` routes by GDAL capability (`DCAP_VECTOR`+`DCAP_CREATE`) instead of a hard-coded name list; dual-capability drivers (netCDF/PDF/MBTiles: also `DCAP_RASTER`) route by INPUT kind, preserving "GeoTIFF→NetCDF raster export". Declared-but-unwritable vector drivers get typed refusals, never a silent re-route.

**D — subdatasets**: `io:subdatasets` — bounded (cap 64, honest `truncated`), ResourceUri-classified, redacted inventory; `select` projects one entry into the canonical metadata model; non-selector strings refused (trust boundary).

**E — metadata truth**: `io/metadata_patch` — whitelist-only write-back (scale/offset/unit/nodata/role/wavelength/fwhm/color-interp + SICNU_* stamps), validate-THEN-apply (bad batches never open the file), update-capability gate (typed refusal; never a staged full rewrite of a huge file — documented crash-window trade-off, DECISIONS D-004), fresh read-only back-check, and finalize-manifest refresh with `patches[]` history so verification stays truthful.

**G — security/path**: `io/param_guard` wired into ALL 13 `io:*` operators (source/target classification via ResourceUri, remote-write typed refusal `remote_write_offline_policy`, read-only projection targets refused, target directory must exist, credential-redacted error surfaces). Remote SDS selectors never cross the JSON boundary with raw names.

**H — GDAL matrix**: `io/gdal_feature_probe` + `test_io_gdal_matrix` — compat-macro states cross-checked against compile-time truth, driver presence vs live GDAL, truncated COG (validator typed refusal + digest-drift verdict), garbage bytes never validate, truncated GPKG typed failure, 40000² logical VRT with COMPUTED byte budget and typed full-window refusal (bounded memory, no materialization).

**#1001 fix**: `io:clip` treats `srcCrsOverride` as a SOURCE declaration again (reaches the warp as `-s_srs`; target stays the source grid). An override on an input that already carries a CRS is a typed refusal (fail-closed, nothing published). Known-answer tests compute expected extents independently (margin 1e-9).

## Compatibility

- Additive API surface (new `src/geospatial/io/` modules, 3 new `io:*` operators); existing operator JSON results gain keys (`cog_plan` on make_cog) without changing existing keys.
- `makeCog` behavior unchanged (preset assembly moved to the call side of a shared helper; one extra read-only dtype probe).
- `atomic_fs::sidecarsFor` gained `.sicnu-manifest.json` — sidecar-first publish/backup/discard apply to it; existing sidecar behavior untouched (6 atomic/roundtrip suites re-verified).
- Writers unchanged by default; manifests appear when finalizeAttached/operator paths write them.

## Local tests (all local, no online CI)

- New suites: `test_io_param_guard`, `test_io_finalize_manifest`, `test_io_stage_ledger`, `test_io_cog_options`, `test_io_vector_interchange`, `test_io_subdataset_inventory`, `test_io_metadata_patch`, `test_io_gdal_matrix` — all green.
- Extended: `test_io_operators` (13 cases incl. #1001 known-answer, verify/metadata_patch/subdatasets operator contracts, netCDF routing).
- Full sweep: 20 × `test_io_*` + `test_adversarial_m3` — all green, re-run after every remediation.
- Oracle 6 double verification: 15 key suites run twice consecutively — both passes fully green.
- Oracles are independent (FIPS 180-4 vectors, byte-flip tampering, OSRIsSame, runtime DCAP/metadata cross-checks) — no test reuses the implementation under test as its truth.
- Environment note: configure needs `-DSICNU_LAB_SKIP_PYTHON_BINDINGS=ON` (pybind11 FetchContent clone fails from this host: TLS) and offline `FETCHCONTENT_SOURCE_DIR_CATCH2`; unrelated to this diff.

## Resources

Builds `-j2`/`-j1` (nice; host load dipped >10 from concurrent tracks → j1 per envelope), tests `-j1` offscreen; no wall-clock gates; scale evidence is logical (1.6e9-cell VRT with computed 12.8 GB double budget refused at the contract boundary). Details: `.planning/geospatial-io-formats-11/PERFORMANCE.md`, `EVIDENCE.md`.

## Review

Independent adversarial review (read-only subagent, full-diff + full-file reading, live GDAL 3.13.3 capability enumeration and jsoncpp probe): **0 P0**, 5 P1, 3 P2, 6 P3 (+2 nits). **All P1/P2 fixed and re-verified; every P3 dispositioned** — table in `.planning/geospatial-io-formats-11/REVIEW_LOG.md`. Highlights: validate-then-apply hole closed (NaN/color-interp + handle-leak), jsoncpp LogicError containment, dual-capability driver routing regression fixed with a pinned test, remote SDS credential redaction, guards actually wired to operators (docs were ahead of code — closed), live-transaction sweep protection, staged/target same-directory invariant, sidecar size caps.

## Known limitations (honest)

- `metadata_patch` requires driver update support; read-only media/drivers → typed refusal (no staged full-rewrite path by design).
- Deterministic COG byte-identity is scoped to one GDAL/libtiff build (documented, tested within this build).
- `sweepOrphans` staged-shape heuristic (`<stem>.<n>.<n>.tmp<ext>`) is documented as ours-by-shape; removal is opt-in and live-ledger-protected.
- No CLI/MCP surface added for the three new operators beyond the registry/schema (agent tools consume `io:` dynamically); follow-up if needed.

## Follow-ups

- Wire `finalizeAttached` into the workflow/GUI export paths that currently bypass manifests.
- Consider `io:capabilities`-style exposure of `vectorInterchangeCapabilities`/`gdalFeatureReport` in the doctor output.
- netCDF-C-authored subdataset inventory fixture could also cover HDF5 when a fixture generator lands.

**Local evidence only; no online CI dependency. Not merged here; awaiting review.**
