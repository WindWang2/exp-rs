# R16-B Review Round 1 (independent reviewer subagent, 2026-10-17)

Reviewer: fresh-context general-purpose subagent, no involvement in implementation; read-only. Scope: full `origin/master...HEAD` diff + whole-repo caller sweeps + docs-accuracy audit. Axes: API/compatibility, lifetime/concurrency, types/scientific semantics, test quality, security/robustness, docs accuracy.

## Verdict

**ROUND1_VERDICT: PASS_WITH_P2** — "The core fix is correct, minimal, and properly regression-covered; every defect I found is in the evidence documents, not the code."

## Findings and dispositions

| ID | Sev | Finding | Disposition |
| --- | --- | --- | --- |
| P2-1 | P2 | `.planning/r16-b/TEST_RESULTS.md` referenced by docs but not yet committed; integration-run numbers unverifiable from the tree | FIXED — TEST_RESULTS.md committed with the real integration ctest outputs; ledger Phase 3/4 filled |
| P2-2 | P2 | RED narrative mechanically impossible as transcribed (failed REQUIRE is fatal in Catch2, so the `9 == 7` expansion could not co-occur with a failed REQUIRE_NOTHROW; on master the move-assign leaves the NEW authority's valid callables, so nothing throws sequentially) | FIXED — ledger rewritten to the three non-fatal CHECK failures the run actually printed; the throw/torn-read mode is now correctly attributed to the concurrent window (TC_R16B_01's domain); test inline comment reworded |
| P3-1 | P3 | dead `#include <optional>` in `broker.h` | FIXED — removed |
| P3-2 | P3 | `make_shared` `bad_alloc` under lock is a new theoretical exception path | FIXED — comment added: strong guarantee, aborted install leaves `mCapFacts`/`mCache` untouched |
| P3-3 | P3 | RISK_REGISTER R4 named the wrong dependency prefix | FIXED — aligned to `/home/kevin/pwb-sdks/root/usr` with the paleo-prefix failure recorded |
| P3-4 | P3 | TC_R16B_02 worker lacks try/catch (an escaping exception would terminate the binary) | FIXED — mirrors TC_R16B_01's catch + coherent-failure protocol |
| P3-5 | P3 (observation, pre-existing) | snapshot pins the lookup object, not the underlying CapabilityKnowledge data; a pack reload bypassing setCapabilityFacts can still tear revision-vs-entries inside one synthesize (self-healing on the next call) | NOTED — broker.h comment reworded to say exactly what the snapshot owns; behavior unchanged (pre-existing, out of this PR's defect class) |

## Axis results (reviewer's own words, abridged)

1. API/compatibility: CLEAN — consumers are exactly `agent_adapter.cpp` (snapshot held for the whole call) and the new test; move ctor/assign sane; moved-from broker degrades to builtin fallback exactly like the old empty optional; outstanding snapshots now survive a broker move (improvement).
2. Lifetime/concurrency: CLEAN for this change — no `query.facts` escape from `routeCapabilities`; no new lock/nesting; authority/revision read once per synthesize.
3. Types/scientific semantics: CLEAN — no observable change for non-concurrent callers.
4. Test quality: good — churn tests deterministic (fixed iterations, no wall-clock), predicates load-bearing, stop/join protocol race-free.
5. Security/robustness: acceptable — bad_alloc path analyzed (strong guarantee); .gitignore/planning practice consistent with committed r14-E evidence.
6. Docs accuracy: baseline SHA, 39/40→40/40, 14/14 and the 82/82 case count all cross-checked (40+14+13+15); defects found were P2-1/P2-2/P3-3 (fixed above).
