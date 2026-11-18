# R35-T06 — BASELINE

Snapshot taken at session start; re-read before every delivery step. Execution
base is not frozen.

## Repository

| Item | Value |
|---|---|
| Repo | `WindWang2/exp-rs` |
| Default branch | `master` |
| `origin/master` at start | `120677e6e165d3e88bb9d2ec2a3fd98042ec719d` |
| Open PRs | **0** (so no active exclusive paths; the six-track whole-file owners still apply) |
| Open issues | 106 (API; endpoints include PRs — filtered out) |
| Remote branches | 29 = master + 27 merged-retained + the superseded `#2138` branch |
| Prior local checkout | `/home/kevin/projects/rs-studio/main` @ `59edc79137` — **read-only**, untouched |

## Inherited baseline (already merged — not re-implemented here)

* **#2140** (this track's R33 delivery): preview request identity captured once,
  cache generation gate, `identityChanged` consumed by the detail pane (one
  bounded re-read), ±Inf/NaN/2e308 display rule, supersede/cancel abort flag,
  i18n pure-format-template classifier. **These are regression now.**
* #2142 / #2144 / #2145 / #2147: science, model/GPU, data/IO, execution+teaching
  backends — other tracks' files, untouched here.
* #2143 / #2139 / #2141 / #2146 / #2148: Agent/Mission, Windows paths, bilingual
  startup, deep-Chinese Python, macro-clone budget.

The R33 planning ledger is merged at `.planning/r33-t06-desktop-workflows/` and
states plainly that **WP06–WP18 were NOT_EXECUTED**, with H-03 (cartography
draft vs compiled), H-04 (multi-page export vs previous delivery), H-05 (history
truncation label), H-06 (Studio curve truth) and H-08 (preview file watching)
carrying file:line evidence. This track continues from there rather than
pretending the module is finished.

## This track's startup evidence (measured, this session)

| Item | Result |
|---|---|
| `git fetch` | master `120677e6`, 0 open PRs, 29 branches — matches the brief's snapshot |
| Fresh worktree checkout | **aborted twice** by the host: 9.8 MB then 140 MB at ~9 files/s (16 385 files), load average 6–9 from the other five tracks. Recorded as DECISIONS D-01. |
| Source delta used instead | `c1bf3eb2` (R33 tip) → `120677e6` = 155 files, of which `src/core` **0**, `src/gui` **0**, `src/geospatial` 2, `src/app/preview` **0**, `tests/CMakeLists.txt` changed (other tracks' appended blocks) |
| Consequence | the existing build dir is reused: qgis objects stay valid, only the 2 geospatial TUs, the changed app TUs and the 3 test binaries are rebuilt after one regeneration |

## Environment (unchanged from the R33 session, re-verified)

* `/tmp` is a **10 MB tmpfs** → every build must export
  `TMPDIR=$HOME/.cache/exp-rs-agent/tmp`.
* Configure needs `-DOpenCV_DIR=…/main/.scratch/ui-build-deps/opencv/usr/lib/cmake/opencv5`.
* `sicnu_discover_tests` registers **one CTest case per Catch2 `TEST_CASE`**,
  named by the case title, and filtering is case-sensitive.
* Qt 6.12, GDAL 3.13.3, CMake 4.5, Debug preset `dev-default`.
