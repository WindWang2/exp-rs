# R35-T06 — ACCEPTANCE

Two sections, because this session has two different evidence levels: what was
verified, and what the host refused to let me verify.

## A. Verified — translation freshness (WP19)

All numbers produced on this worktree at `45b43c70` (base `120677e6`).

| Step | Command | Result |
|---|---|---|
| Before | `python3 scripts/check_translation_freshness.py` | **`FRESHNESS GATE: FAIL (5)`**, 7 s, 5 missing messages, all QGIS-vendored |
| Root cause per string | read `src/core/qgsabstractcontentcache.h:236`, `src/gui/qgsfieldmappingmodel.cpp:648-653`, `src/gui/qgsfieldmappingmodel.h:178` | 1 genuine (`Field Mapping` under a stale nested context, runtime context is the enclosing QObject) + 4 template extraction aliases whose runtime context is the `*Base` QObject |
| After | same command | **`FRESHNESS GATE: PASS (unfinished=0<=0, new messages=0)`**, 7 s; `tracked .ts: 23995`, `regenerated: 23982`, 13 stale WARN (identical to base) |
| TS compiles | `lrelease6 resources/translations/sicnu_zh_CN.ts -qm …` | `Generated 24029 translation(s) (24029 finished and 0 unfinished)`, rc=0 |
| Translation ships | `lconvert6` round-trip of the `.qm` back to `.ts` | `QgsFieldMappingModel / Field Mapping → 字段映射` present; both alias contexts present |
| Canonical gate | `audit_i18n_strings.py --files … --gate … --selftest` | `SELFTEST PASS` / `GATE PASS` — unchanged (no C++ literal added) |
| Ownership | `git diff --name-only 120677e6..HEAD` | only `resources/translations/sicnu_zh_CN.ts` (+31/−0), `.gitignore` (append), `.planning/r35-t06-*` |

Independent review R1 verdict: **real fix, not green-washing.** It re-derived the
runtime context from the C++ (the nested `ExpressionContextGenerator` has no
`tr()`; its only base, `QgsExpressionContextGenerator`, is a plain interface), ran
the project's own `lupdate` invocation on a copy (gate still PASS afterwards,
only the hand-written `extracomment`s are dropped), checked all 1582 `<name>`
entries are unique, and confirmed the alias set cannot mask a missing runtime
translation because the gate independently requires the `*Base` keys.

Its four P3 findings are dispositioned in REVIEW_R1.md; three are documentation
durability notes and one (`src/app/application_language.cpp:14` naming the wrong
lupdate context) is handed over as H-10 because it is not this track's file.

## B. Not verified — preview lifecycle (WP02/WP03), and why

The code exists, is syntax-clean, and is **not** in this PR. It is on
`zcode/r35-t06-preview-lifecycle-wip` @ `7b926fa6c8` with an explicit
do-not-merge-without note.

| Attempt | Command | Outcome |
|---|---|---|
| Syntax | `g++ -fsyntax-only` with the configure's own flags for the 3 TUs | rc=0 (`service.cpp`, `test_r35_t06_preview_lifecycle.cpp`, and the R33 test) |
| Configure ×4 | `cmake --preset dev-default -DOpenCV_DIR=…` | (1) CMake Error `Cannot open file for write …/depend.make.tmp…: Operation not permitted`; (2) same; (3) generation ran 31 min without finishing, killed by me; (4) generation ran **82 min** writing ~1200 files/5 min, killed — never reached `Generating done` |
| Build | `cmake --build … --target test_r35_t06_preview_lifecycle` | `No rule to make target` — the target exists only after a completed generation |
| Environment probes | minimal CMake C++ project in `$HOME/.cache` | configure **and** build **and** link rc=0 → the toolchain is fine |
| Filesystem probes | create / `chmod` / `chown` / `xattr` / `rename` in `$HOME`, `/tmp`, the worktree, the build dir | all OK — **except `utimensat`, which returns `EPERM` for every path** |

So: not a toolchain problem, not a disk/inode problem (5 % inodes, 509 GB free),
not a repo problem — a host that is simultaneously running five sibling tracks
(load average 6–17) and cannot finish a 15 k-file generation inside any sane
window. `HANDOFF.md` H-09 records it for the next session, together with the two
other environment surprises (worktree checkout rate ~9 files/s; `resource_guard`
locks are unverifiable across background PID namespaces).

**Consequence, stated plainly:** WP02/WP03 are `BLOCKED`, not `DONE`. No claim in
this PR rests on them.

## C. Not attempted

Visible desktop (EN/zh_CN, light/dark, DPI, keyboard), QM loading from an
installed package, Windows packaging, and E2E-02…E2E-05 — all `NOT_EXECUTED`,
with the target each one needs listed in PLAN.md.
