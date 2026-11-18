# R35-T06 — REVIEW R2 (author's disposition log)

**This is not a second independent review.** No code changed after R1, so there
was nothing for a second reviewer to re-derive; calling the author's own notes
"R2" would mislabel self-review. What follows is the exact post-R1 delta and the
reason each item needed no re-review.

## Changes after R1 (all documentation)

| File | Change | Why no re-review of code |
|---|---|---|
| `DECISIONS.md` D-04 | added the durability caveat R1 finding 1 measured (the `extracomment`s are dropped by `lupdate`; the ledger is the durable record) | no executable behaviour |
| `PATH_OWNERSHIP.md` | replaced the unsourced "T06 (global TS owner)" with the actual basis (the #2134 claim ended when it merged; the diff is additions only) | ownership statement, not code |
| `HANDOFF.md` | added H-10 (`src/app/application_language.cpp:14` names an out-of-date lupdate context) | new handover row only |
| `PLAN.md`, `ACCEPTANCE.md` | WP02/WP03 reclassified `DONE` → **`BLOCKED`**, with the full attempt log | this is the honest correction R1's "could not build" caveat implied |
| `PR_BODY_I18N.md` | added the explicit "what is not in this PR" section | documentation |

## State of the evidence

* R1 re-ran both gates itself and re-derived the runtime context from the C++; it
  also ran the project's own `lupdate` on a copy and re-ran the gate afterwards
  (still PASS). Those measurements cover the final HEAD's content, because the
  only change since is prose in `.planning/`.
* Final HEAD re-check by the author: `check_translation_freshness.py` → **PASS**;
  `audit_i18n_strings.py --gate --selftest` → `SELFTEST PASS` / `GATE PASS`
  (`nonui_zh=1022`, all `hard_*`/`fmt_concat` 0); `git diff --check` clean;
  `lrelease` 24 029/0 unfinished.

## What a real R2 should look at, if one is commissioned

1. Whether `QgsProjectStoredObjectManagerModel` is a template in this tree (R1
   located it at `src/core/project/qgsprojectstoredobjectmanagermodel.h:36,100`
   and confirmed the runtime alias lands on `…Base`; the author had only the TS's
   own `extracomment` as evidence, so an independent confirmation is worth having).
2. Whether adding four never-queried translations is the maintenance cost the
   repository wants, versus teaching the gate about template aliases.
