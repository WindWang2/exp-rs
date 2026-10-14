# R13-H Ownership & File Boundaries

## Writable Production Scope (Exclusive to Track H)
- `src/planner/**`: Scientific goal parsing, planner core candidate ranking, rule spine DAG, IR projection, open questions, provider isolation, determinism.
- `src/repair_planner/**`: Action safety whitelist, candidate ranking, repair schema, state sequence.
- `src/science_context/**`: Broker cache key & failure digestion, capability router, recipe router, context budget, observed state convergence.

## Read-Only with Exceptions
- `src/suitability/**`: Read-only by default. Only minimal independent edits permitted if an active planning interface defect (unresolved by #1497) is verified with RED reproducer.

## Strictly Read-Only / Off-Limits
- `src/preflight/**`
- `src/scientific_state/**`
- `src/verification/**`
- `src/verify/**`
- `src/faultlab/**`
- `src/grader/**`
- `src/agent/**` (outside of science context adapter if any)
- `src/processing/**`
- `src/workflow/**`
- `src/analysis/**`
- `src/geospatial/**`
- `src/data/**`
- `src/dataset/**`
- `src/gui/**`
- `src/app/**`

## Shared Files Policy
- `tests/CMakeLists.txt`: Append-only for new test cases or targets. Check diff against master before committing.
- `.planning/r13-h-planning/`: Exclusive to this track.
