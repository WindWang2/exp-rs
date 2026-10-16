# Goals: R13-K JsonCpp Type Safety & Exception Defense

## Core Objective
Systematically eliminate untyped JsonCpp access where `Json::LogicError` can escape typed error boundaries or crash host processes during malformed inputs, API misuse, or untrusted payload processing (Issue #1447 and Issue #1488).

## Key Deliverables
1. **Verification Canonical Json Exception Barrier (Issue #1488)**
   - Protect `sicnu::verification::canonicalJson` against escaping `Json::LogicError` and standard exceptions.
   - Return `false` and set informative error string on exception, leaving output buffer untouched.
   - Strengthen tests in `test_verifier_core_14.cpp`.

2. **Operations Coordinator & Surface Wire Defenses (Issue #1447 Site 5)**
   - Protect `OperationsCoordinator::armRepairApproval` from unverified access on `tokenDoc["findings_digest"]` and non-object `tokenDoc`.
   - Protect `OperationsCoordinator::run` direct repair approval handling from non-string digests.
   - Enforce fail-closed typed refusal `APPROVAL_MALFORMED` before any digest conversion.
   - Prevent `Json::LogicError` in `sessionSurfaceApply` ("run", "resume") on malformed arguments.
   - Replace unsafe `args.get(..., "").asString()` in `ops_driver.cpp` with safe `getStringArg`.
   - Wrap `OpDiagnostic::fromJson` and `FinalDelivery::fromJson` in safe exception barriers.

3. **Science Context Agent Adapter & Capability Router**
   - Provide safe typed accessors (`safeString`, `safeBool`, `safeInt`) in `agent_adapter.cpp` so unexpected types fall back gracefully rather than throwing `Json::LogicError`.
   - Protect `capability_router.cpp` from type mismatch on boolean flags (`crs_conflicted`, `grid_conflicted`).
   - Add unit tests in `test_science_context_broker.cpp` covering adversarial and type-mismatched payloads.

4. **Remote Sensing Operator Parameter Defenses**
   - Protect `rs_spectral_index_operator.cpp` from type mismatch on `executionMode`, `chunkedMode`, `postNir`, `postSwir2`.
   - Ensure unexpected types are safely ignored without escaping `Json::LogicError`.
