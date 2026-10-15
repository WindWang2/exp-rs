# Oracles: R13-K JsonCpp Type Safety & Exception Defense

## Verification Oracles
Each fix must satisfy strict, falsifiable oracles:

1. **Oracle O1 (Canonical JSON Barrier)**
   - When `canonicalJson` encounters an unexpected value structure or exception, it must not allow `Json::LogicError` to escape to callers.
   - It must return `false`, preserve the contract that `out` is untouched, and provide a non-empty `error` string.

2. **Oracle O2 (Approval Token Type Safety)**
   - Passing non-object, non-string `digest`, or non-string `findings_digest` to `armRepairApproval` or `run` with `repairApproval` returns `"APPROVAL_MALFORMED"`, never throws `Json::LogicError`.
   - Verified via Catch2 assertions with `Json::Value(42)`, `Json::Value(Json::arrayValue)`, and objects with invalid field types.

3. **Oracle O3 (Surface Wire Robustness)**
   - `sessionSurfaceApply` with non-string `goal` or `journal_directory` returns `MISSING_ARGS` error doc, never throws `Json::LogicError`.
   - `ops_driver.cpp` action `"reconcile"` with integer or non-string `journal_directory` returns `MISSING_ARGS`, never throws `Json::LogicError`.

4. **Oracle O4 (Science Context Agent Adapter Resilience)**
   - `scientificContext`, `scientificCapabilities`, `dataAssetPassport`, and `recipeSearch` invoked with type-mismatched options (e.g., `autonomy_level = 12345`, `offline = "false"`, `max_bytes = "100"`, `use_cache = 999`) process safely using defaults or clean errors, never throwing unhandled `Json::LogicError`.

5. **Oracle O5 (Spectral Index Parameter Robustness)**
   - Setting `params["executionMode"] = 123` or `params["postNir"] = "invalid"` on `rs:spectral_index` does not crash or throw `Json::LogicError`.

6. **Oracle O6 (100% Green Verification Suite)**
   - All tests in `test_verifier_core_14`, `test_agent_ops_core`, `test_science_context_broker`, and `test_rs_operator` pass consecutively twice (GREEN x 2).
