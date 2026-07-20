# Repo Layout Reorganization (Approach A)

**Date:** 2026-07-19  
**Status:** Approved — implement  
**Scope:** Deep but conservative root cleanup; keep `itk_ref/` and `otb_ref/` at repo root.

## Goals

1. Clear root directory: only project entrypoints, build system, source, tests, and CMake-coupled trees.
2. Remove accidental tracking of build trees (`cmake-build/`).
3. Group docs, lab samples, design assets, and local reference trees consistently.
4. Update living code/scripts paths; leave historical plan text as-is unless it blocks builds.

## Non-goals (this pass)

- Moving `itk_ref/` or `otb_ref/` under `refs/` (subtree + CMake coupling).
- Merging `images/` into `resources/` (QGIS `images.qrc` surface area).
- Deleting local multi-GB `build*` trees (user may still need them).

## Target layout

```
exp-rs/
├── README.md, CLAUDE.md, CMakeLists.txt, CTestCustom.cmake
├── cmake/, cmake_templates/, packaging/, scripts/, tools/
├── src/, tests/, external/
├── images/, resources/
├── data/
│   ├── processing/, tools/, schemas/, pipelines/   # tracked configs
│   ├── samples/                                    # lab datasets (was samples_data/)
│   └── … large rasters (gitignore)
├── docs/
│   ├── design/          # DESIGN.md + former UI/
│   ├── architecture/    # DOCS_*, phase reports
│   ├── agent/           # task_plan, findings, progress, AGENT_TODO
│   ├── labs/, superpowers/
│   └── repo-layout.md
├── refs/
│   ├── qgis/            # was qgis_ref/ (local, gitignored)
│   └── boost/           # was boost_ref/ (local, gitignored)
├── itk_ref/, otb_ref/   # stay at root
├── vendor/              # source trees only (gdal/proj/geos/boost_sys)
└── build*/              # all gitignored
```

## Path mapping

| Before | After |
|--------|--------|
| `samples_data/` | `data/samples/` |
| `UI/` | `docs/design/ui/` |
| `DESIGN.md` | `docs/design/DESIGN.md` |
| `DOCS_*.md` | `docs/architecture/` |
| `task_plan.md`, `findings.md`, `progress.md`, `AGENT_TODO.md` | `docs/agent/` |
| `qgis_ref/` | `refs/qgis/` |
| `boost_ref/` | `refs/boost/` |

## Runtime path rules

- Lab samples: prefer `data/samples/`; keep legacy candidates for one release.
- Symbology XML: source from `refs/qgis/resources/`; install destination may remain `share/.../qgis_ref` for install layout compatibility; runtime resolver tries both.
- Icons: `resources/icons` symlink retargets to `docs/design/ui/svg-icons/icons`.

## Git hygiene

- `git rm -r --cached cmake-build` (keep local tree).
- Expand `.gitignore`: `cmake-build/`, `refs/`, root junk, vendor build pollution patterns.
- Do not commit multi-GB ref trees or vendor prefixes.

## Verification

- Root listing is readable (no agent md, no samples_data, no UI at top).
- `resources/icons` symlink valid.
- Path helpers resolve `data/samples`.
- CMake still configures with `refs/boost` candidate (OTB optional).
- No new untracked secrets; status shows intentional moves only.
