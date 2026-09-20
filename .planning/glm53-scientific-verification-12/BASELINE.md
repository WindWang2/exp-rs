# BASELINE — glm53-scientific-verification-12 (Phase 0 live refresh)

> Track: `glm53-scientific-verification-12` (GLM-5.3 Track 02)
> Refreshed live at session start. **This snapshot is not a standing fact** —
> every gate below is re-verified against the live remote before PR.

## Repository state (live)

| Item | Value | How obtained |
|---|---|---|
| Repo | `https://github.com/WindWang2/exp-rs` | `git remote -v` |
| Live `origin/master` | `adf8f98952422fe9c386c56d64d5fb6a4a6642f1` | `git ls-remote origin refs/heads/master` |
| Local `master` | `adf8f9895` | `git rev-parse master` |
| ahead/behind | `0 / 0` | `git rev-list --left-right --count` |
| Working tree (main repo) | dirty (untracked planning/scratch + `M tests/test_workspace_catalog.cpp`) | `git status --short` |
| **Clone type** | **SHALLOW** (`.git/shallow`, 10 boundary commits) | `git rev-parse --is-shallow-repository` |
| Open PRs | **0** | `gh pr list --state=open` |
| Open issues | **0** | `gh issue list --state=open` |
| Closed issues tracked | 300 (page limit) | `gh issue list --state=closed --limit 300` |

### Prompt snapshot vs live

The prompt's snapshot SHA `2761a6857f2a879b29ec0fa70d7439c38c964deb` was
**already stale** at run time. Three commits landed after it:

```
adf8f9895 docs(agents): record Platform 5.0 audit request
fe7da0622 feat(agent): add StepFun preset provider profile
2761a6857 Merge pull request #1115 from WindWang2/fix/issues-1097-wave2
```

Per the track brief this is expected; **the live SHA above is authoritative**.

## Headline: the immediate predecessor is PR #1027

`#1027 feat(verification): F09 scientific contract & verification platform 11.0`
(merged 2026-09-16, +10082/−190, 51 files) is the direct predecessor of this
track. **12.0 is an increment ON TOP of 11.0, never a re-do.** What 11.0
already shipped (do not duplicate):

| 11.0 capability | Location |
|---|---|
| Determinism & contract census (source projection) | `src/contracts/determinism_census.{h,cpp}` |
| Census byte-snapshot gate | `data/contracts/determinism_census.snap.json` |
| Contract-or-exemption over live registry | `data/contracts/contract_exemptions.json`, `tests/test_contract_census_11.cpp` |
| Cross-family replay determinism corpus | `tests/test_contract_determinism_11.cpp` |
| 6-relation metamorphic oracle | `tests/test_verification_metamorphic_11.cpp` |
| Long-double + analytic numeric reference | `tests/test_verification_numeric_reference_11.cpp` |
| Mutation kill (10 mutants) | `tests/test_mutation_kill_11.cpp` |
| Failure/cancel/atomic lane (F1–F5) | `tests/test_verification_failure_11.cpp` |
| Cross-surface welding (help/agent/graph) | `tests/test_contract_cross_surface_11.cpp` |
| Capability-aware ladder | `scripts/verification_ladder.py`, `docs/verification/READINESS.{md,json}` |

