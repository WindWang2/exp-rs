# R35-T06 — translation freshness: one real gap, four extraction aliases

## Problem

`python3 scripts/check_translation_freshness.py` **fails on `master`**
(`FRESHNESS GATE: FAIL (5)`), which is the remaining half of issue #2081. Five
user-visible QGIS strings were reported as untranslated. They are not one kind of
problem:

| Context lupdate emits | Source | What it actually is |
|---|---|---|
| `QgsFieldMappingModel` | `Field Mapping` | **A real gap.** The tracked entry lives under `QgsFieldMappingModel::ExpressionContextGenerator`, which the gate lists as stale. The call site (`src/gui/qgsfieldmappingmodel.cpp:653`) is inside the nested `ExpressionContextGenerator`, which declares no `tr()` of its own, so C++ name lookup finds the enclosing `QgsFieldMappingModel` QObject's `tr`. Runtime context = `QgsFieldMappingModel`, and that context had no entry → the label showed English. |
| `QgsAbstractContentCache` | `%3 request failed …`, `%4 request error …` | **Extraction alias.** The code is `template<class T> class QgsAbstractContentCache : public QgsAbstractContentCacheBase` (`src/core/qgsabstractcontentcache.h:236`) with no `Q_DECLARE_TR_FUNCTIONS`, so runtime lookup goes through the inherited `QgsAbstractContentCacheBase::tr` — already translated in this file. |
| `QgsProjectStoredObjectManagerModel` | `Rename Layout`, `There is already a layout named “%1”.` | Same shape, with the tracked copy under `QgsProjectStoredObjectManagerModelBase`. |

The file already carried this alias convention (`T`, `*Base`, with
`extracomment`s explaining the runtime path); lupdate has since changed the name
it emits, so the alias set no longer covered what the gate sees.

## What changed

`resources/translations/sicnu_zh_CN.ts` only:

* `Field Mapping` → `字段映射` added under **`QgsFieldMappingModel`**, the context
  that is actually consulted at runtime.
* Two alias contexts (`QgsAbstractContentCache`, `QgsProjectStoredObjectManagerModel`)
  carry the four remaining messages with their translations and an
  `extracomment` naming the real runtime context, so the next person does not
  have to re-derive it.

No baseline, ceiling, audit file list or scan surface was touched.

## Verification

| Command | Before | After |
|---|---|---|
| `python3 scripts/check_translation_freshness.py` | `FRESHNESS GATE: FAIL (5)` | **`PASS (unfinished=0<=0, new messages=0)`** |
| `lrelease6 resources/translations/sicnu_zh_CN.ts -qm …` | — | 24 029 translations, **0 unfinished** |
| `lconvert6` round-trip of the produced `.qm` | — | `QgsFieldMappingModel / Field Mapping → 字段映射` present; both alias contexts present |
| `python3 scripts/audit_i18n_strings.py --gate --selftest` | `SELFTEST PASS / GATE PASS` | unchanged (`GATE PASS`) — this change adds no C++ literal |

The 13 "stale tracked entries" remain a `WARN`: they are messages lupdate no
longer extracts (including the old `QgsFieldMappingModel::ExpressionContextGenerator`
copy). Dropping them is a `-no-obsolete` decision, deliberately left alone here.

## Boundaries

* `resources/translations/sicnu_zh_CN.ts` is this track's file; no other track's
  source, test or CMake block is touched.
* The `test_bilingual_delivery_ui` half of #2081 is **T05's test** and was not
  run — this PR must not be described as closing #2081 outright.

## Tracking

Refs #2081.
