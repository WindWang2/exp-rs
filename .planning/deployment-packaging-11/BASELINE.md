# BASELINE — Phase 0 audit (2026-09-16, worktree off origin/master @ a5b11b7f10)

## 1. Fresh origin facts (supersede the prompt-generation snapshot)

| Fact | Value | Evidence |
| --- | --- | --- |
| origin/master | `a5b11b7f10fa010c1c060864fb427d777ba9a4aa` ("fix: fail-closed fixes for review issues #994–#999 (#1000)") | `git rev-parse origin/master` after `git fetch origin --prune` |
| Prompt snapshot SHA | `ebcafb4d02` — **stale**, superseded | `git log` |
| PR #991 (D18 mission workbench) | **MERGED** into master (`c5d4aafe8e`) | `git log --decorate` |
| PR #992 (D19 foundry/benchmark) | **MERGED** into master (`1cea98921b`) | `git log --decorate` |
| Open PR #1009 | `zcode/execution-runtime-convergence-11` — execution runtime convergence (authority, chunk contract, crash-safe resume, resource governance, worker lease, telemetry, adoption kit). MERGEABLE, not draft. | `gh pr list` / `gh pr view 1009` |
| Open PR #1008 | `zcode/radiometric-spectral-workbench` — D13 radiometric/6S/spectral. **CONFLICTING** (with master), not draft. | `gh pr list` / `gh pr view 1008` |
| Open issues | #1001 (io:clip CRS override, P1 critical), #1002 (workflow registry executor fail-open, P1), #1003 (dataset join null columns, P1), #1004 (dataset:qa scan_capped, P1), #1005 (georef transform throw, P1 critical), #1006 (PipelineRunCoordinator syntheticExecute, P2), #1007 (dataset:qa CRS audit, P2). **All scientific-domain; none in packaging/deployment scope.** Owned by other tracks; untouched here. | `gh issue list` |
| Remote branches (recent) | `origin/zcode/execution-runtime-convergence-11`, `origin/zcode/radiometric-spectral-workbench` (the two open PRs); older merged branch heads also present. | `git branch -r --sort=-committerdate` |
| ISSUES.md | Old D3 operator-gap backlog (T-1..T-3, S-1..S-2, H-1..H-3, C-1..C-2). T-1/C-2 rows already fixed on master per CHANGELOG (`rs:temporal_monitor` scenes seam, `rs:temporal_extract_regions`). None concern packaging/deployment. Not used as backlog. | `sed -n '1,260p' ISSUES.md` |
| CHANGELOG.md head | Temporal Platform 10.0 + Data Fabric 10.0 entries; packaging entries from D7 era live further down. | `sed -n '1,60p' CHANGELOG.md` |
| docs/agents/goal-template.md | Read (320 lines). Requirements applied: slug three-way naming, .gitignore whitelist pattern + `git check-ignore -v` self-check, evidence policy (every claim = command + exit or `not-executed`), skills referenced only with full SKILL.md path after `ls` verification, PR runbook verbatim. | file read |

## 2. Existing packaging/deployment capability audit (subagent #1, read-only, 2.87M tokens)

Summary of the dense audit (full report retained in conversation log; facts below re-checked by main agent):

### Inventory (what exists at a5b11b7f10)

