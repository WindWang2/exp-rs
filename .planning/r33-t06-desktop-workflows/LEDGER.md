# R33-T06 — LEDGER

One row per checkpoint: what ran, what it returned, where the evidence lives.
Logs stay local (`~/.cache/exp-rs-agent/logs/`); nothing here is a substitute for
them.

| # | Checkpoint | Command (abridged) | Exit / result | Evidence |
|---|---|---|---|---|
| 1 | Repository refresh | `git fetch origin --prune`, `gh api` pulls/issues/branches | master `5ff1540004`; 4 open PRs (#2134 `f7b0ebb9…`, #2135 `5150d2d1…`, #2136 `57f99a4f…`, #2137 `99262a73…`); 106 open issues; 19 branches | BASELINE.md, PR_BRANCH_MATRIX.md |
| 2 | Exclusive paths | `gh api repos/…/pulls/N/files` for N in 2134-2137 | read; `tests/CMakeLists.txt` claimed by 3 PRs, `.gitignore` + TS + Windows staging by #2134 | PATH_OWNERSHIP.md |
| 3 | Independent worktree | `git worktree add -b zcode/r33-t06-… <wt> 5ff1540004` | ok; branch + HEAD asserted against the worktree root | RESUME.md |
| 4 | First configure | `cmake --preset dev-default` | **failed**: `src/processing/CMakeLists.txt:12 sicnu_processing requires OpenCV` | BASELINE.md "Environment" |
| 5 | Configure with OpenCV | `-DOpenCV_DIR=…/opencv5` | ok, generation 1539 s | — |
| 6 | Reconfigure for the new target | same + new `tests/CMakeLists.txt` block | ok, generation 1198 s | — |
| 7 | First build attempt | `cmake --build … --target test_r33_t06_preview_identity` before regeneration | **failed**: no rule to make target (target added after generation) | — |
| 8 | Build with `/tmp` default | `cmake --build … --parallel 4` | **failed**: `error writing to /tmp/ccXXXX.s: No space left on device` — `/tmp` is a 10 MB tmpfs | RESUME.md landmine note |
| 9 | Build with `TMPDIR` on disk | `TMPDIR=…/exp-rs-agent/tmp cmake --build …` | killed at 27 min by the operator after the log showed the tmpfs cause; a stale build-dir lock remained | — |
| 10 | Reclaim the dead lock | `resource_guard.py --stale-after 1 …` (holder pid dead) | lock reclaimed; build restarted | RESUME.md |
| 11 | Clean build of both targets | `cmake --build … --parallel 4 --target test_r33_t06_preview_identity test_asset_preview_service` | **rc=0**, 3 h 19 m (qgis_core 1054 + qgis_gui 766 objects) | `logs/r33_build4.log` |
| 12 | First test run | `ctest -j1 -R 'r33_t06|asset_preview'` | 0 tests — CTest names are the Catch2 **case titles**, which start with `R33-T06` (case-sensitive), not the target name | — |
| 13 | Test run, correct filter | `ctest -j1 -R 'R33-T06'` | **13/14 passed**; 1 failure was the fixture's own `QFile::setFileTime()` returning false on this filesystem | `logs/r33_ctest_new.log` |
| 14 | Fixture fix + incremental rebuild | remove `setFileTime`, add a same-size/mtime case | build rc=0 (~1 min) | `logs/r33_build5.log` |
| 15 | Final test run | `ctest -j1 -R 'R33-T06'` / `-R 'preview'` | **15/15** and **6/6** passed | ACCEPTANCE.md |
| 16 | i18n canonical gate | `audit_i18n_strings.py --gate --selftest` | `SELFTEST PASS`; `hard_en` 1→0; three backend cap overruns remain (T04) | ACCEPTANCE.md |
| 17 | i18n freshness gate | `check_translation_freshness.py` | FAIL (5) — identical to base; a `tr()` caption was measured to add a 6th and was replaced by the bounded re-read | ACCEPTANCE.md |
| 18 | Independent reviews | two separate agents on `BASE..HEAD` | R1: 12 findings (1 P2); R2: 3 blocking items, all closed before delivery | REVIEW_R1.md, REVIEW_R2.md |

## Open at hand-off

* Build/system state: `build-dev` is complete for the two test targets only; the
  application target (`data_manager_panel.cpp`) is syntax-verified, not linked.
* No locks are held; the host build lock is released between commands.
* Nothing pushed, no PR opened, no merge — the delivery step is recorded below
  once the operator authorises it.
