# MILESTONES — verification platform 8.0

| # | Milestone | Work packages | Exit evidence |
|---|---|---|---|
| M0 | Planning docs + worktree | — | this directory populated; branch from 322dfd3876 |
| M1 | Release hygiene + portability seam | WP-A | `.obj`/`.pdb` removed + ignored; `gdal_compat.h` used by first-party geo code; portability contract test compiles+passes |
| M2 | Verification ladder | WP-B | `scripts/verification_ladder.py` runs L0..L3 locally, JSON results, resumable |
| M3 | Trace chain completion | WP-F | adapters at workflow/taskcenter/committer/stores; `test_trace_chain_8` proves cross-seam correlation |
| M4 | Fault matrix broadening | WP-E | new SICNU_FAULT_POINT sites + `test_fault_matrix_8` contracts pass |
| M5 | Known-answer corpus expansion | WP-C | `test_known_answer_corpus_8` passes (grid ops, splits, cartography structure) |
| M6 | Contract fuzz deepening | WP-D | `test_contract_fuzz_ops` + `test_contract_fuzz_ipc` pass, deterministic seeds |
| M7 | Scale baselines | WP-G | `benchmark_scale8` produces benchmarks/scale8 JSON on Linux host |
| M8 | Anti-vacuity sweep | WP-H | high-value vacuous assertions repaired (documented list) |
| M9 | Readiness report + docs sync | WP-I | `collect_readiness.py` emits READINESS.md/json; docs updated |
| M10 | Adversarial review + remediation | §7 | REVIEW_LOG.md complete; P0/P1 fixed |
| M11 | Final integration + PR | §8 | clean diff vs master; branch pushed; PR created |

## Current status

- M0 done (this directory).
- M1 done (committed: hygiene + gdal_compat + portability contract + probes).
- M2 done (ladder script; D5/D6 lane fixes).
- M3..M7 code complete (trace adapters, fault points, corpus, fuzz, bench);
  local evidence pending the Clang build.
- M8 done (sweeps + TaskCenter adapter anti-vacuity proof).
- M9 script + docs done; report generation pending evidence.
- M10..M11 pending.
