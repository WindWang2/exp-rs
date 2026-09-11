# DOCS LEDGER — Verification Platform 8.0

Docs-vs-code synchronization contract: every doc change names the code that
backs it.

| Document | Change | Backed by |
|---|---|---|
| docs/verification/PLATFORM_MATRIX.md (NEW) | platform claims vs local execution; the three post-7.0 failure classes and their guards; GDAL breakpoint table; feature-toggle surface | src/geospatial/util/gdal_compat.h; tests/CMakeLists.txt sicnu_header_probes; test_portability_contract |
| docs/verification/TRACE_ARCHITECTURE.md (appended) | 8.0 chain-completion table (5 new adapters, funnels, disabled-path cost) | workflow_run_coordinator.cpp, task_center.cpp, output_committer.cpp, dataset_store.cpp, experiment_store.cpp; test_trace_chain_8 |
| docs/verification/FAULT_MATRIX.md (appended) | 3 new fault rows with contracts + test evidence | dataset_store.commit, experiment_store.commit, model_provider.acquire; test_trace_chain_8; test_model_failure_matrix fault8 case |
| docs/verification/KNOWN_ANSWER_MATRIX.md (appended) | grid ops / splits corpus rows; zonal remains refused-by-scope | test_known_answer_corpus_8 |
| .planning/verification-platform-8/** | track governance (baseline, ownership, plan, tests, perf, review log) | this branch |
| README/docs claims | NO product-behavior claims added anywhere: adapters are default-off; fault points test-only | product diff |

Rule enforced during review: any claim in the final PR description must map
to (a) a command in PERFORMANCE.md, (b) a lane item in the ladder JSON, or
(c) a doc line backed by code named here.
