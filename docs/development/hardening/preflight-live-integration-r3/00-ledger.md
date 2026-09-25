# Track 04 — Preflight Live Integration (`preflight:check`, Temporal/Grid Facts) R3

Master at branch point: `9ea5a2fd17317d924ac2c234286650b48c0b02bf` (post-#1315).
Open PRs at execution time: #1314 (operators/runtime model family — no overlap),
#1312 (app/workbench lifecycle — touches `tests/CMakeLists.txt`; this branch appends
targets at the end, semantic-union is trivial).

## Recon findings (verified on live master)

* `src/preflight` (RS14-02 PreflightEngine, ten rule families, provider seam,
  mirror projection, report/render) had **zero runtime consumers** — tests only.
* `preflight:check` did not exist on any surface (data-platform, spatial tools,
  harness actions).
* `CapabilityMirrorProjection` (Qt-free) re-implements extends/variant merge with
  documented "authority parity" intent, while the runtime authority
  `sicnu::agent::harness::CapabilityKnowledge` (Qt) merges independently — a real
  dual-logic risk (e.g. the mirror keeps `id`/`kind` from the entry itself and
  takes the family default at the terminal ancestor; the authority strips
  `id`/`kind` from every source and resolves the family default at each
  extends-less hop).
* The passport projection (`asset_state_adapter`) deliberately left temporal
  counts/dates unset ("a provider with catalog access should supply those") and
  dropped the passport's temporalRefs and extent.

## Slices landed

1. `preflight/facts.h`: `SlotFacts` gained `hasExtent`/min/max (grid facts,
   authority-projected), `temporalInvalidTimeCount` (bad/missing scene times are
   COUNTED, never silently dropped) and `temporalCollectionRefs` (declared
   collection identities, sorted). `asset_state_adapter.cpp` projects all of
   them from the passport.
2. `preflight.temporal_policy` (revision 2): scenes without a parseable
   acquisition time emit `SPF_TEMPORAL_TIME_INCOMPLETE` (require_ack, observed,
   counted evidence) and skip the order/gap judgment — same partiality
   discipline as the truncation marker: a partial series never gets a verdict.
3. `preflight/runtime_adapter.{h,cpp}` (Qt-free, in `sicnu_preflight`):
   * `AuthorityCapabilityProvider` — `ICapabilityProvider` over an injected
     lookup; the authority's MERGED entry flows in verbatim (no second merge;
     load problems flip a null entry to typed Unavailable).
   * `TemporalFactsProvider` — decorator resolving declared collection refs via
     an injected collection lookup; dates keep COLLECTION order (rules judge
     ordering); the retained-date cap is loud (`truncated`).
   * `preflightCheckJson` — args in, `{report, teaching, agent}` out; ONE local
     engine, builtin rules; malformed args fail closed with a typed
     `sicnu.preflight.check_error/1` document.
4. `agent/data_platform_tools.cpp`: `preflight:check` tool — per-input inline
   passports (same inline semantics as `data:asset_passport`) or the shared
   broker's live passport resolver; capability = `CapabilityKnowledge::instance()`
   (the single store); temporal = workspace-catalog collection descriptors.
   No policy, no repair, no second capability merge in the shell. Tool taxonomy
   override added (`context/preflight`).
5. Oracles:
   * `tests/test_preflight_runtime_adapter.cpp` (Qt-free): real passports through
     the canonical `sicnu.asset_state.v1` reader + injected authority → report;
     authority variant/extends edits flip the runtime verdict (parity by
     injection); temporal order/invalid/missing/truncation matrix; grid
     mismatch blocks with typed unknown fallback; byte-stability; ack can never
     flip a block (and DOES clear require_ack codes); three-projection
     consistency; typed arg errors.
   * `tests/test_preflight_check_tool.cpp` (Qt): dispatched through
     `handleDataPlatformTool` with the live harness CapabilityKnowledge — a
     capability DOCUMENT edit (blue demanded, then not) flips the verdict;
     block survives its own acknowledgement; workspace-catalog temporal
     collection produces `SPF_TEMPORAL_ORDER_INVALID` (observed); byte-stable
     repeats; malformed args throw typed failures.

## Non-goals honoured

No second engine; no repair decisions in preflight; the tool does not judge
scientific validity itself (the engine is the only evaluator); the mirror is
NOT copied into the runtime path — the Qt runtime delegates to
`CapabilityKnowledge` by injection and the Qt-free mirror stays the test/pin
path. agent_ops/repair_planner are deliberately NOT wired here: they consume
typed artifacts (`agent_loop::PreflightReport`, `RepairRequirement`) with
different vocabularies; forcing a bridge would create a second truth source.
`preflight:check` returns the formal `sicnu.preflight.report/1` artifact
(digested, fail-closed reader) which any future consumer can adopt verbatim.

## Verification evidence

* Baseline: 56 preflight leaf tests green before changes.
* RED: `SPF_TEMPORAL_TIME_INCOMPLETE` test failed before the rule change
  (revision bumped 1 → 2).
* Narrow builds only: named leaf targets (`test_preflight_*`), object-level
  compile for `data_platform_tools.cpp` (plus its named test target link).
  No full-product build was run; the narrow-target script maps the same set.
* All preflight suites (engine/rules/render/provider/golden/report_schema) plus
  the two new suites green after the change.
