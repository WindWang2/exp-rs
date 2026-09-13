# OFFLINE_BUNDLE.md — Offline Classroom Bundle Contract (goal D7)

**Status**: contract · **Owned by**: D7 · **Consumed by**: `scripts/build_offline_bundle.{sh,cmd}`,
D1's generator output (`bin/sicnu_generate_samples`), D9's MCP runtime precondition.

## Problem statement

Teaching labs run on classroom Windows machines that are almost always offline. Today there is no
Windows story at all in the docs, the four remote-I/O verification baselines hang without a network,
and assembling "everything a student needs" requires a developer. A teacher must be able to unpack
one archive on a clean machine and run experiment 1 end-to-end — generate sample data, process it,
grade the result — without a single outbound network call.

## Solution

A single self-contained directory `sicnu-lab-<version>/` produced by one builder command from an
already-built tree. Every byte the lab needs travels in the bundle: binaries, sample data,
lab specs, grading rules, pipelines, schemas, fonts, and the one-click scripts. A manifest with
per-file SHA-256 makes the bundle verifiable on the target machine, and a declared size ceiling
keeps it copyable to 60 machines off a USB stick.

## Bundle layout (normative)

```
sicnu-lab-<version>/
  bin/                      sicnu_geo_rs_cli(.exe), sicnu_generate_samples(.exe);
                            Windows: self-contained runtime (windeployqt output
                            with --compiler-runtime + QGIS/GDAL DLL closure from
                            the configured QGIS_BIN)
  data/
    samples/                deterministic sample rasters (generated at bundle time, seed-42 stable)
    labs/                   grading/<id>.rules.json + lab_rules.schema.json (+ labspecs when D2/D3 land)
    pipelines/              runnable pipelines shipped from the repo data tree
    schemas/                pipeline_schema.json and sibling schemas
    fonts/                  IBM Plex font set (repo resources/fonts)
    runtime/                PROJ/GDAL data (proj.db & co) — grading compares
                            EPSG authorities; without proj.db every submission
                            would lose the blocking CRS assertion
  labs/
    lab1/                   lab1_ndvi.pipeline.json + INSTRUCTIONS-zh.md (the 5-minute experiment)
  RUN.cmd                   run lab 1 offline end-to-end (generate → process → report)
  GENERATE_SAMPLES.cmd      regenerate data/samples via bin/sicnu_generate_samples
  GRADE_ALL.cmd             batch-grade a submissions folder into grades.csv (lab --batch)
  VERIFY.cmd / VERIFY.ps1   self-contained integrity check for the target machine
                            (re-hashes every file against manifest.json)
  README-zh.md              teacher quick-start (中文)
  manifest.json             bundle identity + completeness contract (below)
```

Top-level names are fixed by autonomy default; fonts live under `data/fonts/` because the top level
is closed. Everything else in `bin/` (Windows DLL set) is host-dependency closure, recorded in the
manifest like any other file.

## Manifest schema (`sicnu.offline_bundle/1`)

```json
{ "schema": "sicnu.offline_bundle/1",
  "bundle_version": "<version>",
  "created_utc": "<iso8601>",
  "size_ceiling_mb": 250,
  "required": ["bin/", "data/samples/", "data/labs/grading/", "data/fonts/",
               "data/runtime/proj/", "labs/lab1/", "RUN.cmd",
               "GENERATE_SAMPLES.cmd", "GRADE_ALL.cmd", "VERIFY.cmd",
               "VERIFY.ps1", "README-zh.md", "manifest.json"],
  "files": [ { "path": "bin/sicnu_geo_rs_cli", "bytes": 123, "sha256": "<hex>" } ] }
```

- `required` entries are prefixes; every one must match at least one file (or exist as a directory).
- `files[]` covers **every regular file** in the bundle; verification recomputes bytes + sha256,
  re-walks the tree to flag unlisted files, and rejects absolute/`..` manifest paths.
- `size_ceiling_mb` is the declared ceiling (default 250 MB). `--max-mb` may lower it, never raise
  the default silently: a ceiling breach fails the build unless `--max-mb` explicitly overrides.

## Builder contract

`scripts/build_offline_bundle.sh` (POSIX) and `scripts/build_offline_bundle.cmd` (Windows) implement
the same steps in the same order:

1. Locate the built binaries in `--build-dir` (default `build-dev`); fail with a targeted message if
   `sicnu_geo_rs_cli` or `sicnu_generate_samples` is missing.
2. Generate `data/samples/` at bundle time with `sicnu_generate_samples` (deterministic; identical
   bytes on any machine — the offline contract does not trust network mirrors).
3. Assemble the layout above from the source tree (`data/` subset excluding `benchmarks/`,
   `resources/fonts` → `data/fonts/`, `packaging/bundle/` templates → top level).
4. Windows only: make `bin/` self-contained. `QGIS_BIN` (or `SICNU_QGIS_BIN`)
   is a **hard requirement** — the CLI links `qgis_core`, which windeployqt
   cannot supply; the builder fails fast when it is unset or lacks
   `qgis_core.dll`, and after the copy loop when `qgis_core.dll` is still
   missing. It deploys `windeployqt --compiler-runtime` (MSVC CRT), copies the
   `%QGIS_BIN%\*.dll` closure, and copies `share/proj` + `share/gdal` (from
   `SICNU_QGIS_SHARE`, default `QGIS_BIN\..\share`) into `data/runtime/`;
   `proj.db` is a hard sentinel. The bundle scripts set `PROJ_DATA`/`GDAL_DATA`
   relative to the bundle root.
5. Write `manifest.json` (walk files, hash, sizes, ceiling).
6. Verify (`--verify <bundle>`): re-walk, re-hash, check `required`, check ceiling; print
   `BUNDLE VERIFY PASS/FAIL <path> (<n> files, <size> MB / ceiling <c> MB)` and exit non-zero on any
   violation.

## Zero-network rule

The bundle makes no network call at any point: generation, processing, grading, and verification
are purely local. The CLI `--offline` flag / `SICNU_OFFLINE=1` env turns remote-input attempts into
typed refusals (see `docs/deployment/lab-offline.md`); the bundle scripts always set it.

## Testing seams

- **Builder seam**: `--verify` is the single testable seam — a bundle either passes the manifest
  check or names the offending file/cause. The phase-5 smoke exercises it end-to-end on Linux.
- **Grading seam**: `OutputVerifier::gradeArtifact` (ADR 0146) — batch grading composes it one
  submission at a time (`tests/test_lab_batch.cpp`).
- **Offline seam**: `sicnu::data::offline` mode flag — typed refusal, no socket attempt.

## Out of scope

- AppImage / Linux desktop packaging (existing `packaging/build-appimage.sh`).
- GUI (non-CLI) classroom flows; STAC browser.
- D1's sample-foundry profiles beyond the default lab set (consumed as-is when D1 lands).
- Telemetry of any kind — there is none, and the bundle adds none.
