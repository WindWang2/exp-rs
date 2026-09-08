# ADR 0141: I/O Error Model & GDAL Error Hygiene (5.0)

- Status: Accepted (Remote Sensing I/O Foundation 5.0 goal)
- Context: the 4.0 prototype's `GeoError` covered eleven codes. The 5.0 goal
  adds remote, metadata-corruption, cancellation and resource-exhaustion
  semantics, and GDAL's global error state (thread-local CPL error + handler
  stack) needs scoped capture so probe paths stay quiet while failures stay
  diagnosable.
- Decision:
  1. **Extended `ErrorCode` taxonomy** (foundation-wide): the original eleven
     (`InvalidArgument, OpenFailed, DriverMissing, MissingCrs, InvalidCrs,
     TransformFailed, WriteFailed, FidelityLoss, Unsupported, Cancelled,
     IoError`) plus `NotFound`, `PermissionDenied`, `UnsupportedFormat`,
     `UnsupportedProduct`, `InvalidMetadata`, `CorruptData`, `NetworkError`,
     `Timeout`, `ResourceExhausted`, `Incompatible`. Stable string names are
     part of the API (CLI exit-code and operator-error mapping).
  2. **Context, not bare strings**: `GeoError` carries the code, a message,
     structured JSON details and — for GDAL-originated failures — the captured
     CPL error context (message list, severity) inside `details`. Callers see
     typed errors; the underlying GDAL context survives for diagnostics.
  3. **Scoped CPL hygiene**: all probing/inspection goes through
     `QuietCplErrors` (RAII handler push/pop). Library code never installs a
     *permanent* process-wide CPL handler; per-call capture is the only
     sanctioned pattern. Test isolation relies on the same scoping (no test
     output pollution, no cross-test error leakage).
  4. **Mapping**: RSOperator translation maps codes to `RSOperatorError`
     families; the CLI maps codes to stable SDK exit codes; doctor embeds
     codes in findings. One enum, three stable surfaces, no re-translation
     per consumer.
- Consequences: every I/O failure is diagnosable (typed + contextual) and
  testable (code-level assertions), while GDAL's global error state remains
  untouched outside scoped blocks.
