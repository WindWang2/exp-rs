# R12-E Independent Review — Round 1

Reviewers: REVIEWER-A (adversarial correctness & test validity) and REVIEWER-B (contracts/integration/ownership/platform) — both fresh agents, no participation in implementation. Target: 28abef3582..(10 commits, D1–D17).

## Verdicts
- Reviewer-A: **0 P0, 0 P1, 7 P2** — "sound, ship-able direction; every fix fail-closed, typed, pinned by precise tests; no false-PASS introduced."
- Reviewer-B: **0 P0, 1 P1, 6 P2** — "SOUND with one unclosed sibling hole."

## Round-1 findings (all addressed)
| # | Sev | Finding | Disposition |
|---|---|---|---|
| B1/A2 | P1 | model_sidecar.cpp:66-69/:93-96 (and asset_state_diff.cpp:188) isIntegral→asInt/asUInt64 throw escape — D4 defect class unclosed module-wide | FIXED 717f5537f4 (+ fault_scenario max_bytes same commit) |
| A1 | P2 | capability_mirror depth-cut false positive: unresolved parent at the merge bound misreported | FIXED 717f5537f4 (cut requires resolving parent) + self-extends pin |
| A3 | P2 | uncovered-result fold bypassed IndeterminatePolicy::Fail promotion | FIXED 717f5537f4 (rollUp(policy) + promoted flag on result) |
| A4 | P2 | canonicalProjectionJson still precision-12 writer | FIXED 717f5537f4 (rides canonicalizeJsonRfc8785) |
| A7/D2 | P2 | scene integer-shaped fields: fractional truncation / negatives / out-of-range | R1.5 fixed fractional 717f5537f4; range gate e067a523b2 |
| A5/B3 | P2 | stale E12 comment in test_fault_matrix.cpp | FIXED (main agent, docs commit) |
| B2 | P2 | src/preflight/CMakeLists.txt header comment claimed Qt-free sources | FIXED (main agent, docs commit) |
| B4 | P2 | ORACLE_LEDGER D8 row overstated golden pins | FIXED (main agent, docs commit) |
| B5 | P2 | validator guidance_ member dead — documented as such | recorded follow-up in DECISIONS.md |
| B6 | P2 | Qt `slots` macro include-order fragility — loud at compile time, guarded by comments | recorded awareness, no action |
| B7 | P2 | fault_transforms isIntegral→asInt64 siblings | escalated to R2 P1 → FIXED e067a523b2 |

## Verified-sound highlights (attacks that failed)
- D1: all existing scenario fixtures are comment-free, ≤4 MiB; no production caller of the file loader; cap generous for artifact class.
- D8: zero pinned digest literals repo-wide; replay-equality pins hold; acyclic link independently verified (data's closure has no path back to preflight; root CMakeLists orders data before preflight).
- D10: covered-set construction prevents double-folding; SPEC_INVALID provably cannot fire on full coverage (negative control pinned in G-14).
- D12: iterative three-color DFS hand-traced on self-loop/2-cycle/long-chain/diamond DAG; MSVC-safe (no recursion).
- D16: parity gate compares all four forks on every corpus entry + NIST KAT anchors catch joint drift; teeth demonstrated by KAT perturbation (reverted).
- Ownership: diff touches only R12-E paths + two recorded tests/CMakeLists.txt lines + src/preflight/CMakeLists.txt.
- Vocabulary seal: zero invented error codes; every new string reuses a sealed code.
- Test honesty: assertions pin precise typed outcomes (exact codes/fields/boundaries); guards labeled GUARD; no non-success-only assertions.
