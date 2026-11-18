# R35-T06 — REVIEW R1

Independent reviewer, no part in the implementation, given the base/head SHAs and
told to decide whether the freshness fix is real or green-washing. It was not
allowed to build (a configure was running) and verified by reading the C++, by
running both gates, and by running the project's own `lupdate` invocation on a
throw-away copy of the `.ts`.

## Verdict

> **Real fix, not green-washing.** `Field Mapping` is a genuine user-visible
> defect and the direction is right: … the nested class's only base
> `QgsExpressionContextGenerator` is a plain interface with no `Q_OBJECT` and no
> `Q_DECLARE_TR_FUNCTIONS`, so unqualified `tr()` resolves to the enclosing
> `QgsFieldMappingModel` (`qgsfieldmappingmodel.h:43-45` has `Q_OBJECT`). The
> runtime context is `QgsFieldMappingModel`. The other four are genuine
> extraction aliases … the runtime translation is still reachable and is not
> dropped by `-no-obsolete`. The gate goes green because the `(context, source)`
> keys lupdate actually emits were added, not because a threshold moved.

It also confirmed: all 1582 `<name>` entries unique, `xmllint` clean,
`git diff --check` clean, `lrelease` 24029/0 unfinished, `lconvert` round-trip
contains the new entry, the 13 stale WARNs are identical to base, and
`audit_i18n_strings.py` is unchanged (`SELFTEST PASS` / `GATE PASS`).
It could not run `test_bilingual_delivery_ui` (no build) — marked unproven, which
the PR body already states.

## Findings and disposition

| # | Sev | Finding | Disposition |
|---|---|---|---|
| 1 | P3 | A hand-written `<extracomment>` is not durable: the project's own `lupdate` run drops it (translations survive, gate still PASS). | **ACCEPTED, documented.** DECISIONS D-04 now says the durable record is the ledger, not the `.ts`, and notes the reviewer's measurement. No code change. |
| 2 | P3 | The four alias translations are never queried at runtime — pure gate reconciliation, duplicated maintenance. | **ACCEPTED.** Reviewer confirmed the gate independently requires the `*Base` keys, so the aliases cannot hide a missing runtime translation. The alternative (adding `Q_DECLARE_TR_FUNCTIONS` to the templates) would change runtime context in shared QGIS headers — out of this track's scope; recorded in H-02. |
| 3 | P3 | `src/app/application_language.cpp:14` still says lupdate attributes these calls to `T`; it now emits the template class name. | **HANDED OVER (H-10).** Not this track's file; low risk; fix on next touch. |
| 4 | P3 | `PATH_OWNERSHIP.md` asserted "T06 (global TS owner)"; the previous batch's ledger had the file as #2134-exclusive, and no batch-level authorization document exists ("unproven"). | **FIXED (doc).** The table now states the actual basis: #2134 merged (`a060fa3d2c` is an ancestor of the base), the claim therefore ended, and the diff is additions only. |

## What the reviewer could not settle

* `test_bilingual_delivery_ui` (the other half of issue #2081) — that test is
  T05's and needs a build. The PR deliberately claims only the freshness half.
* Whether the repository has a batch-level document granting T06 the global TS —
  it could not find one offline. The ownership row now rests on the merged-claim
  fact rather than on an unsourced title.
