# Agent Benchmark Corpus (RS14)

Goal-level benchmark cases for grading whole agent trajectories — complement,
not replacement, of the tool-contract corpus in `data/agent/evals/` (Tier A).
Everything here is data: JSON documents executed deterministically by the
pure-C++ harness in `src/agentbench` (see `docs/agent/benchmark-harness.md`).
No network, no live model, no engine execution.

## Layout

- `suite.json` — `sicnu.agentbench.suite/v1`; pins the case set and, per
  entry, the graded source: a reference `script` (executed by the
  deterministic fake agent) or a recorded `trace` (replayed as-is).
- `cases/*.json` — `sicnu.agentbench.case/v1`; goal, allowed tools, hidden
  invariants (the grading oracle — agents never see these), resource budget,
  expected evidence, optional fault schedule.
- `scripts/*.json` — `sicnu.agentbench.script/v1`; deterministic fake-agent
  policies with explicit failure discipline (`abort | retry_once | skip`).
- `traces/*.json` — `sicnu.agentbench.trace/v1`; recorded trajectories
  (rogue-tool, inefficient, silent-corruption, clean replay).

## Regeneration

The pack is generated from the SPEC table in
`scripts/bench/generate_agent_bench_corpus.py`:

```
python3 scripts/bench/generate_agent_bench_corpus.py
```

Output bytes are stable. The corpus-validation test
(`tests/test_agentbench_corpus.cpp`) pins the pack digest (FNV-1a of the
suite report's `pack_digest`) and the reference evaluation digest for
`optical/ndvi-basic` — after ANY content change, rerun the generator, then
update the pinned fingerprints and `suite.json.version` in the SAME commit.

## Adding a case

1. Add a spec row in the generator; rerun it.
2. Keep the closed vocabularies: task families
   (`optical|classification|change|temporal|model|map_delivery`), invariant
   kinds, fault kinds, failure classes (see `src/agentbench/failure_taxonomy.h`).
3. The validation test enforces: ≥ 20 cases, ≥ 3 per family, unique
   (case, source) pairs, every case paired in the suite, and every entry
   grading end-to-end.
4. A case that starts failing is a regression — fix the harness or the case,
   never delete it silently.
