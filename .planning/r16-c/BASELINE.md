# R16-C Baseline (field-verified 2026-10-17)

## Host adaptation (honest execution record)

The task charter references Google Antigravity `/teamwork-preview` + `/goal-loop`.
The actual host is **ZCode**, which has neither command. Per the charter's own
fallback rule, the semantically equivalent native mechanism was used:

- **Teamwork preview → ZCode Agent tool**: two implementation sub-agents run in
  parallel, each in its own git worktree + branch (isolated indexes, isolated
  build dirs), followed by a separate integration agent step and independent
  reviewer sub-agents that did not implement the code.
- **Goal-loop → five-phase Oracle-driven loop** in this `.planning/r16-c/`
  tree (baseline → decomposition → RED/GREEN ×2 → adversarial review ×2 → PR),
  with per-track `GOAL_LOOP_LEDGER.md` oracles.

No `/teamwork-preview` or `/goal-loop` command output is fabricated anywhere.

## Repository baseline

- `origin/master` after `git fetch origin --prune`:
  **`46791bc65c6f552ebbcf723bb91892736d56f319`** (matches the 2026-10-16
  reference SHA in the charter; treated as moving target, re-verified at push
  time).
- Main worktree `/home/kevin/project/exp-rs` was clean before work started.
- Local worktrees pre-existing (untouched, not ours): main, `hardening-planner-repair-capability-graph`,
  `hardening-verifier-grader-explain-evidence`, `cpp-build-run-13`,
  `ds41-http-fetch-strict`, `rs14-exp-debugger`, plus R16-A's
  `exp-rs-r16-a-{t01,t02,integration}` (parallel charter, disjoint branches).
- Remote branches: 22 — all `hardening/r12..r15` PR residue plus
  `fix/all-open-issues`; none named `r16-c-*` before we created ours.

## Open PR snapshot (conflict matrix)

| PR | Branch | Files vs our tracks |
|---|---|---|
| #1513 (DRAFT) | r15-B geodata/persistence | no overlap with `src/teaching*` |
| #1514 | r15-A science | no overlap |
| #1515 | r15-C model runtime | **`tests/CMakeLists.txt`** (shared file — append-only registration discipline) |
| #1516 | r15-D pi/mcp | no overlap |

No open PR touches `src/teaching/**`, `src/teaching_admin/**`, `src/lab/**`,
`src/lab_pack/**`. Source-level conflict matrix is empty; the only shared file
is `tests/CMakeLists.txt` (and `.gitignore`), handled with minimal appends.

## Open issues touching this charter

Primary: **#1466** (teaching/suitability cluster). Status of its four claims on
current master (verified by reading the code, see `ISSUE_PR_BRANCH_MATRIX.md`):

1. "lab session file written non-atomically" — **already fixed** by R6/#1511
   (`lab_session_state.cpp` routes through `platform::sidecar` staged
   temp+fsync+atomic publish).
2. "discovery heuristics silently drop student files" — **already fixed** by
   R14-E/#1511 (`allEntries()` unfiltered listing + typed `DiscoverySkip`
   accounting, symlink/NFC/bounded-read hardening).
3. "grader resolution has no Windows python fallback" — R8 hardened
   `script_adapters` (test names confirm); T06 re-verifies remaining gaps.
4. "subprocess output captured unbounded" — T06 audits current state.

Adjacent: #1448 (checkpoint content validation — artifact stamp added by R14-E),
#1467/#1455/etc. are outside our file ownership. Old issues are treated as
leads only; nothing is changed without a reproduced defect.

## Build environment (validated end-to-end on this machine)

- Qt 6.12.0 (Arch system), GCC, OpenCV 5.0, jsoncpp 1.9.8, protobuf 36.1.
- Geo stack from local SDK prefix `/home/kevin/pwb-sdks/root/usr`
  (GDAL 3.13.3, PROJ 9.8.1, GEOS 3.15.0) — no network needed.
- Catch2 v3.7.1 offline source: `/home/kevin/.cache/exp-rs-fdeps/catch2-v3.7.1`
  via `-DFETCHCONTENT_SOURCE_DIR_CATCH2=...`.
- Configure command (validated, 42 s):

```bash
cmake -S <worktree> -B <worktree>/build-r16-c -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON \
  -DENABLE_LOCAL_BUILD_SHORTCUTS=ON \
  -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr \
  -DFETCHCONTENT_SOURCE_DIR_CATCH2=/home/kevin/.cache/exp-rs-fdeps/catch2-v3.7.1
```

- `ctest` binary is NOT on PATH; use
  `/home/kevin/.local/opt/cmake-3.30.5-linux-x86_64/bin/ctest`.
- Smoke validation on baseline master (T05 worktree): built
  `test_lab_runtime`, `test_teaching_admin_core`, `test_teaching_fake_grader_cli`
  (-j4, under lease, 286 steps) → `ctest -R "test_teaching_admin_core|lab_runtime" -j1`
  → **37/37 passed**.
- Parallelism discipline: `CMAKE_BUILD_PARALLEL_LEVEL=4`, `ctest -j1`,
  every expensive build wrapped in
  `flock -w 600 /home/kevin/project/exp-rs-build.lock ...` (shared with the
  sibling R16 charters' convention; the r14-era `/tmp/exp-rs-r14-global-build.lock`
  also exists and is honored when convenient).

## Worktrees / branches created by R16-C

| Worktree | Branch | From |
|---|---|---|
| `/home/kevin/project/exp-rs-r16-c-t05` | `hardening/r16-c-t05-teaching-session` | origin/master 46791bc65 |
| `/home/kevin/project/exp-rs-r16-c-t06` | `hardening/r16-c-t06-teaching-admin` | origin/master 46791bc65 |
| `/home/kevin/project/exp-rs-r16-c-integration` | `hardening/r16-c-teaching-classroom` | origin/master 46791bc65 |
