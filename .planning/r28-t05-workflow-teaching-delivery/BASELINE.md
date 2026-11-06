# BASELINE — R28-T05 workflow/experiment/teaching delivery

## Repository facts (measured 2026-11-04T21:07Z / local 2026-11-05)

- Repo: `WindWang2/exp-rs` (origin, https). Worktree: `exp-rs-r28-t05-workflow-teaching-delivery-20261104T210720Z`
- Branch: `agent/r28-t05-workflow-teaching-delivery-20261104T210720Z`
- BASE_SHA: `50cc3255c5eb30cd247d7c065026607f36f90f03` (= origin/master at task start; re-verified by `git fetch origin --prune` under the shared git-metadata lock at 2026-11-04T21:07Z — no new commits since the R28 batch snapshot)
- Baseline commit: "Merge pull request #1812 from WindWang2/fix/swarm-review-2026-11-04"
- gh auth: account WindWang2, authenticated (keyring)

## Machine / toolchain

- Host: cachyos-3080, Linux 6.18.55-2-cachyos-lts x86_64, 16 cores, 62 GiB RAM (≈41 GiB available at start), 681 GiB free disk on /home
- Compilers: /usr/sbin/c++ (system GCC), CMake presets available (schema v3)
- Qt6: /usr/lib/cmake/Qt6 (system); GDAL: /usr/lib/cmake/gdal (system)
- OpenCV: **OpenCV 5 local prefix at `/home/kevin/.cache/exp-rs-agent/opencv5-prefix`** (`lib/cmake/opencv5/OpenCVConfig.cmake`) — required for the snapshot regeneration that #1812 declared blocked; will pass `-DOpenCV_DIR=...` at configure
- OTB vendored: OFF; embedded Python: OFF (mirrors main worktree build-dev profile)
- ccache present (45% hit rate); builds additionally go through `scripts/dev/resource_guard.py` with host lock `$HOME/.cache/exp-rs-agent/host-build` (acquire order: host lock → build-dir lock), JOBS=2 cap per the -j4-max contract (starting conservative at 2)

## Parallel-work facts (avoid-collision gate)

- Only ONE open PR: **#1813** "fix(ui): workbench UI polish pass" (head `fix/ui-review-2026-11-05`) — T04/UI territory but it DOES touch `tests/CMakeLists.txt` and several test files; re-fetch before PR.
- All 21 pushed `agent/*` branches are 0-ahead of master (fully merged; R23–R27 work landed).
- R28 tracks T01/T02/T03 have **local worktrees but unpushed branches** (t01-science, t02-data-runtime, t03-harness-sdk; a t04 worktree appeared during recon). Their in-flight edits are INVISIBLE to this track — treated as unknown parallel work; re-check at merge time.
- The main user worktree (`rs-studio/main`, branch `fix/ui-review-2026-11-05`) has uncommitted UI edits — read-only for us, never touched.

## Prior-round claims that must be re-verified (not trusted as measurements)

- #1812 PR body: fast CTest 77 failures / 9,776 at merge time; remaining clusters named: TaskCenter/ExecutionPlane cancellation (T02-owned), surface/contract snapshot drift (needs real OpenCV toolchain), cartography atlas deliveries, single-test expectation drift tail. These numbers are the PR's own record — this track re-measures.
- #1809 (merged 2026-11-03) touched: workflow placeholder grammar, experiment evaluation/benchmark/lineage/repeat_execution, reproduction_bundle_import, grader engine/matcher/types, task_center, teaching admin script adapters, offline bundle script, appimage packaging.
- #1812 additionally touched: experiment benchmark_compare/definition/runner, reproduction_bundle_import, run_bridge, run_recorder, workflow_session_controller, rs_pipeline_runner, agent_session snapshot description, contract graph expectation list, SQLite target alias unblock.

## Verification entry points

- Build wrapper: `scripts/build.sh` (self-doc; -j1 default, cap -j2, offscreen ctest -j1)
- CMake presets: `dev-default` (Debug+tests, build-dev), `ci-fast` (Release+tests), `offline-lab` (lab profile)
- Merge integrity: `scripts/check_merge_registration_integrity.py` (read --help before use)
- Resource guard: `scripts/dev/resource_guard.py --lock-dir D [--wait N] -- <cmd>`
