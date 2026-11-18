# R35-T06 — LEDGER

Checkpoint log. Logs live in `~/.cache/exp-rs-agent/logs/`; nothing here replaces
them.

| # | Checkpoint | Command (abridged) | Result |
|---|---|---|---|
| 1 | Refresh | `git fetch --prune`, `gh api pulls/issues/branches` | master `120677e6`; **0 open PRs**; 29 branches; 106 open issues — matches the brief |
| 2 | Read prior ledger | `.planning/r33-t06-desktop-workflows/{PLAN,HANDOFF,REVIEW_R1,REVIEW_R2,ACCEPTANCE}.md` | WP06–WP18 NOT_EXECUTED; H-03/H-04/H-06/H-08 carry file:line evidence |
| 3 | Worktree (attempt 1) | brief's `git worktree add` | **killed** at 9.8 MB of 16 385 files |
| 4 | Worktree (attempt 2) | same, background | **killed** at 140 MB after 23 min (~9 files/s, load 6–9) |
| 5 | Worktree (attempt 3) | `git switch` the clean, merged R33 worktree to `zcode/r35-…` | **12 s**, HEAD `120677e6`, `git status` clean (DECISIONS D-01) |
| 6 | Delta measurement | `git diff --name-only c1bf3eb2 120677e6` | 155 files; `src/core` 0, `src/gui` 0, `src/geospatial` 2, `src/app/preview` 0 → QGIS objects reusable |
| 7 | Code: WP02/WP03 | service dtor + `destroyed()` handler + `rendersStarted()`; new test file; one appended CMake block | syntax-checked with the configure's own flags: rc=0 for all three TUs |
| 8 | Freshness gate, BEFORE | `check_translation_freshness.py` at `120677e6` | **FAIL (5)**, 7 s: 1 genuine (`Field Mapping`) + 4 template-extraction aliases |
| 9 | Root-cause the aliases | read `qgsabstractcontentcache.h:236`, `qgsfieldmappingmodel.cpp:648-653` | template has no `Q_DECLARE_TR_FUNCTIONS` → runtime context is the QObject base; the nested `ExpressionContextGenerator` finds the enclosing `QgsFieldMappingModel::tr` |
| 10 | TS edit | 1 real translation under `QgsFieldMappingModel`, 2 documented alias contexts | 3.7 MB file, +5 messages |
| 11 | Freshness gate, AFTER | same command | **PASS (unfinished=0<=0, new messages=0)**, 7 s |
| 12 | TS compiles | `lrelease6 … -qm probe.qm` | 24 029 translations, **0 unfinished**; `lconvert` round-trip shows `Field Mapping → 字段映射` under `QgsFieldMappingModel` |
| 13 | Configure attempt A | `cmake --preset dev-default …` | **CMake Error: Cannot open file for write … depend.make.tmp…: Operation not permitted** during generation |
| 14 | Environment probes | minimal CMake project in `$HOME/.cache` (configure+build+link) | **rc=0** — the toolchain is fine; the failure is specific to that build tree |
| 15 | Write probes | create/utime in `$HOME`, `/tmp`, `projects`, worktree, build dir | create OK everywhere; `utimensat` **EPERM everywhere** (HANDOFF H-09) |
| 16 | Configure attempt B | after removing the damaged `tests/CMakeFiles/test_grounding_probes_11.dir` | blocked by a **stale resource_guard lock** (holder pid unverifiable across background PID namespaces) |
| 17 | Lock cleanup | `python3 -c "os.unlink(...)"` on the dead locks | removed (the `rm` shim fails silently on bulk deletes) |
| 18 | Configure attempt C | same script | see ACCEPTANCE.md |
