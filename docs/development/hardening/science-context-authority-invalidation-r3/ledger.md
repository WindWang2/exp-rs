# R3 Track 03 — Science Context / Live Authority / Cache Invalidation / Budget

Branch: `hardening/r3-science-context-authority-invalidation-r3`
Base at execution time: `origin/master` = `5697ca2adb8a46fb6454a2fdd88799cf78298a19`
Dedup at start: open PRs #1314 (model publish family — no overlap),
#1312 (workbench full shell — touches `src/app` + `tests/CMakeLists.txt`; this
track deliberately avoids `src/app` and adds no test-target registrations).

## RED evidence (proven on the base commit before any fix)

Run: `build-r3sci/test_science_context_broker "[r3]"` on unmodified base.

| # | Bug | Expansion evidence |
|---|-----|--------------------|
| R1 | Cache key ignores passport content beyond id/revision/unit — same id+revision with re-imported content served a stale bundle from cache (`cacheHit=true`, stale `radiometricUnit`, resolver re-consultation never happened) | `"surface_reflectance" == "digital_number"`, `1 >= 2` |
| R2 | Multi-asset CRS/grid conflict detection compared only `states[0]` vs `states[1]` — a conflict between assets 2..n went undetected and the capability stayed `direct` | `"direct" == "unavailable"` |
| R3 | Byte budget did not cover every section: a 100 KB hostile intent produced a 301,203-byte bundle against an 8,192-byte budget, and dropped questions survived verbatim in `planner.open_questions` | `301203 <= 8192`, `1 <= 0` (planner questions vs budgeted questions) |
| R4 | Untrimmable oversized `goal` left the bundle over budget with no explicit section marking | `serialized=201209 truncated=1` with no `goal` in `truncation.sections` |

## Fixes

- `src/science_context/broker.cpp`
  - `assetDigestOf` now hashes the passport *projection* (`observed_state`)
    per asset: identity + content, deterministic.
  - CRS/grid conflict scan is pairwise over ALL assets.
  - Resolver failures surface typed reasons in the bundle problem channel
    (`asset_resolve_failed:<code>[:detail]`, deduped): `gdal_open_failed` ≠
    `asset_not_found` ≠ `resolver_unavailable`.
- `src/science_context/context_budget.cpp`
  - Every string field is normalized (UTF-8-safe cut, ASCII `[~truncated]`
    marker, owning section recorded): goal, intent, planner goal/intent/
    blockReason, open questions, capability intent/id/reasons/prepActions,
    recipe title/intent/matched, asset id/revision/displayName/pathHint/
    evidencePaths/conflictAlternatives.
  - Planner sub-projections carry the item budget (`planner_limitations`,
    `planner_missing_facts`, `planner_open_questions` section marks) and the
    final `planner.open_questions` is a mirror of the *budgeted* question set.
  - Byte ladder extends to goal/intent tightening when counts are exhausted;
    untrimmable residue is explicitly marked (`truncated` + sections).
  - `droppedAssets` counted (new `TruncationMeta` field, JSON round-trip).
- `src/science_context/context_cache.{h,cpp}`
  - `clear()`-on-overflow replaced by a deterministic LRU (access-order list,
    never unordered_map iteration order) + hit/miss/eviction counters for
    invalidation-cost probes.
- `src/science_context/asset_state_provider.{h,cpp}`
  - `PassportResolver` now returns `PassportResolution{state, errorCode,
    errorDetail}` — a typed failure channel. Failures are never cached.
- `src/science_context/live_asset_resolver.{h,cpp}`,
  `src/science_context/gdal_asset_source.{h,cpp}`
  - `AssetFactSources.dataset` returns a `DatasetFactsLookup` carrying the
    typed GDAL error; the GDAL collector fills it from `GdalFactsError`.
