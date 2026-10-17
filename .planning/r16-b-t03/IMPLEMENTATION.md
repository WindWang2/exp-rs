# R16-B-T03 Implementation

## Changed files

| File | Change |
| --- | --- |
| `src/science_context/broker.h` | `capabilityFacts()` returns `std::shared_ptr<const CapabilityFactsLookup>` (owned snapshot) instead of a raw pointer into `std::optional` storage; member `mCapFacts` becomes the shared_ptr; `<memory>` include. |
| `src/science_context/broker.cpp` | `setCapabilityFacts` installs a fresh object via `make_shared` (pointer swap under lock — outstanding snapshots stay valid); `synthesize()` raw view switched to `.get()` with a comment why it stays safe under the broker lock. |
| `src/science_context/agent_adapter.cpp` | `scientificCapabilities` tool holds the snapshot for the whole routing call. |
| `tests/test_science_context_broker.cpp` | New TEST_CASE `capability facts snapshot stays answerable across an authority reinstall` (the deterministic RED→GREEN oracle). |
| `tests/test_science_context_concurrency.cpp` | New churn regressions `TC_R16B_01_*` (tool ∥ authority reinstall) and `TC_R16B_02_*` (tool ∥ router replacement). |
| `.planning/r16-b-t03/**` | baseline/ownership/matrix/risk/ledger/test-matrix/performance evidence. |

## Why (defect mechanics)

`setCapabilityFacts` move-assigned the stored `CapabilityFactsLookup`. Any view obtained from `capabilityFacts()` before that pointed into the same storage: after the reinstall it either threw `std::bad_function_call` (moved-from `std::function`) or silently answered for the NEW authority (observed: `revision()` returned 9 through a view taken while authority A was installed — expansion `9 == 7` in the RED run). The agent tool `scientificCapabilities` kept exactly such a view past the broker lock (`agent_adapter.cpp:208` on master), so a reinstall racing a tool call was a data race with those outcomes. The broker's own API contract supports reinstall (it clears the cache for that scenario, `broker.cpp:102-104`), so the hazard is reachable, not theoretical. Production installer: `src/agent/data_platform_tools.cpp:1509` (read-only, unchanged).

The fix makes the handed-out projection owned: install = fresh object + pointer swap; snapshot = shared_ptr copy under the lock. No behavioral change for non-concurrent callers; `synthesize()` still reads `mCapFacts` under its lock (co-owned object, reinstall-safe).

## Alternatives considered

- Returning `std::optional<CapabilityFactsLookup>` by value (copy of three `std::function`s + string per call): rejected — copies `std::function` targets on every synthesize/tool call; shared_ptr snapshot is one atomic increment.
- Making `CapabilityFactsLookup` itself internally synchronized: rejected — it is a plain projection value type by design (`capability_facts.h`); synchronization belongs to the broker that owns it.
