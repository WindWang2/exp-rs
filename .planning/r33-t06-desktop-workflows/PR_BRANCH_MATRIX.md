# R33-T06 — BRANCH / PR MATRIX

Refreshed at session start (`git fetch origin --prune`, `gh api`). 19 remote
branches: `master` + 4 open-PR heads + 9 merged-retained + 1 closed-unmerged
retained + 4 vendor `codex/*` lines.

| Branch | Head | PR | ahead/behind vs master | Note |
|---|---|---|---|---|
| `master` | `5ff15400047a` | — | 0/0 | execution base |
| `zcode/r32-t05-agent-runtime-20261116T184011Z` | `f7b0ebb9c6ce` | **#2134 open** | moved past the `a6fbc9fc45` named in the brief | 43 files; holds TS + Windows staging |
| `zcode/r32-t01-science-20261116T153844Z` | `5150d2d1c451` | **#2135 open** | — | 23 files |
| `zcode/r32-t03-data-io-20261116T153931Z` | `57f99a4fcde8` | **#2136 open** | — | 11 files |
| `zcode/r32-t02-model-runtime-20261116T154018Z` | `99262a739a97` | **#2137 open** | — | 11 files |
| `zcode/r32-t06-desktop-experience-20261116T150731Z` | `9c7159d501ff` | merged (#2132) | retention | content absorbed; not re-done |
| `zcode/r30-t07-…` | `98a7b8c787d8` | merged | retention | |
| `zcode/r31-t01-…` | `a7377a05480f` | merged | retention | |
| `zcode/r31-t02-…` | `4176ba9228a4` | merged | retention | |
| `zcode/r31-t03-…` | `7dcea7e6805b` | merged | retention | |
| `codex/bilingual-ui-language` | `9170e8dee72b` | — | upstream vendor | not this track |
| `codex/fix-profile-language-startup` | `59e1866b5f80` | — | upstream vendor | |
| `codex/pi-agent-p0 … p5` | 6 heads | — | upstream vendor | |
| `codex/windows-offline-ndvi-acceptance` | `cb3e4a555fdb` | — | upstream vendor | |

No public `T04` branch and no unassociated new branch exists at start; that does
**not** prove another machine has no unpushed work (the brief's caveat).

Squash-merged branches still show `ahead` commits — that is retention, not
in-flight work.

## This track

| Item | Value |
|---|---|
| Branch | `zcode/r33-t06-desktop-workflows-20261117T141119Z` |
| Base at start | `master` @ `5ff15400047a6c55892669c9e9911e0e3b51bd04` |
| Base at delivery | `master` @ `1dfb04382e` (#2134/#2135/#2136/#2137/#2139 merged while this track ran; 0 open PRs remained, so no path was still exclusive) |
| Stacked on another PR | **no** |
| PR | **#2140** — https://github.com/WindWang2/exp-rs/pull/2140 (OPEN, base `master`, head `95945b9adbce`) |

### Effect of the merges on this track's reservations

* `resources/translations/sicnu_zh_CN.ts` and
  `scripts/stage_windows_application.ps1` are **free** again (#2134 merged).
  This track did not take them over: it adds no `tr()` source, so consolidation
  is a separate decision, recorded in HANDOFF H-02.
* `tests/CMakeLists.txt` and `.gitignore` conflicts were pure EOF appends and
  were resolved by keeping both blocks; the delivered diff adds lines only.
