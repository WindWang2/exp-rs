# REVIEW_LOG — scientific-agent-workflow-compiler-10

Phase 7: two read-only subagent reviewers ran against HEAD (bbeafbb863 diff
range). All findings re-verified in code by the main agent before action.

## Reviewer A — architecture / scientific correctness (Explore, read-only)

| ID | Sev | Finding (verified) | Disposition |
|---|---|---|---|
| A-1 | P0 | eval corpus categories absent from the runner's closed kCategories | FIXED: kCategories extended (+ integration commit), README list updated |
| A-2 | P0 | 3 workflow_compiler cases assert verdicts the implementation does not produce | FIXED: declared-CRS case → post-repair "ok" + repairs evidence; unknown-operator → code changed to repairable=false (no rule inserts operators) so "blocked" is real; modality case → asserts ok + FACT_CONFLICT + observed-facts echo (correct observed-wins semantics, now pinned) |
| A-3 | P0 | UAF in repair rules (pointers into outcome.ir.nodes across push_back) | FIXED: value-copied edges + id-based consumer re-lookup in both CRS and align rules |
| A-4 | P0 | grid check faked a pass: gridFacts reads arrays, inspect emits objects | FIXED: shape-tolerant GridShapeFacts (object+array) + tri-state GridCompare; unknown → skip, never pass; regression test pins object shapes |
| A-5 | P1 | SAR/categorical errors fired on assumed-only facts | FIXED: factBacked gate on both branches → warnings; regression test added |
| A-6 | P1 | executable workflow_json returned even when verdict blocked | FIXED: execution gate — engine JSON withheld when verdict != ok; plan_document stays for audit |
| A-7 | P1 | CRS analysis vs repair read different keys, no case folding | FIXED: one shared normalizedCrsAuthid (crs object/string + crs_authid, case-folded) used by both; regression test (epsg:4326 vs EPSG:4326 no conflict) |
| A-8 | P1 | closed fact table rejected the platform's own object-shaped grid facts | FIXED: size/pixel_size/extent accept object and array |
| A-9 | P2 | documented issue order not applied (dead sorter) | FIXED: deterministic sort applied (code, node, port, message) |
| A-10 | P2 | ADR/header promised radiometric auto-insertion that does not exist | FIXED: ADR + header + rule-table strings now describe prepared decisions |
| A-11 | P2 | node-level CRS/grid emitted once per edge (duplicate issues/refusals) | FIXED: CRS+grid hoisted to node level, one issue per node |
| A-12 | P2 | repair_refusal DN case vacuous (decisions[] empty passes) | FIXED: warnings-driven decision scan added (B-3), case asserts decisions[0].rule_id |
| A-13 | P2 | requires_projected inspected only the first CRS-bearing sibling | FIXED: all siblings checked |
| A-14 | P2 | session evals not hermetic | FIXED: session cases removed from corpus; category renamed knowledge_budget; store coverage in unit tests with temp dir |
| A-15 | P2 | duplicate `as` port bindings unvalidated | FIXED: rejected at read time; test added |
| A-16..A-20 | P3 | fact_status echo keys; kMaxCrsChars/kMaxArtifacts unused; goal in fingerprint; ir_id vs content drift; .tif hardcoded; align literal | FIXED: echo filtered to closed keys; bounds hygiene (kMaxArtifacts removed, kMaxCrsChars enforced + mirrored, ref/port bounds); goal excluded from fingerprint; derived ir_id re-derived after goal/intent override; align uses CapabilityRelations::gridFixer(). .tif extension documented as raster-repair default (repair operators are all raster) |

## Reviewer B — tests / bounds / concurrency / docs-vs-code (Explore, read-only)

| ID | Sev | Finding (verified) | Disposition |
|---|---|---|---|
| B-1 | P0 | HarnessSessionStore self-deadlocks (non-recursive QMutex relocked) | FIXED: directoryLocked/sessionPathLocked used under the single lock |
| B-2 | P1 | grid repair test contradicted implementation (reference pick, as preservation) | FIXED: test pins the implemented deterministic contract (t2 aligned onto t1, local port preserved) |
| B-3 | P1 | radiometric refusal unreachable for warn-class DN | FIXED: planRepairs scans warnings for INVALID_RADIOMETRY → prepared decision (test now passes) |
| B-4 | P1 | rendered_bytes self-measure equality impossible | FIXED: measure-with-placeholder + slack-bounded test |
| B-5 | P1 | corpus loader hard-fails on new categories | FIXED (= A-1) |
| B-6 | P1 | CRS case asserted pre-repair verdict | FIXED (= A-2) |
| B-8 | P2 | compaction retry clobbered saved_at | FIXED |
| B-9 | P2 | declared bounds not all enforced (artifacts/CRS chars/ref/port) | FIXED: kMaxArtifacts removed (no such envelope member), kMaxCrsChars enforced + mirrored in irLimits(), ref ≤ 512, port names ≤ 64; params documented as caller-parsed |
| B-10 | P2 | shortlist budget not hard (keep>1 floor, unbounded why) | FIXED: why cap (4), filter arrays bounded (16×64), summary-trim fallback → budget holds for one item; adversarial test added |
| B-11 | P2 | ADR says six codes, seven added | FIXED |
| B-12 | P2 | ARCHITECTURE repair table / check count / analysis-table claims wrong | FIXED: table matches repairRuleTable(); 17 ledger names; inline-checks wording |
| B-13 | P2 | repeated_error: doc/code/coverage mismatch | FIXED: ADR words the implemented signature guard; distinct_proposal_sets in bounds; session-stage vocabulary aligned |
| B-14 | P2 | session evals escape runner isolation | FIXED (= A-14) |
| B-15 | P2 | ARCHITECTURE drift claims (mcp_bridge.js, phantom C++ floor test, compact action, stage names) | FIXED |
| B-16 | P3 | eviction on overwrite / before write | FIXED: evict after successful write, never the target file |
| B-17 | P3 | unbounded readAll in store | FIXED: load rejects >64KiB typed; list skips oversize |
| B-19 | P3 | weak F-PI-2 regex | FIXED: positional check (Promise.race → finally → cancel) |
| B-20 | P3 | CONTEXT.md ADR index + OWNERSHIP drift | DEFERRED to Phase 9 (CONTEXT.md index entry added with the ADR ledger update; OWNERSHIP table amended) |
| B-21 | P3 | live grounding inside tool execute ("must not block long") | ACCEPTED: same grounding cost as spatial:understand; documented in the tool description; fix would need an async seam (execution-plane lane) |
| B-22 | P3 | dead declaredCentersForRole + stale comment | FIXED |

## Status
All P0/P1 fixed and syntax-verified against the real build flag set. P2/P3
dispositions above; accepted debt recorded (B-21). Re-verification of the full
suites runs on the final HEAD (Phase 8) — local evidence only.