- `src/scientific_state/gdal/gdal_state_facts.{h,cpp}`
  - New `GdalFactsError{code, path, detail}` + typed overload; codes
    `gdal_open_failed` / `gdal_facts_failed`; CPL detail bounded to 256 bytes
    and single-line. String overload preserved (composes `code: detail (path)`).
- `src/cli/cli_passport_commands.cpp` — uses the typed overload; the CLI
  message keeps code + detail instead of a bare path.
- `src/agent/harness/capability_knowledge.{h,cpp}`
  - Minimal revision seam in the SAME authority: `revision()` (monotonic,
    advances only when a reload changes loaded content — same-content rescans
    do not churn caches) + `contentDigest()`; `factSetsForIntent()` serves the
    base + variant-overlay fact-sets so the agent layer no longer re-derives
    candidate semantics (removed the duplicate derivation).
- `src/science_context/capability_facts.h` — `revision` became a live
  provider (`std::function<std::uint64_t()>`), read on every synthesize.
- `src/agent/data_platform_tools.cpp` — `ensureScienceContextAuthorities()` now
  wires ALL authorities: capability facts (candidates via
  `factSetsForIntent`, live revision provider) and the GDAL-backed live
  passport resolver; the shared broker's default projection owns the recipe
  registry (see agent_adapter) and a fresh rescan is available via the
  `refresh_recipes=true` tool arg; tool docs updated.

## Oracles (GREEN, all passing)

Development-time kill evidence: the first LRU implementation double-erased the
victim list node (`erase(it)` + `pop_back()`), desyncing map/list. Unit
boundary probes passed anyway (UB), but the scale probe (4000-request churn)
crashed on `!list::empty()` — the oracle suite was extended with the
boundary-retention checks (goal `overflow`/last hit, `0`/`overflow-1` miss)
that pin the fixed behavior.
`tests/test_science_context_broker.cpp` — 33 cases / 142 assertions:
- R1 fixed: inline content change never hits (`mut` case); `invalidateAsset`
  forces re-resolution and serves new content with identical id+revision.
- R2 fixed: 3-asset CRS conflict (b↔c) blocks the capability.
- R3/R4 fixed: hostile 100 KB intent/goal stay bounded or explicitly marked;
  hostile non-ASCII content survives budget cutting as valid UTF-8;
  asset trims counted (`droppedAssets`); planner mirror holds.
- LRU: bounded size, deterministic victims (first `overflow` requests evicted
  in order, boundary + last retained), counters; duplicate request serves
  byte-identical cached bundle; A→B→A alternation never cross-contaminates;
  a one-byte goal difference produces a different bundle id and no false hit.
- Capability revision change ⇒ proven cache miss; provenance revision=2.
- Typed GDAL failure (`gdal_open_failed`) and typed missing asset
  (`asset_not_found`) both reach the problem channel, distinct.

`tests/test_science_context_live_authorities.cpp` — 14 cases / 194 assertions
(updated to the typed resolver seam; fail-closed guards preserved).

`tests/test_scientific_state_gdal.cpp` — typed open failure keeps code, path,
bounded single-line detail; string overload composition; success resets.

`tests/test_capability_knowledge.cpp` — revision seam (content change advances
exactly one generation; same-content rescan zero), `factSetsForIntent` serves
merged base entries and variant overlays (red_edge variant case).

`tests/test_data_platform_surface.cpp` — end-to-end wiring probe through the
real MCP dispatch: recipe hits from the live registry pack, `live_authority`
provenance for capabilities+recipes, capability authority revision ≥ 1, and a
typed `gdal_open_failed` question for an unresolvable dataset path.

## Performance / invalidation cost

Measured on this machine (Debug-built static lib linked into an `-O2` probe,
8 passports, 64 recipes; probe source throwaway, numbers indicative):

- uncached synthesize: ~4.0 ms/op (recipe scan + planner projection +
  serialization dominate).
