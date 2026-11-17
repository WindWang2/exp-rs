# R33-T06 — BASELINE

Snapshot taken at session start (2026-11-17). Execution base is **not** a frozen
baseline: the track re-reads `origin/master`, open PRs and branches before every
delivery step.

## Repository state

| Item | Value |
|---|---|
| Repo | `WindWang2/exp-rs` |
| Default branch | `master` |
| `origin/master` at start | `5ff15400047a6c55892669c9e9911e0e3b51bd04` (`fix(desktop): bilingual delivery and Windows atomic exports (#2132)`) |
| Previous local checkout | `/home/kevin/projects/rs-studio/main` @ `59edc79137` — **read-only**, 13 commits behind, never reset/cleaned |
| This worktree | `/home/kevin/projects/rs-studio/exp-rs-r33-t06-desktop-workflows-20261117T141119Z` |
| Branch | `zcode/r33-t06-desktop-workflows-20261117T141119Z` |
| Build dir | `<worktree>/build-dev` (Debug, `ENABLE_TESTS=ON`) |
| Open issues | 106 (API count at start; labels are not dynamic evidence) |
| Remote branches | 19 |

## Open PRs and their heads (refreshed at start)

| PR | Head | Files | State |
|---|---|---|---|
| #2134 R32-T05 agent dock/Pi/SDK | `f7b0ebb9c6ce` (moved past the `a6fbc9fc45` in the brief) | 43 | open, exclusive on `sicnu_zh_CN.ts`, `stage_windows_application.ps1`, `tests/CMakeLists.txt`, `.gitignore` |
| #2135 R32-T01 science | `5150d2d1c451` | 23 | open |
| #2136 R32-T03 data/IO | `57f99a4fcde8` | 11 | open (incl. `tests/CMakeLists.txt`) |
| #2137 R32-T02 models | `99262a739a97` | 11 | open (incl. `tests/CMakeLists.txt`) |

`tests/CMakeLists.txt` is touched by three open PRs; this track only appends one
unique target block at EOF (no reordering, no edits to existing blocks).

## Already merged — inherited, not re-implemented

* **#2132** (`5ff1540`): chart non-object style/nested-binding refusal, non-spatial
  symbology, ContextRules zh_CN, collection paging + tail reveal, real
  post-processing cancel, effective language + bilingual manual, Windows
  `QTemporaryFile` handle release, legend BSP.
* **#2133**: Windows UTF-16 workspace paths, four Base-context anchors, real
  Agent fixture.
* **#2131**: Native/Pi endpoint parity and real host verification.

`#2138` is closed-unmerged; its content was absorbed by #2132/#2133/#2131.

## Environment (measured, not assumed)

| Item | Value |
|---|---|
| CMake | 4.5.0 (preset schema v3 OK) |
| Qt | 6.12.0, `Qt6_DIR=/usr/lib/cmake/Qt6` |
| GDAL | 3.13.3 (`GDAL_DIR=/usr/lib/cmake/gdal`) |
| GEOS / PROJ | system cmake packages |
| OpenCV | **not on the system path** — must pass `-DOpenCV_DIR=/home/kevin/projects/rs-studio/main/.scratch/ui-build-deps/opencv/usr/lib/cmake/opencv5` (read-only shared prefix; the previous session's build used the same one) |
| Free disk | 653 GB at start |

Configure without `-DOpenCV_DIR` fails hard:
`src/processing/CMakeLists.txt:12 sicnu_processing requires OpenCV`. That is an
environment fact, not a product defect.

## Reproduction of the inherited i18n gate (dynamic, this HEAD)

```
python3 scripts/audit_i18n_strings.py --files tests/fixtures/i18n_audit_files.txt \
  --gate tests/fixtures/i18n_string_baseline.json --selftest
SELFTEST PASS
GATE FAIL: hard_en=1 (steady state must be 0; phase0 9)
GATE FAIL: nonui_zh=1095 > ceiling 1059
GATE FAIL: nonui_zh[src/science_context/capability_router.cpp]=66 > file cap 4
GATE FAIL: nonui_zh[src/teaching/lab_feedback_projection.cpp]=14 > file cap 13
GATE FAIL: nonui_zh[src/teaching/lab_session_state.cpp]=21 > file cap 15
```

* `hard_en=1` = `src/app/panels/data_manager_panel.cpp:1002`
  `QStringLiteral("%1 — %2")` — a **pure format template** (placeholders +
  separator, no words). Classifier false positive; owned and fixed by this track.
* `nonui_zh=1095` vs issue **#2108**'s measured `1088` — the +7 is exactly the
  seven bilingual pagination literals that #2132 added in
  `DataManagerPanel::addCollectionPagerRow`. They are intentional
  language-selected UI text, classified `nonui_zh` because the literal reaches
  `setText()` through a local `QString`, not directly.
* The three per-file cap overruns are `src/science_context/**` and
  `src/teaching/**` backend text — **T04's**, not this track's.
