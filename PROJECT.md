# Project: exp-rs — Remote Sensing AI & Geospatial Analysis Platform

## Architecture
- Repository: `exp-rs` (C++20 / Qt 6.8+ / GDAL / PROJ / GEOS / OpenCV / Catch2 v3.7.1)
- Main Branch: `master`
- Build System: CMake (Release + Ninja in `build/`)
- Test Runner: CTest with Catch2 (see TEST_INFRA.md; `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`)

## Platform Capabilities (long-lived)
- **Geospatial I/O & Data Plane**: GDAL-backed raster/vector pipelines, streaming block I/O with bounded memory, project workspaces with governance and reproducibility (ADR 0129).
- **Operators**: self-describing `rs:`/`gdal:`/`otb:`/`opencv:` operators over one registry, powering GUI, TaskCenter DAGs, CLI and Agent/MCP (see `src/operators/`).
- **Model Runtime (ADR 0130)**: manifest 4.0 identity (stable id, version, content digest), bounded session pool, deterministic cpu/cuda:N/auto devices, atomic tiled inference with an OOM ladder, declarative pre/post (labels/mask/confidence products, detection decode to vector), one execution seam (`rs:infer` / `rs:segment` / `rs:detect` / `rs:embedding`), failure-matrix tests and a benchmark baseline (`docs/models/`, `docs/inference/`).
- **Workflows & Agents**: TaskCenter DAG execution, workflow sessions, MCP server (`--mcp`), `spatial:`/`temporal:` agent tools, Pi extension (`pi/exp-rs-spatial.ts`).
- **GUIs & Labs**: desktop shell (map canvas, layer tree, layout studio), classification/OBIA/georeferencing labs, processing toolbox dialogs.

## Code Layout
- Core libraries: `src/core`, `src/gui`, `src/data`, `src/processing`, `src/operators`, `src/workflow`, `src/jobs`, `src/agent`
- Model runtime: `src/operators/framework` (catalog/registry), `src/operators/runtime` (backends, engines, execution seam), `src/operators/rs` (task operators)
- Model manifests: `models/*/model.json` (weights never committed)
- Model docs: `docs/models/`, `docs/inference/`
- Applications: `src/app` (`sicnu_geo_rs`), `src/cli` (`sicnu_geo_rs_cli`)
- Tests: `tests/` (Catch2 test executables)
