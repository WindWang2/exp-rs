# R12-E Work Packages — execution mapping

Phase model: P0 baseline (done, see BASELINE.md) → P1 audit swarm (read-only, 7 tracks) → P2 defect fixes (RED→GREEN×2 per defect, single-writer file sets) → P3 targeted build+test under flock → P4 independent review ×2 → P5 final report + PR.

| WP | Scope | Audit track | Status |
|---|---|---|---|
| E01 | Evidence-chain map: verify/verification/preflight/grader/explain/faultlab/suitability entries, producers/consumers, error codes | synthesized from T1..T7 | ACTIVE |
| E02 | Status lattice & failure closure (verification/status_lattice, verify_levels) | T1 | ACTIVE |
| E03 | Grader sequencing & replay determinism | T3 | base items ALREADY_FIXED; edge-hunt ACTIVE |
| E04 | FaultLab declared-param ↔ transform parity | T2 | cell_size ALREADY_FIXED; full-parity audit ACTIVE |
| E05 | FaultLab hostile JSON / boundary exceptions | T2 | fault_scenario parse guard candidate LIVE |
| E06 | Canonical JSON/SHA-256 single authority + parity gate | T7 | four forks confirmed LIVE (quality) |
| E07 | Suitability fail-closed input gates | T4 | min_samples ALREADY_FIXED; sweep ACTIVE |
| E08 | Suitability/preflight report digest & capability mirror bounds | T4 | digests ALREADY_FIXED; mirror bounds audit ACTIVE |
| E09 | scientific_state serialization bounds & consistency | T7 | std::equal ALREADY_FIXED; round-trip audit ACTIVE |
| E10 | verify_adapters bounded IO probes | T6 | ACTIVE |
| E11 | explain output ↔ verification state consistency | T5 | ACTIVE |
| E12 | Behavior benchmarks on existing teaching fixtures (verify→grader→explain) | T3+T5 inputs | PENDING |
| E13 | Negative-contract matrix across fixed entries | from P1/P2 | PENDING |
| E14 | Authority cleanup (only if proven safe) | T7 input | PENDING |
| E15 | Independent review ×2 + final archive | — | PENDING |

Ledger rule: every defect gets `WP-id, root-cause, repro command, RED output, changed paths, commit, GREEN1/2, reviewer, status`. No green-without-repro.