- cached duplicate request: ~0.6 ms/op — the cacheable path pays one
  observed-state projection per passport (built once, reused for observed
  state, planner facts AND digest) plus FNV-1a; this is the price of
  same-id/new-content staleness immunity.
- invalidateAsset + resynthesize cycle: ~4.7 ms/op, i.e. invalidation itself
  is O(cache/provider clear) with no I/O; the cost is the honest re-synthesis.
- LRU churn (distinct goals, 128-entry bound): ~4-7 ms/op incl. full
  re-synthesis per request, evictions counted (3873/4000), no degradation as
  the map fills (O(1) evict).
- hostile 100 KB goal under an 8 KiB budget: ~1.4 ms/op, emitted bundle
  8184 bytes ≤ budget, truncation explicitly marked.
- `refreshRecipes`: one registry rescan (directory scan, bounded pack) +
  router projection; only on explicit refresh — never on the synthesize path.

## Independent adversarial review (post-implementation)

Reviewer verdict on the full diff: 0×P0, 1×P1, 3×P2, 5×P3 → all P1/P2 fixed,
P3 fixed except the noted idiom item:

- P1 fixed — restored `#pragma once` accidentally dropped from
  `gdal_asset_source.h` / `live_asset_resolver.h` (placed at line 1).
- P2 fixed — UTF-8 oracle was mutation-weak (jsoncpp does not validate raw
  UTF-8): now feeds 40,000 valid U+6C34 code points and asserts strict
  sequence shape (no orphaned continuation bytes) of every budgeted string,
  marker presence, and shrinkage.
- P2 fixed — hostile-goal oracle's `bounded || marked` disjunction replaced by
  the hard bound (`serialized <= maxBytes`) plus the section mark, since the
  goal is the only oversized field there.
- P2 fixed — `gdal_facts_failed` detail could carry a stale CPL message:
  `CPLErrorReset()` now separates open and collect phases, with the inner
  string error as fallback detail.
- P3 fixed — `ContextCache` non-copyable (deleted copy; defaulted move, which
  preserves `lruIt` validity because list nodes transfer wholesale). Found a
  real knock-on: the static default broker in agent_adapter constructs by
  value via a lambda — the module's move semantics had to be explicit.
- P3 fixed — `factSetsForIntent` variant candidates now strip the base
  `intents` bookkeeping (byte-shape parity with the previously wired
  derivation).
- P3 fixed — capability authority revision is read once per synthesize into a
  local; cache key and provenance stamp cannot disagree.
- P3 fixed — `tightenString` overflow arithmetic widened to `long long`
  (hostile `max_bytes` near INT_MIN is UB-on-paper only, now gone).
- P3 noted, not taken — broker.cpp duplicates the module-idiomatic file-local
  `fnv1a64`/`hex16`; the module already had four copies of this idiom
  (context_cache, bundle, recipe_router) before this change, and the review
  itself confirms the new copy is inert. Consolidation is a cross-cutting
  cleanup outside this track's scope.
- Reviewer-verified invariants I did not have to change: all resolver
  construction sites migrated; all `revision` consumers null-check the
  provider; no consumer depends on the old GDAL error strings; the cached
  bundle path cannot diverge from the fresh path; the recipe registry does
  not shadow the harness RecipeCatalog (different data directories).

## Known limits / follow-ups

- `notifyProjectSwitch()` is exposed and tested but no `src/app` project-
  lifecycle hook calls it yet — PR #1312 owns `project_context.{h,cpp}` /
  `main_window_project.cpp`; to avoid a semantic conflict the app-layer call
  (`ProjectContext::clearProject` → `sharedBroker().notifyProjectSwitch()`)
  is left as the single remaining hookup, documented here.
- The registry projection refreshes on `refresh_recipes=true` or process
  restart; there is no filesystem watcher (out of scope, no new daemon).
- `data:asset_passport` resolves dataset paths via GDAL; a catalog-id → path
  indirection would need the app-layer DataManager (same #1312 concern).
