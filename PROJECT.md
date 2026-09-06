# SICNU GEO RS (`exp-rs`) — Project State

A pure C++ remote-sensing analysis platform built on the QGIS engine, with
an agent surface (MCP/Pi/CLI/GUI) over one unified execution seam. This
document is the living project-state overview; per-sprint history lives in
git history, ADRs (`docs/adr/`), and `CHANGELOG.md`.

## Architecture

- Repository: `exp-rs` (C++20 / Qt 6.8+ / GDAL / PROJ / GEOS / OpenCV 5 / Catch2 v3.7.1)
- Main branch: `master`; work flows through short-lived feature branches and PRs.
- Build system: CMake (Release + Ninja in `build/`); ccache recommended.
- Test runner: CTest with Catch2 (`QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib
  ctest --test-dir build --output-on-failure`). `CTestCustom.cmake` pins
  `PYTHONHOME`/`PYTHONPATH` and `QT_IM_MODULE=compose` (see TEST_INFRA.md).
  Never claim "suite green" without a fresh `ctest` log.

## Subsystem map

| Subsystem | Home | State |
|-----------|------|-------|
| QGIS core/GUI libraries | `src/core`, `src/gui` | vendored/forked, builds on Linux/Windows CI tiers |
| Processing framework + Task Center | `src/processing`, `src/jobs` | unified algorithm registry, DAG pipelines, tool-call dispatcher |
| Operators (`rs:`/`gdal:`/`otb:`/`opencv:`) | `src/operators` | operator-ization + determinism grades |
| Workflow engine 2.0 | `src/workflow` | lifecycle, deterministic cache, recovery, GC |
| Agent infrastructure | `src/agent` | MCP server, spatial tools, contracts, MapSpec cartography, workspace state |
| Data governance & reproducibility | `data/governance` | workspace identities, governance store, project format v3 |
| Cartography design system | `src/agent/mapspec`, `src/agent/cartography`, `data/cartography` | MapSpec 2.0, design tokens, component/template libraries, compose→preflight→repair (ADR 0130/0131; docs in `docs/cartography/`) |
| Applications | `src/app` (`sicnu_geo_rs`), `src/cli` (`sicnu_geo_rs_cli`) | desktop shell + headless CLI |
| Pi bridge | `pi/` | external TypeScript adapter (ADR 0122), never compiled into the app |

## Current focus

- **Cartography Design System 4.0** (ADR 0130/0131): design tokens,
  component/template library expansion, MapSpec 2.0 compositional
  constraints, composition solver, preflight/repair rule catalog, and the
  deterministic visual-regression harness.

## Code layout

- Repository checkouts live under `~/projects/rs-studio/` (one primary
  checkout; epic work may use dedicated git worktrees, removed after merge).
- Build directory: `build/` inside the active checkout.
- Tests: `tests/` (Catch2 executables, discovered via CTest).
- Agent guidance: `CLAUDE.md`, `CONTEXT.md`, `.agents/skills/`, `docs/agents/`.

## Historical notes

The 2026-07 integration sprint (PRs #708–#712) completed: sequential
squash-merges, build verification, worktree/branch cleanup. Those sprint
tables were removed here — see git history and the ADR ledger for details.
