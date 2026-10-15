# Ownership Ledger: R13-K JsonCpp Type Safety & Exception Defense

## Module & File Boundaries
This track focuses on critical P1 stability and robustness issues from untyped JsonCpp access allowing `Json::LogicError` to escape typed error boundaries (Issues #1447 and #1488):

### Canonical Verification Boundary (Issue #1488)
- `src/verification/canonical_json.cpp` (Wrap serialization in exception barrier; safe integer and float serialization)
- `tests/test_verifier_core_14.cpp`

### Operations Coordinator & Approval Tokens (Issue #1447 Site 5)
- `src/agent_ops/operations_coordinator.cpp` (Guard approval token digests and projection lookups; exception barrier in `armRepairApproval` and launch)
- `src/agent_ops/session_surface.cpp` (Guard untrusted client arguments against type mismatch)
- `src/agent_ops/ops_driver.cpp` (Use safe `getStringArg` on reconcile parameters)
- `src/agent_ops/ops_types.cpp` (Exception barrier in `OpDiagnostic::fromJson` and `FinalDelivery::fromJson`)
- `tests/test_agent_ops_core.cpp`

### Science Context Agent Adapter & Capability Routing
- `src/science_context/agent_adapter.cpp` (Safe typed accessors for untrusted agent inputs without unverified `Value::get().asX()`)
- `src/science_context/capability_router.cpp` (Guard boolean flags against type mismatch)
- `tests/test_science_context_broker.cpp`

### Remote Sensing Operators Parameter Boundary
- `src/operators/rs/rs_spectral_index_operator.cpp` (Guard execution mode, chunked mode, and post-fire band indices against non-string/non-int types)
- `tests/test_rs_operator_contract.cpp` or operator tests

## Prohibited Cross-Module Edits
- No modifications to geospatial remote / range cache (Track R13-J in PR #1505).
- No modifications to SQLite durability journal (Track R12-B in PR #1504).
- No modifications to Cartography / MapSpec (Track R13-G).