11.0 also recorded a **known-limitations debt list** (see PR #1027 body) which
is the natural seed for 12.0 — but 12.0 must find its own evidence, not
inherit claims.

## Recent merged wave (14 fail-closed PRs, all 2026-09-19)

`#1098–#1115` landed a large fail-closed / concurrency / lifetime /
HTTP+fabric / workflow-resume / plugin-trust / data-transaction / CI-portability
wave. Relevant to this track: these PRs *hardened behaviour* that the
verification platform is supposed to *prove*. Several closed issues are
exactly verification-shaped:

| Issue | Title (abridged) | Relevance to 12.0 |
|---|---|---|
| #1097 | Deep-review investigation targets, wave 2 (confidence 55–70) | residual defect classes = oracle targets |
| #1096 | Lab report writer ignores `QFile::write` → truncated report reported as success | **failure lane**: false-success class |
| #1092 | `httpFetch` `truncated` flag is dead code — clamped body never reports truncation | **failure lane**: dead safety flag |
| #1091 | spectral_unmixing leaves truncated output on failure; NoData only band 1 | **failure lane**: partial artifact |
| #1088 | sample-data launchers pass positional out-dir the CLI rejects | cross-surface contract drift |
| #1077 | lineage signature ignores port wiring → wrong artifacts as cache hits | provenance/determinism semantics |
| #1076 | EVI/SAVI scale probe scans stale cross-scene buffer tail | **numeric oracle** class |
| #1071 (ref) | verification rollback never invoked on production paths | atomicity lane |

**Conclusion**: the class of defect 12.0 must systemically catch is
"silent false success / dead safety flag / partial artifact declared
complete" — and the 11.0 failure lane (F1–F5) covers only a subset
(corrupt, missing, bad params, pre-set cancel, missing output dir).

## Pre-existing master failures (recorded, NOT caused by this track)

From `docs/verification/READINESS.md` (generated 2026-09-16 @ `d2868c7`):

- `test_contract_platform_9` — failed
- `test_contract_projection_9` — failed
- `test_command_contract_9` — failed
- `test_diagnostics_contract_9` — failed
- `test_drift_projection_10` — failed
- `test_contract_fuzz_ipc` — timeout (Windows named-pipe emulation)

These are **pre-existing** and outside this track's owner scope. 12.0 must
re-measure them live and record them honestly rather than absorb or hide them.

## Remote branches that still exist (historical evidence only)

`agent/ds41-http-fetch-strict`, `agent/ds41-pipeline-drag-lifetime`,
`agent/flash-*` (7), `agent/glm53-desktop-lifecycle`,
`agent/glm53-plugin-sdk-trust`, `fix/ci-master-unblock`,
`fix/r2-ci-protobuf-multimode`, `fix/review-issues-1033-1056`.

Plus local-only branches restored from reflog during this session (see
`DEDUP.md`): `agent/glm53-mission-workbench-12` (a **concurrent live 12.0
sibling track**), `agent/ds41-*`, `agent/glm53-*`.

**None may be used as a development baseline.** They are read-only evidence.

## Environment facts (host)

| Item | Value |
|---|---|
| Host | Windows, MSVC 19.38 (VS 2022 Community 14.38.33130) |
| Generator | Ninja |
| CMake | `C:\Qt\Tools\CMake_64\bin\cmake.exe` |
| Qt | `C:\deps\Qt\6.8.0\msvc2022_64` |
| vcpkg | `C:\deps\vcpkg` (gdal/geos/proj/protobuf; **debug** variants in tree) |
| Warm build dir | `C:\Users\wangj.KEVIN\projects\exp-rs\build-dev` (54 GB, 682 binaries, `CMAKE_HOME_DIRECTORY` = **main repo**) |
| Runtime DLL search path (required) | Qt bin + `vcpkg/installed/x64-windows/debug/bin` + `vcpkg/installed/x64-windows/bin` + qca + kc + build dir |
| Test platform | `QT_QPA_PLATFORM=offscreen` |

### Repo health incident found & repaired this session

The shared `.git` was **corrupted**: `.git/refs/heads/agent/` directory was
missing while `.git/logs/refs/heads/agent/*` reflogs survived, so
`git branch <name>` **exited 0 but created no resolvable ref**, and
`git worktree add` failed with `invalid reference`. Additionally
`.git/info/refs` was stale (`origin/master = 007e70cff`, 10 commits behind
live). Root cause: an interrupted `worktree add` in a *different* concurrent
session plus a wiped refs subdirectory.

Repaired by: recreating `.git/refs/heads/agent/` and reconstituting every
orphaned ref from its reflog last-line. Verified `git rev-parse` resolves.
**Recorded as a host-level hazard** — other parallel sessions can hit it.

## Gate commands (canonical, used by every round)

```sh
# runtime env (required before running any test exe)
export PATH="/c/Program Files/Git/usr/bin:/c/Program Files/Git/cmd:$PATH"
export PATH="/c/deps/Qt/6.8.0/msvc2022_64/bin:/c/deps/vcpkg/installed/x64-windows/debug/bin:/c/deps/vcpkg/installed/x64-windows/bin:/c/deps/qca-install/bin:/c/deps/kc-install/bin:$BUILD:$PATH"
export QT_QPA_PLATFORM=offscreen

cmake --build <build-dir> --parallel 1     # NEVER -j3+
ctest --test-dir <build-dir> -j1 --output-on-failure
```