| File | Purpose | State |
| --- | --- | --- |
| `packaging/OFFLINE_BUNDLE.md` | Normative bundle contract; layout `sicnu-lab-<version>/`, manifest schema `sicnu.offline_bundle/1` (`schema/bundle_version/created_utc/size_ceiling_mb/required[]/files[]{path,bytes,sha256}`), builder contract 6 steps, zero-network rule | complete |
| `scripts/build_offline_bundle.sh` | POSIX assembler + verifier (`--build-dir --out --version --max-mb --skip-samples --verify`); verify = embedded python3 heredoc; sorted forward-slash paths; unsafe-path rejection; unlisted-file flag; required-prefix + ceiling checks; verdict line `BUNDLE VERIFY PASS/FAIL` | complete (Linux-exercised) |
| `scripts/build_offline_bundle.cmd` | Windows twin; `QGIS_BIN`/`SICNU_QGIS_BIN` hard requirement (`qgis_core.dll` sentinel before+after copy); windeployqt `--no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime`; copies `%QGIS_BIN%\*.dll` + `share/proj`,`share/gdal`; robocopy >=8 fails; manifest via `bundle_manifest.ps1` | complete (not CI-exercised) |
| `scripts/bundle_manifest.ps1` | PS 5.1 manifest writer/verifier (twin of .sh python block); PS 5.1 constraints honored (no GetRelativePath; `$script:BUNDLE_VERDICT`) | complete |
| `packaging/bundle/*` | In-bundle one-click surface: RUN.cmd (sets `SICNU_OFFLINE=1`, `PROJ_DATA`, `GDAL_DATA`, `QT_QPA_PLATFORM=offscreen`, PATH), GENERATE_SAMPLES.cmd, GRADE_ALL.cmd (CSV BOM+CRLF), VERIFY.cmd/VERIFY.ps1 (in-bundle integrity), README-zh.md, labs/lab1/*, labs/grading-overlay/ndvi_basics.rules.json | complete; **verify logic exists in 3 mirrored implementations** |
| `packaging/build-appimage.sh` | AppImage build; `make install DESTDIR=AppDir`; OTB from `/opt/otb`; **downloads linuxdeploy `-continuous` from GitHub at build time (network, non-reproducible)**; bundles only `proj.db` (duplicated if/elif); no manifest/verify/ceiling/RPATH strategy | partial / legacy |
| `scripts/offline_smoke.sh` | 5-phase acceptance smoke: bundle build → verify → in-bundle lab1 `--offline` (proxy vars unset, offscreen) → single grade w/ peak RSS (`getrusage`) → batch 60+1 corrupt (BOM/CRLF/61 rows/isolation/RSS ≤ 1.25×). **Never referenced by CI/ctest/docs — manual only** | complete but unwired |
| `scripts/install_deps.sh` | system deps (unpinned package lists; prose version guidance only) | partial |
| `scripts/windows/*` | `_env.cmd` (vswhere→vcvars, tool probes, hard bounds `-j2`, offscreen; **one hardwired user-profile default `C:\Users\wangj.KEVIN\...vcpkg_installed`**), setup.cmd, check_mcp.cmd(+ps1, MCP stdio round-trip), run_lab.cmd, grade_all.cmd | complete |
| `cmake/SicnuLabProfile.cmake` | `-DSICNU_LAB_PROFILE=ON` offline machine-room profile (OTB/ONNX/vendor-GDAL/python-bindings OFF), included from root CMakeLists | complete, wired |
| `cmake/Bundle.cmake`, `cmake/VcpkgToolchain.cmake`, `cmake/VcpkgInstallDeps.cmake`, `cmake/sicnu_otb_bundle.cmake` | QGIS-inherited/legacy — **not included anywhere (dead code)**; VcpkgInstallDeps is the historical Windows closure spec | dead code, do not resurrect |
| `cmake/SicnuTestEnv.cmake` | test env harness (offscreen, compose IM, LD/PYTHON pins) via `CTestCustom.cmake` | complete, wired |
| `CMakePresets.json` | v3; `dev-default` (Debug, tests ON, `build-dev`), ci-fast, ci-full, sanitizer-debug, release-package | complete |
| `data/help/diagnostics.json` | 104 curated diagnostic entries `{id: diagnostic.<family>.<code>, family, code, title/whatHappened/whyItMatters (zh), severity, retry, remediation[], keywords[], related[], category}`; qrc-compiled into both binaries; consumers `src/help/diagnostic_catalog.h`, `src/agent/harness/lab_diagnostics.h`, `src/contracts/graph_assembly.cpp` | complete |
| `docs/deployment/lab-offline.md` | zh operator runbook; matches implementation incl. exit codes/env vars | complete |
| `docs/verification/OFFLINE_POLICY.md` + ADR 0147/0153 | test-side offline policy (`SICNU_FORCE_OFFLINE`, `sicnu-skip:` + exit 77, loopback probe, proxy hygiene); product offline contracts | complete |

### Authority map

| Domain | Authority |
| --- | --- |
| Windows dependency versions | `vcpkg.json` (`builtin-baseline 7f3781e1…`, windows-filtered) + CI pins (same commit, Qt 6.8.0 msvc2022_64) |
| Linux dependency versions | **no single authority** (`install_deps.sh` unpinned; Find*.cmake; `vendor/` chain) |
| Bundle contents + manifest schema | `packaging/OFFLINE_BUNDLE.md` + the 3 mirrored implementations (`required[]` duplicated in .sh-python / .ps1 / doc) |
| Per-operator capabilities | `AlgorithmDescriptor` → `data/processing/algorithm_meta/*.json` (drift gate `tests/test_algorithm_meta_drift.cpp`) → D8 sidecars (`schema_version: 2`) → `pi/knowledge/capability-*.md` |
| Offline policy | `src/geospatial/remote/offline_gate.{h,cpp}` (runtime authority; `sicnu::data::offline` is the Qt facade) + ADR 0147/0153 |
| Diagnostic codes | code vocabularies (harness_error, RSOperatorError, GeoError) — `data/help/diagnostics.json` is curated prose, never invents codes |
| App version | **inconsistent**: CLI `0.9.2-dev` (`src/cli/main_cli.cpp`), GUI `1.0` (`src/app/main.cpp`), `vcpkg.json` `1.0.0` |
| Readiness reporting | `scripts/collect_readiness.py` schema `exp.readiness.report.v1` (build-tree evidence; no dep/DLL inventory) |

### Gap analysis vs work packages A–H (drives the plan)

- **A dependency inventory**: no machine-readable dependency/version/DLL-closure inventory anywhere. Windows closure = directory glob of `%QGIS_BIN%\*.dll` (no per-DLL provenance); Linux has no shared-library inventory. Seam: manifest `components` section + a dependency report script + runtime-side env doctor.
- **B deterministic manifest**: strong core (`/1`); missing license/build-options/components/compat fields; `created_utc` breaks byte-reproducibility (by design); 3 mirrored verifier implementations with no conformance gate; no schema negotiation (string equality only).
- **C Windows offline**: build-side solid; **no first-run environment check on the target machine** (missing-DLL-at-launch surfaces as loader dialogs); bundle never exercised on a Windows host in CI; `_env.cmd` hardwired user-profile default.
- **D Linux packaging**: AppImage needs network + is unverifiable; no RPATH strategy; the D7 lab bundle is the de-facto portable tree but has **no in-bundle Linux integrity check** (VERIFY is .cmd/.ps1 only).
- **E first-run diagnostics**: **no environment self-check exists at all** (`doctor` subcommands are per-dataset `data doctor` and plugin-doctor). Nobody enumerates GDAL drivers, verifies proj.db, checks Qt platform plugin/SSL/temp/permissions at startup or on demand. Note: `gdalCapabilityMatrix()` (9.0 M9, `src/geospatial/doctor/data_doctor.h:74`) already provides GDAL version + driver capability introspection — reusable, do not duplicate.
- **F offline guarantee**: CLI-path strong (`offline_gate` + typed refusals + GDAL deny + env inheritance + tests). Gaps: GUI `--offline` flag absent (env only) — GUI is read-only scope → documented, not fixed; no auto-update code exists (verified by grep — nothing to remove, evidence recorded).
- **G upgrade/migration**: no version-to-version migration story, no compatibility declaration in the manifest, no forward-schema refusal. In-repo pattern to mirror: `src/experiment/reproduction_bundle.{h,cpp}` (manifest + checksums + pinned refs).
- **H clean-machine simulation**: no container/isolated-env harness; `offline_smoke.sh` runs on the dev host; Windows bundle never smoke-run anywhere.

### Build environment facts (this host)

| Fact | Value |
| --- | --- |
| Tools | cmake, ninja, g++, python3, docker present; **pwsh absent** (Windows PS scripts: static checks only → execution marked not-executed, host limitation) |
| RAM / cores / load | 64 GB free-ish (10.5 GB used), 16 cores, load ~2.3 → hard cap `-j2` per envelope |
| Main build tree | `main/build-dev` = **Unix Makefiles Debug**, last built 2026-08-30, 14 GB; qgis_core built from `src/core` (982 cpp) — repo builds its own QGIS core; GDAL 3.13.3 / PROJ 9.8.1 / Qt ≥6.8 / qgis 4.2.2 system packages |
| Incremental delta | `make -n sicnu_geo_rs_cli` from the stale tree → **192 compile steps** to reach a5b11b7f10 → tractable at `-j2` |
| ccache | present but **empty** (no warm cache anywhere); no sibling worktree has a built CLI |
| Consequence | A fresh preset build in the worktree would rebuild qgis_core + all libs (≈2800 cpp) at `-j2` — days. Decision D1 (DECISIONS.md): copy `build-dev` into the worktree, reconfigure against worktree sources, sync mtimes so only master-drift + track files rebuild. Main `build-dev` left untouched. |
