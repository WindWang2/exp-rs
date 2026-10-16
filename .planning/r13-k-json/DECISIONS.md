# Decisions: R13-K JsonCpp Type Safety & Exception Defense

## Architectural & Design Decisions

### D1: Safe Typed Accessors vs Exception Catching
- **Context**: In JsonCpp, `Value::get(key, defaultValue)` returns the value associated with `key` if present, even if its type differs from `defaultValue`. Calling `.asString()`, `.asBool()`, or `.asInt()` on the result throws `Json::LogicError`.
- **Decision**: Prefer proactive type checking (`isString()`, `isBool()`, `isInt()`) with fallback defaults at input boundaries (such as `agent_adapter.cpp`).
- **Rationale**: Proactive checking is faster, clearer, and avoids exception handling overhead on valid or semi-valid input paths while preventing `Json::LogicError` completely.

### D2: Exception Barrier at Serialization and Top-level Deserialization
- **Context**: Serialization (`canonicalJson`) and structured parsing (`OpDiagnostic::fromJson`, `FinalDelivery::fromJson`) may process arbitrarily nested JSON trees where unanticipated types or memory allocation failures can occur.
- **Decision**: Wrap top-level serialization and JSON model deserialization with `try / catch (const std::exception &)` barriers, converting uncaught errors into typed return values (`false` or `std::nullopt`) with informative diagnostics.
- **Rationale**: Satisfies the fail-closed guarantee across subsystem boundaries.

### D3: Strict Surgical Scope
- **Context**: Maintain strict YAGNI and surgical changes to avoid regressions in other modules.
- **Decision**: Only touch targeted files: `src/verification/canonical_json.cpp`, `src/agent_ops/operations_coordinator.cpp`, `src/agent_ops/session_surface.cpp`, `src/agent_ops/ops_driver.cpp`, `src/agent_ops/ops_types.cpp`, `src/science_context/agent_adapter.cpp`, `src/science_context/capability_router.cpp`, `src/operators/rs/rs_spectral_index_operator.cpp`, and their corresponding test files.
