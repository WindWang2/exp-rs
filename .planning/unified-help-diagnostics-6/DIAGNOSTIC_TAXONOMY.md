# DIAGNOSTIC_TAXONOMY — Unified Help 6.0

## Origin families (sources of failure)

| Family | Origin type | Code space | Count | Notes |
| --- | --- | --- | --- | --- |
| harness | `HarnessError` (`sicnu::agent::harness`) | 24 stable `SCREAMING_CASE` codes | 24 | category + RetryClass already defined; suggestedActions machine shape |
| operator | `RSOperatorError` (`sicnu::operators::ErrorCode`) | int enum 1000–9999 (24 values) | 24 | string via `errorCodeToString` |
| geospatial | `GeoError` (`sicnu::geospatial::ErrorCode`) | enum, 22 values | 22 | I/O foundation layer |
| dataset | `DatasetFinding` codes (`label.*`, leakage kinds, composition) | dotted strings | ~10 kinds | severity typed |
| preflight | scientific preflight `checks[].code` | dotted strings | catalogued at implementation | MapSpec/preflight issues |
| context | workbench/ContextRules unavailability | command ids | 34 commands | availability facts, not errors |

## Unified Diagnostic ID mapping

```
diagnostic.harness.<snake_case_code>          e.g. diagnostic.harness.dataset_not_found
diagnostic.operator.<snake_case_enum>         e.g. diagnostic.operator.out_of_range
diagnostic.geospatial.<snake_case_enum>       e.g. diagnostic.geospatial.missing_crs
diagnostic.dataset.<finding_code>             e.g. diagnostic.dataset.label.unknown_class
diagnostic.preflight.<check_code>             e.g. diagnostic.preflight.crs_mismatch
diagnostic.rs.<domain>.<issue>                e.g. diagnostic.rs.sar.geometry.missing_look_direction
```

`DiagnosticCatalog::resolve(family, code)` → `DiagnosticDescriptor` (or a generic
fallback descriptor that still carries the original code — never swallowed).

## Descriptor contract

```
DiagnosticDescriptor {
  id, originFamily, originCode (unchanged), title,
  whatHappened, whyItMatters, severity(info|warning|error|critical),
  retrySense(none|manual|transient|ask-origin),
  remediation[], relatedDiagnosticIds[], relatedHelpIds[], technicalNote
}
```

Invariants (tested):
- originCode round-trips byte-identical;
- retrySense agrees with `retryClassForCode` for harness codes (drift test);
- no descriptor text contains filesystem paths of the authoring machine, tokens, or
  credentials (secret-scan test);
- every harness code and every RSOperatorError enum has a descriptor (coverage test).

## Severity mapping

- harness: derived from category (validation→error, io→error, runtime→error,
  environment→critical when blocking).
- operator: 1xxx validation→error; 2xxx io→error; 3xxx computation→error;
  4xxx lifecycle→info (Cancelled) / warning (AlreadyRunning/NotInitialized).
- geospatial: FidelityLoss/CorruptData→critical; Missing/InvalidCrs,
  TransformFailed→error; Cancelled→info.
- dataset findings: severity carried by finding (trusted).

## Remediation authoring rules

1. First bullet = the *cheapest safe* fix.
2. Never suggest destructive recovery (overwrite/delete) without an explicit
   "备份后" qualifier.
3. Reference the exact command/operator help ID when a tool fixes the problem
   (e.g. "运行 rs:spatial_resample" → `operator.rs.spectral_resample`).
4. "Why it matters" states consequences in remote-sensing terms (e.g. wrong look
   direction ⇒ ~90° geometry rotation), not generic "may cause errors".
5. Secrets/absolute author paths forbidden (enforced by test).
