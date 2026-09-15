# EVIDENCE — temporal-intelligence-11

Append-only. Every capability claim links to a command + exit code from this worktree.

## Phase 0

- 2026-09-15 `git fetch origin --prune` → origin/master `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`.
- `gh pr list --state open` → only #1008 (spectral/radiometric; no temporal file overlap; shared CMake/.gitignore only).
- `gh issue list --state open` → #1001–#1007, all dataset/workflow/georef/io/agent; zero temporal scope. OUT_OF_SCOPE for this track (recorded, not acted on).
- Read-only audit of all temporal kernels/operators/tests/docs on origin/master (5.6M-token Explore agent, read-only) → BASELINE.md.
- Concurrent local branches checked: `git diff --name-only origin/master...<branch>` per branch → only `.gitignore` shared; PARALLEL_OWNERSHIP.md.
- Configure (background, `configure-ti11.cmd`, exit 0 pending verify): preset-equivalent flags + `CMAKE_PREFIX_PATH=C:/deps/Qt/6.8.0/msvc2022_64;C:/deps/qca-install;C:/deps/kc-install` + vcpkg toolchain + winflexbison. First attempt failed (Qt6 missing → prefix path), second (BISON missing → `C:/deps/winflexbison`); both environment-discovery issues, not code.

## OUT_OF_SCOPE findings

- Issues #1001–#1007 (dataset/workflow/georef/io/agent fail-open residuals): real but owned by other tracks; not touched.
- Two coexisting BFAST-like kernels (`temporal_change` wired, D16 `breakpoint_detection` library-only): consolidation is a cross-lineage refactor beyond this track's ownership; D-TI11-1 records the reasoning; PR_BODY follow-up.

## not-executed items (with reason)

- (none yet)
