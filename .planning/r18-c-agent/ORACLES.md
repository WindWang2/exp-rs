# R18-C Oracles (gate definitions)

- **Oracle-A (reproducibility)**: every claimed fix carries a minimal failing reproduction (RED) plus two GREEN runs of the exact same command after the fix. Non-reproducible candidates are recorded as FALSE POSITIVE and dropped — no patch is written to "fill quota".
- **Oracle-B (local verification)**: build/CTest/CLI/Qt-offscreen results with exit codes recorded in TEST_RESULTS.md. Environment-blocked items are listed as `NOT_EXECUTED` with reason + compensating verification; never marked green.
- **Oracle-C (performance)**: baseline machine (40c/62G, shared), same build config (`build-r18-c`, Debug), same inputs, ≥3 controlled comparisons when a perf claim is made (hot/cold cache separated). No unmeasured speedup ratios. Correctness/consistency outranks speed; no output-semantics changes for speed.
- **Oracle-D (regression contract)**: existing user paths must not regress; incomplete-data/no-permission/cancel/timeout/crash-restart/concurrency paths yield distinguishable states (Fail/Indeterminate/Cancelled — never fake Success); thread-safety, error typing, protocol schemas, and resource release have coverage evidence.
- **Oracle-E (ownership)**: `git diff --name-only origin/master...HEAD` ⊆ OWNERSHIP.md zones or documented cross-track permission; overlap re-scanned on every baseline change; stop sub-items that collide with #1513's write set.
- **Oracle-F (review)**: ≥2 independent review rounds (R1 → fix P0/P1/reproducible-P2 → R2 by a different context). More rounds if R2 still finds P0/P1. Unhandled P2/P3 recorded with rationale + impact.
- **Oracle-G (delivery)**: atomic commits; no secrets/large data/build artifacts; `git diff --check` = 0; `review_pack.py` bundle complete; branch pushed; `gh pr create --base master` succeeds with real URL. If push/PR permissions fail, record the genuine error — never fabricate a URL.

## Quantitative self-check matrix (track-specific)
- Pi available: `node --test pi/test/*.test.mjs` ×2 + C++ MCP/catalog/workflow tests ×2. Pi/node unavailable ⇒ exact version evidence + `NOT_EXECUTED`.
- Every request has a terminal state, error code, cancel propagation, bounded queue; same inputs + same event sequence ⇒ deterministic replay.
- Schema snapshot has no unexplained drift; tool catalog vs actual execution cross-checked (no "advertised but never executable" tools).
- Two validation layers: independent black-box fake MCP server + real local MCP binary. No GUI/map-cartography code touched.
