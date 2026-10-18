# R16-B-T03 Baseline — Scientific State + Science Context

- Baseline SHA: `46791bc65c6f552ebbcf723bb91892736d56f319` (`origin/master`, fetched + pruned 2026-10-17, matches the task's reference SHA).
- Worktree: `/home/kevin/project/exp-rs-r16-b-t03`, branch `hardening/r16-b-t03-science-context-lifetime` (from `origin/master`, no rebase/reset).
- Build: `cmake --preset dev-default` (Debug, ENABLE_TESTS=ON), own `build-dev/` inside this worktree; parallelism `-j2` (repo rule in `CLAUDE.md` is stricter than the task's `-j4` ceiling — the stricter one governs). Cross-prompt build lease: `flock /home/kevin/project/exp-rs-r16-build.lock`.
- Test invocations: `ctest --test-dir build-dev -j1 -R <family> --output-on-failure`, `QT_QPA_PLATFORM=offscreen` where Qt is linked.

## Upstream state at baseline (verified by direct read, not by issue text)

| Lead | Where | Status on master @46791bc65 |
| --- | --- | --- |
| #1505 review P1 "capability facts stable lifetime" | `src/science_context/broker.h:65-69`, consumer `src/science_context/agent_adapter.cpp:208` | **OPEN** — `capabilityFacts()` still returns `const CapabilityFactsLookup *` into `std::optional` storage; the tool path stores it and calls through it after the broker lock is released. |
| #1505 review P1 "route recipe searches through the broker lock" | `src/science_context/recipe_router.cpp` | **Mitigated on master** — `RecipeRouter` is independently synchronized (`mMutex` guards `search()` :231, `sourceInfo()` :107, and the `loadFromRegistry()` swap :128). This is the review's named alternative fix. No code change needed; regression test added (WP-T03-2). |
| #1465 broker cache key drops typed failure cause | `src/science_context/broker.cpp:301-318` | **Fixed** — `failureDigest` (sorted, deduped typed markers incl. errorDetail) is cache-key material, with an inline #1465 comment. |
| #1465 3-arg `std::equal` OOB (4 sites) | `src/scientific_state/asset_state_json.cpp:157/191/230/269` (`match` lambdas) | **Fixed** — length gate `text.size() != std::strlen(candidate)` before the 3-arg form, with inline #1465 comments. |
| #1465 static-init file I/O throw → terminate | `src/science_context/agent_adapter.cpp:38-48` + `src/recipes/recipe_registry.cpp` (`defaultDirectory`/`reload`) | **Fixed** — registry probes use `std::error_code` forms; `reload()` returns status instead of throwing; comments reference #1465/#1463. |
| #1447 `capability_router.cpp` `minimum.asInt()` overflow | `src/science_context/capability_router.cpp` (`authoritySpecFromCandidate`) | **Fixed** — `isInt()` gate with inline #1447 comment; UInt-beyond-INT_MAX candidates are skipped fail-closed. |
| #1465 terminology dedup suffix | `src/help/terminology_provider.cpp` | **Out of this track's ownership** (belongs to no R16-B track; recorded in the matrix, untouched). |

## Scope decision

The only live, in-ownership defect confirmed by code read is the capability-facts lifetime P1. Per the task's honesty rules the fixed items above get verification evidence, not re-fixes, and no features are manufactured.
