# R25-D PERF BASELINE (measured, not gated — #1622 discipline: no wall-clock pass gates)

Host: Linux 6.18-cachyos-lts, 16 cores / 62 GB, offscreen (QT_QPA_PLATFORM=offscreen),
Release build (build-ci-fast). 2026-10-28. Five parallel r25 tracks share the host
(build lock serializes compiles; test runs are interactive-era measurements —
comparable for before/after, NOT absolute latency claims).

## Class 1 — real render/export path (CP11 suite, render gate ACTIVE)

`test_cartography_production_11` — 32 cases, including the 5 formerly-gated render
cases (#1655: gate is run-by-default in master; no SICNU_CP11_RENDER opt-out used):

- wall ≈ **6.5 s**, peak RSS ≈ **1.44 GB** (process-wide, includes QgsApplication +
  56-template corpus produce passes with real QgsLayoutExporter PNG/PDF writes).
- 29/32 pass; the 3 failures are IDENTICAL with the master-state compiler
  (verified by rebuilding against master's mapspec_compiler.cpp: 29/32 both) —
  host baseline failures (GDAL "PNG driver does not support update access" noise;
  atlas-series-a4l + report-atlas-appendix-a4l corpus: "atlas enabled but no
  coverage_layer"), isolated, not this diff.

## Class 2 — canvas invalidation discipline (structural counts)

`test_r25_d_canvas_invalidation`: 12 visibility toggles settle with **≤2 canvas
layer-set changes** (bridge coalescing + batch deferral) and **0 manager syncs**
(visibility rides the deferred bridge; the manager's setCanvasLayers counter is
untouched). Wall ≈ 0.5 s / case. A per-toggle refresh regression would read 12
here — counts, not milliseconds.

## Class 3 — E2E chain cost (display + real TaskCenter + compose/export)

`test_r25_d_e2e_display_cartography`: the full product→view→rs:ndvi(TaskCenter)→
result-layer→template→compose→export→project-roundtrip chain runs in **≈0.5 s**
(Release, 64×64 fixture, 96 DPI PNG export, sha256 evidence). The rs:ndvi job and
both cartography jobs execute on real JobEngine workers via the registry dispatch.

## Async-by-design record (GUI-thread stall avoidance)

- Spectral identify on network-backed sources samples off the GUI thread on a
  dedicated bounded pool with generation supersede (#1561 fix already in master —
  verified, not re-fixed; see ISSUE_TRIAGE).
- The cartography dock's every action runs as a TaskCenter job (no operator on
  the GUI thread); preview renders at a bounded 72 DPI.

## Build/evidence environment notes

- ccache hit-rate ≈ 48% at track start; full-tree Release configure+build ≈ hours
  at -j2 under the shared lock — targeted builds used for iteration (task-book
  protocol), neighborhood suites rebuilt fully before regression runs.
- github.com unreachable from this host (TLS eof): FetchContent deps reused from
  the sibling r25-C exact-version cache (catch2 v3.7.1, pybind11 v2.13.6).
