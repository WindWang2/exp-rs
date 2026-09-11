# TEST MATRIX — Verification Platform 8.0

New tests this track adds, and the contract each proves.

| Test / artifact | Lane | Contract proven | Deterministic & bounded |
|---|---|---|---|
| `test_portability_contract` | L2 | gdal_compat macros == compiled == runtime GDAL; monotone capability ordering; /vsirangecache/ install/uninstall on the selected branch | yes; no network (stat error path only) |
| `sicnu_header_probes` (L0) | L0 | nominated first-party leaf headers compile as first include of a cold TU (F1 guard) | yes; compile-only |
| `test_trace_chain_8` | L2/L5 | trace chain: committer/dataset/experiment/TaskCenter records carry identity + truthful status; store fault points fail truthfully + rollback + retry succeeds; TaskCenter terminal record from a REAL instant job | yes; ≤ 5 s waits; ring sinks |
| `test_contract_fuzz_ipc` | L2 | worker protocol parse totality + version strictness + builder round-trip; plugin manifest totality (success ⇒ id present, failure ⇒ diagnostic); split-config totality + truthful validation + JSON round-trip | fixed seeds; ≤ 512 B inputs |
| `test_contract_fuzz_ops` | L2 | operator schema builders total + shape-stable; parameter projection total + #619 uint64 degradation; model manifest validation total with BOTH verdicts exercised | fixed seeds; ≤ 512 B inputs |
| `test_known_answer_corpus_8` | L2 | window algebra (10r+c), budget formula + typed #808 refusal, tiled edge-block NoData padding (#790), largest-remainder splits (10/5/5, 4/3/3, testRatio=0), spatial block atomicity | synthetic ≤ 6×4 rasters; in-proc splits |
| `test_model_failure_matrix` (+fault8 case) | L4 | `model_provider.acquire` fault: typed error BEFORE provider touch; no output/residue; disarmed retry succeeds | scripted provider; 32² raster |
| `benchmark_scale8` | L7 | scheduling 1k/10k/100k; dataset 100k metadata + paged reads; 64² window reads over 1024²; trace file-sink overhead | env-bounded scales; JSON out |

## Environment governance

All binary runs go through `scripts/verification_ladder.py`, which applies
the TEST_INFRA.md policy (offscreen Qt, compose IM, /usr/lib first,
PYTHONHOME/PYTHONPATH from the configured interpreter). Direct runs in this
track used the same env exports.

## Honest status vocabulary

`passed` / `failed` / `timeout` / `not-built` / `skipped` — a capability
that was not executed on this host is recorded as such and never as pass
(enforced by `scripts/collect_readiness.py`).
