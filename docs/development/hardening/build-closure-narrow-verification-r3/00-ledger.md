# Build Closure / Test Wiring / Narrow Verification R3 — Ledger

Branch: `hardening/r3-build-closure-narrow-verification-r3`
Base: `origin/master` `5697ca2adb8a46fb6454a2fdd88799cf78298a19` (fetched live at start;
the 2026-09-24 recon seed `3487b9ad` was superseded — master had moved through #1313).

## Goal closure

| Goal | Delivered |
| --- | --- |
| 1. Mechanical inventory of target graph / test embed graph / source ownership | `tests/test_build_wiring_drift.cpp` builds the wiring model; inventory run pre-implementation: **137 test targets embed 395 production sources** in `tests/CMakeLists.txt`; 648 targets carry link edges; 36 ALIAS targets |
| 2. Extend `test_build_wiring_drift` with statically provable consistency rules | Rules 9–13 (duplicate add_subdirectory, embed link requirements, companion bodies, Qt direct-include hygiene, AUTOMOC availability). Everything not provable is recorded as unknown and stays silent: include-without-use, header-mediated jsoncpp needs where transit exists, unresolved variables, ambiguous quoted-include resolution, body-less macro APIs |
| 3. Strengthen `narrow_targets.py` | Link graph + ALIAS resolution + set/list(APPEND) chain resolution + helper SOURCES-kwarg binding; changed source → owning lib → **transitive consumers** (test executables), with kind + verification classification (`compile+link` / `link+test`) and ctest suggestions restricted to harness-registered targets |
| 4. Real narrow-closure sampling on the 2026-09-24 modules | `src/verify/verify_engine.cpp` → `sicnu_verifier` (compile+link) + 9 verifier test executables (link+test); d17/workflow/teaching samples map with zero `unwired`; every suggested target resolves in the wiring text (no `all` fallback, no guessed targets) |
| 5. Move CI-only wiring faults to local gates | Rules 10–13 are exactly the #1298/#1301/#1302/#1303/#1304/#1306–#1313 incident classes; the mutations below reproduce them locally without any CI run |

## Oracle evidence

### Fixture kills (in-gate, synthetic trees)

`test_build_wiring_drift` carries per-rule mutation sections: every rule is
injected into a synthetic tree and must turn exactly that rule red, and must
stay green for the legal spelling (guarded / branch-separated / allowlisted /
include-without-use / forward-declared / borrowed-moc). 12 test cases,
65 assertions, all green.

### Live-tree incident mutations (real repo, all RED, then restored)

| Mutation (incident shape) | Rule that fires |
| --- | --- |
| Drop `Qt6::Network` from `test_workflow_checkpoint_cache` LIBS (#1302) | 10 |
| Drop `atomic_fs.cpp` from `test_workflow_durability_13` embeds (#1303/#1304) | 11 |
| Drop `workflow_run_lock.cpp` from the coordinator embed (#1301) | 11 |
| Drop `jsoncpp` from `test_workflow_durability_13` (#1298) | 10 |
| Duplicate `add_subdirectory(tools)` (#1276 class) | 9 |
| Remove a restored direct `#include <QJsonDocument>` (#1306–#1313 class) | 12 |
| Remove a restored `AUTOMOC ON` (vtable-ref class) | 13 |

Each run mutated only `tests/CMakeLists.txt` (or one test TU), recompiled the
gate, asserted the expected rule went red **and no other rule regressed
beyond baseline**, then restored from a byte copy of the file (never via
git, after an early `git checkout` restore cost the branch its uncommitted
work — checkpoints are now committed per slice).

## Wiring fixes driven by the new rules (all mechanical, test-only)

- 67 direct Qt includes across 43 test TUs — the exact class of
  #1306/#1308–#1313 (uses QJsonObject/QJsonValue/QJsonArray/QJsonParseError/
  QTemporaryDir/QNetworkRequest/QLocalServer/QLocalSocket/QLockFile/QSignalSpy
  without including them; green today only via transitive includes that
  change between Qt versions)
- `AUTOMOC ON` for 35 embed targets whose production TUs touch a Q_OBJECT
  metaobject (`emit` / `staticMetaObject` / `qobject_cast`) — the
  vtable-ref link-failure class; guarded per target with `if(TARGET …)`
  so offline lanes that skip optional dependencies still configure
- companion bodies embedded/linked for 6 embed targets
  (mission_stage / workbench_host / crs_selector / schema_form_builder /
  rs_product_import_plan / d17 engine sources) — the #1300–#1304 shape
- owning libraries linked where the companion is a whole tool registry
  (`sicnu_agent`, `sicnu_operators_core`) — embedding those would drag the
  entire family into hermetic tests

## Known limits (fail-open, by design)

- Rule 10 cannot judge whether an existing deep-closure transit for jsoncpp
  is PUBLIC-reliable (sicnu_link_jsoncpp exists because it sometimes is
  not). When the over-approximated closure contains jsoncpp the rule records
  unknown and stays silent; when even the over-approximation lacks it, the
  embed certainly fails to link and the rule fires.
- Rule 13's moc-need evidence is deliberately narrow (emit /
  staticMetaObject / qobject_cast with a Q_OBJECT stem header). Bare `tr(`
  is excluded (QObject::tr needs no own moc) and bare pointer includes of
  Q_OBJECT headers link fine — the broader spelling was a false-red factory
  against CI-green targets.
- Rule 11's demandable-identifier extraction is conservative (declared-not-
  defined functions; constructed classes). It cannot see pure symbol-level
  needs with no header declaration — the #1300 MissionTimelineModel case is
  this class and is covered only transitively (the d17 engine sources now
  embed it).
- Windows/MSVC link semantics (no `--gc-sections`, whole-object inclusion)
  are the reason several of these embeds are latent rather than burning on
  every lane; the gate proves the wiring facts, not the linker.
- This environment has no GDAL/GEOS/vcpkg, so no real `cmake configure` or
  target build could run here; verification of the gate is the standalone
  compile + the full Catch2 suite + the mutation matrix, and verification of
  the mapper is its unit suite (14 tests) + static existence checks against
  the wiring text. CI remains the compile-level authority for the wiring
  fixes themselves.

## Pre-existing failures outside this track

`python3 -m unittest discover scripts/dev/tests` → 65 tests, 3 failures on
this machine **with this branch's changes stashed** (identical with them
applied): `test_dev_common` trunk-divergence, `test_preflight` worktree
report, `test_stale_branches` merged-branch — all git-environment dependent
(developed against different git defaults), none touched by this track.

## Rollback

Every change is in three commits (gate+wiring fixes, model strengthening,
mapper) touching only `tests/`, `scripts/dev/narrow_targets.py` and
`scripts/dev/tests/`. Reverting the branch is a pure revert; no production
source file changed.
