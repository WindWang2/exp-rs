# Progress — RS14-05 Process-aware Experiment Grader

## Slice A — schema (2026-09-22) ✅

- **RED record (stub stage)**: `test_grader_schema` → 11/11 test cases FAILED (34/45
  assertions). Stubs returned typed `grader:e-internal` errors / empty parser results, so
  every serde/canonical/digest/engine-refusal case failed for *missing capability*; only the
  enum-spelling cases that stubs already implemented normatively passed. Fixture errors: none
  (stub behavior, not harness, produced the failures).
- **GREEN**: implemented `grader_sha256` (FIPS 180-4, NIST vectors incl. streaming split
  8+48), `grader_json` (sorted-member canonical form, shortest round-trip numbers, non-finite
  refusal, strict parse), `grader_types` (three versioned documents + fail-closed validation
  with typed paths/messages), `grader_engine` (validation + budget enforcement; matching and
  scoring deferred to slices B/C by design).
- **Design decisions locked during GREEN**:
  - Validation runs *inside* `fromJson` (callers cannot bypass fail-closed checking);
    `validate()` remains public for value-object authors.
  - Error convention: `path` locates the offending member; `message` names the offending
    value (duplicate ids, evidence ids).
  - `GradeReport::toJson` honors a pre-set `digest` member (engine-built reports round-trip
    byte-identically); fresh builders get the computed digest.
  - Padding fix in SHA-256: 128-byte staging for the two-block case (buffer-overrun caught
    during self-review before any run).
- **Gate**: `./build-grader/test_grader_schema` → **All tests passed (191 assertions in 11
  test cases), two consecutive runs, exit 0**. No new compiler warnings in `src/grader/*` TUs
  (only pre-existing repo-wide QtCrypto include noise). Build at `-j1`, single target.
- Files: `src/grader/{grader_error,grader_sha256,grader_json,grader_types,grader_engine}.{h,cpp}`,
  `src/grader/CMakeLists.txt`, `tests/test_grader_schema.cpp`,
  `docs/adr/0174-process-aware-experiment-grader.md`.
- Central delta so far: root `CMakeLists.txt` +3 lines (one `add_subdirectory` + comment),
  `tests/CMakeLists.txt` +18 lines (light-lane function + one target).
