# R33-T06 — ACCEPTANCE

Every number below was produced on this worktree at HEAD `accca584b8...` with the
commands shown. Nothing is inherited from a previous PR or a research probe.

## Build

| Item | Value |
|---|---|
| Configure | `cmake --preset dev-default -DOpenCV_DIR=…/opencv5` (Debug, `ENABLE_TESTS=ON`) — generation 1198 s |
| Build | `cmake --build build-dev --parallel 4 --target test_r33_t06_preview_identity test_asset_preview_service` — **rc=0**, 3 h 19 m wall (qgis_core 1054 objects + qgis_gui 766 objects at `-j4`) |
| TUs compiled from this HEAD | `src/app/preview/asset_preview_service.cpp`, `tests/test_r33_t06_preview_identity.cpp` |
| `src/app/panels/data_manager_panel.cpp` | compiled by `cmake --build`? **No** — it belongs to the application target, not to these two tests. It was verified with `-fsyntax-only` using the exact flags the configure recorded for that TU (rc=0). Its behaviour is **not** covered by a test; H-08 names `test_data_manager_panel` as the missing coverage. |

## Tests (real CTest cases, `sicnu_discover_tests`)

```
ctest --test-dir build-dev -j1 -R 'R33-T06'      -> 100% tests passed out of 15
ctest --test-dir build-dev -j1 -R 'preview'      -> 100% tests passed out of 6      (pre-existing target, regression)
```
`QT_QPA_PLATFORM=offscreen`, `-j1`, `TMPDIR` on real disk (see RESUME.md).

The 15 R33-T06 cases (each a Catch2 `TEST_CASE`, registered one-per-case):

| Case | What it proves |
|---|---|
| `+Inf/-Inf/NaN/NoData are display misses and do not widen the stretch` | finite range [10,100] survives an `+Inf` in the data; mid sample is exactly 128; the four non-displayable samples are black |
| `Float64 -Inf does not poison the byte conversion` | `-Inf` minimum no longer produces a NaN ratio |
| `all-missing and flat rasters stay honest` | all-NaN → black, still Ready; flat → 128 gray |
| `a legitimate 1e308 span still stretches` | `max - min` overflow path → 0/128/255 across 2e308 |
| `the NoData predicate narrows Float32 like the reader does` | `bandSentinelMatches(Float32, (double)0.1f)` true while `(double)0.1f != 0.1`; Float64 exact; NaN sentinel |
| `a non-finite sample in one RGB channel blackens the pixel` | per-channel mask + Infinity does not widen band 2's range (col 3 green = 255) |
| `a subnormal-only span still stretches` | the overflow fix did not regress a denormal span (0 / 255) |
| `each band masks with its own NoData` | band 3's sentinel blackens the pixel even though bands 1-2 have data |
| `a file replaced mid-render is never cached under the new identity` | A delivered as 64×48 with `identityChanged`, `cacheEntries()==0`, next request reads B (32×16) not from cache |
| `a same-size rewrite is invalidated by its timestamp` | same byte length → the mtime alone moves the identity, second request re-reads |
| `an unchanged file still caches at request identity` | no regression: second request is a synchronous cache hit, `pendingRequests()==0` |
| `Unicode paths and a deleted file keep identity honest` | Chinese directory + filename works; deletion → `identityChanged`, nothing cached, then typed `Failed` |
| `a selection burst leaves one pending request and one delivery` | 1000 requests, `pendingRequests() <= 1` on **every** iteration, exactly one delivery carrying the last requested size, one cache entry |
| `a canceled request neither delivers nor caches` | cancel before the worker runs → 0 callbacks, 0 cache; unknown tokens are no-ops |
| `destroying the service with work in flight is safe` | dtor with a queued worker, no crash, pool reusable, fresh service renders |

## i18n gates (kept separate on purpose)

| Gate | Result |
|---|---|
| `audit_i18n_strings.py --selftest` | **SELFTEST PASS** (now includes the pure-template positive/negative pair, the Cyrillic residue case and a printf-spelling assertion) |
| `audit_i18n_strings.py --gate` | `hard_en` **1 → 0**. Still failing: `nonui_zh=1095 > 1059`, `science_context/capability_router.cpp 66>4`, `teaching/lab_feedback_projection.cpp 14>13`, `teaching/lab_session_state.cpp 21>15` — **inherited**, three backend files owned by T04 (HANDOFF H-01). Baseline, ceilings and the scan list were not touched. |
| `check_translation_freshness.py` | **FAIL (5)** — identical to base (issue #2081). A `tr()` caption for the stale preview was implemented, measured to add a 6th failure, and replaced by the bounded re-read, which adds no string. |
| Fresh `QM` / visible desktop | **NOT_EXECUTED** — `sicnu_zh_CN.ts` is #2134-exclusive; no `lrelease`/native desktop run in this session. |

## What is not verified here (honest gaps)

* `data_manager_panel.cpp` behaviour (the one-retry re-read): compiled/syntax
  only. No `test_data_manager_panel` build, no native desktop run.
* E2E-02 … E2E-05 (cartography, atlas export, Studio, teaching, bilingual
  desktop): **NOT_EXECUTED**. Their static findings and exact file:line evidence
  are in PLAN.md / HANDOFF.md.
* `git diff --check` clean; no file outside the declared write set except
  `docs/ui-architecture.md` and the sanctioned `.gitignore` append
  (PATH_OWNERSHIP.md records both).
* Release configuration was not built; all numbers are Debug.
