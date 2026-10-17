# R16-C Risk Register

| # | Risk | Likelihood | Impact | Mitigation (verified working) |
|---|---|---|---|---|
| R1 | Parallel charters (R16-A..E) saturate 40-core box; -j4 cap violated system-wide | medium | build failures/OOM | shared `flock /home/kevin/project/exp-rs-build.lock` lease around every expensive build; `CMAKE_BUILD_PARALLEL_LEVEL=4`; ctest `-j1`; 62 GB RAM monitored |
| R2 | tests/CMakeLists.txt merge friction with #1515 | medium | PR conflict | append-only registration at file end; re-diff #1515 before push; no rewrites of existing blocks |
| R3 | Re-fixing already-hardened seams (R6/R8/R14-E) → churn masquerading as work | high (charter history) | review noise, regressions | dedup matrix (ISSUE_PR_BRANCH_MATRIX.md); every fix needs a reproduced RED on pristine master first |
| R4 | Windows-only behaviors unverifiable on Linux | certain | untested paths | simulate where honest (path shapes, quoting, exit-code contracts via fake CLIs); document what could NOT be verified; no fake "Windows tested" claims |
| R5 | Full-project build cost (QGIS fork) | high | timeouts | targeted test-target builds only (validated: 3 targets ≈ 4 min under lease); never `--target all` |
| R6 | ctest not on PATH | certain | command failures | absolute path `/home/kevin/.local/opt/cmake-3.30.5-linux-x86_64/bin/ctest` in every brief |
| R7 | Concurrent GitHub pushes from parallel charters | low | push race | unique branch names `hardening/r16-c-*`; retry with backoff on push |
| R8 | Master moves during the round | medium | stale base | re-fetch before push; rebase integration branch if needed and rerun targeted oracles |
| R9 | Token-burn idling (charter's anti-goal) | — | — | every work package has an executable oracle; 3-stale-attempt rule forces re-scoping, not repetition |
