# R16-B-T03 Risk Register

| # | Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- | --- |
| R1 | `capabilityFacts()` raw pointer used across lock release (`agent_adapter.cpp:208`) — moved-from `std::function` throws `std::bad_function_call` mid-tool-call, or formal data race under concurrent reinstall | Medium (single production installer today, but the broker API contract explicitly supports authority reinstall — `broker.cpp:102-104` clears the cache for exactly that scenario) | P1: tool crash / UB | WP-T03-1 shared_ptr snapshot API; deterministic RED→GREEN test |
| R2 | Un-tested concurrency seam: agent tool paths (`recipeSearch`, `scientificCapabilities`) vs authority mutations (`refreshRecipes`, `setCapabilityFacts`) | Medium | P2 latent regression | WP-T03-2 stress regression in `test_science_context_concurrency.cpp` |
| R3 | Cross-PR conflict on `tests/CMakeLists.txt` with #1515 | Avoided | — | no CMakeLists edits; both extended TUs already registered |
| R4 | Host build env: system GDAL absent | Confirmed | blocked configure | local prefix `/home/kevin/project/paleo-singlefactor-closure/vendor/prefix` (same machine-level dependency location the sibling R16 tracks use); recorded in ledger |
| R5 | Review leads treated as bugs without reproduction | — | false-positive churn | every lead re-verified on master source before any change (see BASELINE.md table) |
| R6 | Sanitizer verification unavailable in this environment | Possible | reduced evidence | deterministic RED test + stress test; ASan/TSan availability checked and honestly reported |
| R7 | Memory pressure from parallel sibling builds on the 40-core host | Medium | slow/failed builds | cross-prompt `flock` lease; `-j2` (stricter than the `-j4` ceiling); RSS watched |
