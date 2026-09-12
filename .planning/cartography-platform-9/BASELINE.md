# BASELINE — Cartography Platform 9.0 (recorded 2026-09-12)

## Repository state

- Base: `origin/master` @ `f316dfdbb47e39ccae045b202d7d38606f6c1b0e`
  ("fix(issues): resolve all 30 P1/P2 issues (#853-#882)", 2026-09-12).
- Local `master` untouched; all work on worktree
  `/home/kevin/projects/rs-studio/exp-rs-cartography-platform-9`, branch
  `feat/cartography-platform-9`.
- `git fetch --all --prune` done 2026-09-12.

## Recent merged PRs (8.0 series complete)

| PR | Title |
|----|-------|
| #847 | geospatial data fabric 8.0 |
| #846 | scientific processing 8.0 |
| #845 | professional workbench 8.0 |
| #844 | plugin platform 8.0 |
| #843 | dataset/experiment/mlops 8.0 |
| #842 | pi spatial scientist harness 8.0 |
| #841 | execution plane 8.0 |
| #840 | **cartography platform 8.0** (direct predecessor) |
| #839 | verification platform 8.0 |
| #837 | model runtime 8.0 |
| #832 | cartography platform 7.0 |

Post-8.0 master hardening (already in base): `8f6293bceb` (P0 defects:
terrain flow, selection context, vector writer, task center, data manager)
and `f316dfdbb4` (30 P1/P2 issues #853–#882, includes the cartography/
mapspec fixes listed in ISSUE_TRIAGE.md).

## Open PRs at baseline (parallel tracks — overlap check in OVERLAP_MAP.md)

- #883 feat(scientific): Scientific Algorithms 9.0
- #884 feat(models): AI Model Runtime & Multimodal EO Inference 9.0
- #885 feat(agent): Pi Spatial Scientist Harness 9.0
- #886 feat(data): Scientific MLOps / Reproducibility 9.0

None touches `src/agent/cartography/**` or `src/agent/mapspec/**` (verified
via `git log origin/master..` on each branch — see OVERLAP_MAP.md).

## Open issues at baseline

**0 open issues** (`gh issue list --state open` → empty). The five
cartography-relevant leads (#864/#865/#866/#867/#877) are all CLOSED with
fixes in the base commit — re-verified on the new master, not taken on
faith (ISSUE_TRIAGE.md).

## Remote branches

Historical `-5/-6/-7/-8` branches (`feat/cartography-platform-7/8` etc.) are
merged residue (PR numbers above); the four `*-9` branches are the open PRs
above. No unmerged independent cartography work exists. This track starts
fresh from `origin/master`; no historical branch is reused.

## Cartography capability state on the base (module sizes)

```
src/agent/mapspec/    mapspec.{h,cpp} 1319, mapspec_compiler 1094, mapspec_conditions 614
src/agent/cartography/ composition 1573, quality 1585, registry 1632,
    chart_registry 1299, cartography_tools 1636, style_spec 1155,
    style_compiler 760, solution_registry 687, design_tokens 472, typography 612
tests/  test_mapspec.cpp, test_cartography_{library,quality,templates,tokens,visual}.cpp,
        benchmark_quality7.cpp  (+ CTest selection "cartography", 60/63 pass
        reported by 8.0; 3 non-passes were unrelated _NOT_BUILT golden targets)
data/cartography/  components(57) templates(56) styles tokens index.json
docs/cartography/  16 docs incl. mapspec-reference (v5), preflight-rules,
    visual-regression, migration-v2..v5, atlas-guide
```

## Environment

Linux host, 16 CPUs, 62 GB RAM. Build: Ninja + Release, parallelism capped
at -j2..-j4 (shared host with parallel tracks); tests at -j1..-j2. System
QGIS + Qt 6 available (8.0 rendered PNG goldens on this class of host).
