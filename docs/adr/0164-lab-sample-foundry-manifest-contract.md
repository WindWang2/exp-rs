# ADR 0164: Lab Sample Foundry Manifest, Spec-Intake and Truth Contract

## Status

Accepted (goal D1 — Lab Sample Data Foundry)

## Context

All seven undergraduate labs in `docs/labs/` start by loading
`data/samples/landsat_sample.tif`, but that directory never existed in the
tree, and the only generator (`tools/generate_sample_data.cpp`) was an orphan:
registered in no `CMakeLists.txt`, GPS (degree) grid, LZW/striped layout, no
band-role metadata, no ground truth, and seeded through
`std::normal_distribution` whose output is implementation-defined — so even a
rebuild on another OS produced different pixels. Downstream tracks need more
than "some files": D4 (auto-grading) grades against ground truth, D2 (LabSpec)
and D3 (lab expansion) need to *declare* which products an experiment consumes,
and D7 (offline deployment) needs to bundle byte-stable data with an integrity
anchor. Meanwhile the repo already has a fingerprint idiom (ADR 0134: SHA-256
over a canonical payload with the self-reference field removed) and a
semantic band-role vocabulary (ADR 0065).

## Decision

1. **`sicnu_generate_samples` is a first-class, unconditional build target**
   (`tools/` → `add_subdirectory(tools)`). The foundry library is Qt-free —
   std C++20 + GDAL + jsoncpp only, mirroring the Geospatial I/O leaf policy —
   so it configures on every lane (Ninja/MSVC/Make, `configure_wb7.cmd`) and
   ships as a standalone binary (D7 bundle seam).

2. **Profiles are closed**: `--profile=lab` (256×256) and `--profile=stress`
   (2048×2048), both 7-band, EPSG:32648, 30 m, one grid for every product.
   `--seed=<n>` defaults to 42.

3. **Determinism contract** (enforced in code, tested in
   `tests/test_sample_fixtures.cpp`): `std::mt19937` raw stream with a portable
   uniform transform (no `std::distributions`); synthesis arithmetic restricted
   to +,−,*,/ on IEEE doubles with FP contraction compiled off; documented
   draw order (row-major pixels, bands innermost, before-noise then
   after-noise); no wall clock, locale, unordered-container or thread
   dependence in the emit path; the shapefile DBF creation date is pinned to
   2000-01-01. The emit path additionally pins the GDAL configuration it
   depends on — `GDAL_PAM_ENABLED=NO` (no `.aux.xml`), `GDAL_NUM_THREADS=1`
   plus the `NUM_THREADS=1` GTiff creation option (mirroring the deterministic
   COG preset in `src/geospatial/io/cog_options.cpp`), and `SHAPE_ENCODING=""`
   (the Shapefile driver then never writes a `.cpg`) — through a scoped guard
   that restores the previous values on scope exit, so a host-exported GDAL
   environment can neither perturb the emitted bytes nor leak configuration
   between the foundry and its embedder. Same host+GDAL+profile+seed ⇒
   byte-identical outputs and manifest. The only transcendentals are
   `atan`/`atan2` in the slope/aspect truth layers, which are host-stable but
   not guaranteed bit-identical across libm implementations — the one
   documented crack in cross-host bit-identity.

4. **Ground truth is mandatory**: every product has a companion
   (`landsat_truth.tif` class mask ids 1–6, `change_truth.tif` change mask,
   `dem_slope_truth.tif`/`dem_aspect_truth.tif` analytic closed-form
   slope/aspect). Truth *pixel content* is seed-independent and pinned by
   golden counts in the fixture suite. Slope/aspect are the analytic gradient
   of the emitted DEM surface — student finite-difference output approximates
   (does not equal) them; tolerance policy is D4's.

5. **Manifest contract**: `data/samples/manifest.json` lists seed, profile,
   grid, generator + GDAL version, and per-file SHA-256/size; a
   `self_fingerprint` is the SHA-256 over the manifest with that field removed
   (ADR 0134 semantics). `--verify` re-hashes and exits non-zero on drift.
   Generated rasters stay untracked (`data/*` policy); the schema template
   `data/samples/manifest.template.json` is tracked, and
   `data/samples/manifest.json` itself is un-ignored so a release manager MAY
   commit a blessed reference manifest.

6. **Re-run ownership boundary**: the foundry manages exactly its own
   artifacts, derived from the fixed catalog — `<product>.tif` per raster
   product and the `training_samples.{shp,shx,dbf,prj,cpg}` sidecar set — and
   nothing else. Regenerating into a non-empty directory (same or different
   selection) overwrites every owned basename the selection emits (the
   shapefile set is removed before create, never relying on the Shapefile
   driver's version-dependent handling of pre-existing files) and prunes owned
   basenames left by a *previous* selection that the current selection does
   not emit, so a full↔subset switch always leaves a `--verify`-clean tree.
   The directory is never scanned and no non-foundry file is ever removed or
   modified. Locked by the regenerate-twice and full↔subset fixtures.

7. **Spec intake for D3**: `--spec=<file-or-dir>` where each `*.json` is
   `{"experiment": "<non-empty>", "products": [<catalog id>…]}` — unknown
   top-level keys are refused along with everything else. The catalog is
   the fixed nine-product id set. Unknown ids, missing paths, parse errors,
   duplicates, empty lists and cross-file experiment mismatches are typed
   refusals (exit 3, `spec-refusal:` on stderr) — never a silent default; a
   valid spec generates exactly the union of the requested products. Selection
   order is normalized to catalog order. Without `--spec`, the full set is
   generated.

8. **CLI exit codes are contract**: 0 ok · 1 generation/I/O · 2 usage ·
   3 spec refusal · 4 verify drift.

9. **Bundle seam only (D7)**: `docs/datasets/BUNDLE.md` specifies the directory
   layout D7 zips; no zip logic in this track, and no first-run auto-generation
   inside `src/`.

## Consequences

- Labs get real data with one command (`scripts/gen_samples.sh|.cmd`); the
  docs no longer reference a directory that does not exist.
- D4 can grade against stable truth (golden counts, analytic slope/aspect,
  change area) instead of ad-hoc fixtures.
- D3 declares experiment data through spec files without touching the
  generator; unknown requests fail loudly.
- D7 inherits byte-stable data plus a built-in integrity check (`--verify`).
- Tests synthesise fixtures at runtime (repo convention preserved): the suite
  generates into temp dirs and never reads tracked rasters; only the
  generator, the manifest template, docs and scripts are tracked.
- The old orphan's WGS84 grid and spectral signature table are replaced by the
  UTM grid (no reprojection needed in labs) and preserved signature values.
