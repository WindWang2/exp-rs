# R16-B Review Round 2 (independent reviewer subagent, 2026-10-17 — FINAL gate)

Reviewer: second fresh-context subagent, no involvement in implementation or round 1; read-only. Scope: re-verification of all round-1 fixes at HEAD, regression & boundary audit of the code change, test-assumption edge scan, docs cross-check (including mechanically recounting test-case populations via `--list-tests`/`ctest -N` on the prebuilt binaries and replaying the RED assertion semantics under master behavior).

## Verdict

**ROUND2_VERDICT: PASS** — "All round-1 fixes are genuinely in place and mechanically verified; the core change is correct, minimal, and regression-covered; moved-from/move/reinstall boundaries behave identically or strictly better than before. Remaining defects are two P3 evidence-tally inaccuracies and P4 observations — none in code."

## Findings

| ID | Sev | Finding | Disposition |
| --- | --- | --- | --- |
| F2-1 | P3 | t03 ledger diff tally said "5 files, +260/−7"; actual committed `git diff --numstat` = **6 files, +280/−8** | FIXED — corrected in the round-2 fix commit |
| F2-2 | P3 | t04 ledger claimed "(21 hits)" for `setCapabilityFacts` in `test_preflight_authority_invalidation.cpp`; actual = **7 occurrences** (the substantive claim — zero `capabilityFacts()` consumers under the t04 modules — was independently verified true) | FIXED — corrected |
| F2-3 | P4 | `agent_adapter.cpp` names `std::shared_ptr` via broker.h's transitive `<memory>` | FIXED — explicit include added |
| F2-4 | P4 | churn-spawner threads lack try/catch (only theoretical `bad_alloc`; the exception-facing tool workers are covered) | NOTED — no action required |
| F2-5 | P4 | GREEN-run-2 / round-2 placeholders unfilled (staged for after this review) | FIXED — this file + TEST_RESULTS.md |
| F2-6 | pre-existing, out of diff | dead/transitive includes in `capability_facts.h` and the broker test (`<cstdint>`) | Out of scope; recorded only |

## Round-1 fix re-verification (reviewer's own checks, abridged)

- **P2-1**: TEST_RESULTS.md committed; all counts cross-checked mechanically — binaries' `--list-tests` match source TEST_CASE counts (40/14/13/15/20); the doc's exact regexes yield 102 and 196 via `ctest -N`; RED's "1 of 40" matches the broker-only regex; RED Test #880 reproduces in the track worktree (it is #980 under the integration tree's larger population — both claims internally consistent).
- **P2-2**: the reviewer independently replayed the new test's assertions under master semantics: exactly 8 assertions, 5 pass / 3 fail, all three failures non-fatal CHECKs, `REQUIRE_NOTHROW` passing sequentially — the rewritten ledger matches genuine Catch2/ctest output format line-for-line.
- **P3-1..P3-4**: all verified present at HEAD (include sweep over every broker.h/agent_adapter.h includer found no other TU relying on the removed transitive `<optional>`).

## Regression/boundary audit highlights

- Single lock discipline unchanged (`synthesize()` function-wide lock; raw view only under it; `cq.facts` never escapes `routeCapabilities`).
- Moved-from broker: null shared_ptr → `capabilityFacts() == nullptr` → builtin fallback + `builtin_fallback/degraded` provenance — identical to the old empty-optional path; outstanding snapshots now survive a broker move (documented improvement).
- Churn tests: stop/join protocol race-free (tools joined → stop → churn joined; guard object outlives all joins); `REQUIRE(countA != countB)` cannot fire spuriously (A serves 1 well-formed candidate, B serves 2; no presence closure filters; limit 8 never truncates).
- Repo-wide `capabilityFacts` sweep: consumers remain exactly agent_adapter.cpp + the new test; production installer unchanged (`data_platform_tools.cpp:1509`).
