# OWNERSHIP — unified-contract-verification-9

## This track owns (creates or extends)

| Area | Paths | Notes |
|------|-------|-------|
| Contract descriptors / projections / generators | `src/contracts/` (new), `src/contracts/…` CMake target `sicnu_contracts` | Canonical descriptor, projection builders, contract graph. New top-level dir → zero overlap with the five open `-9` PRs. |
| Contract verification tests | `tests/test_contract_platform_9.cpp`, `tests/test_contract_projection_9.cpp`, `tests/test_command_contract_9.cpp`, `tests/test_diagnostics_contract_9.cpp`, `tests/test_contract_mutation_9.cpp` | New files only. |
| Operator param source scanner | `src/contracts/operator_param_scanner.{h,cpp}` (+ used by tests) | C++ implementation (repo precedent: test_shortcut_conflicts scans sources in C++). |
| Verification ladder extension | `scripts/verification_ladder.py` (additive: new L2 items), `scripts/collect_readiness.py` (additive) | Minimal diff, no lane renumbering. |
| Contract data projections | `data/contracts/` (new, generated + committed snapshots) | Byte-compare targets for mutation tests. |
| Help/diagnostics/command guards | new tests only; **no** semantic edits to `src/help`, `src/app` behavior | Fixes limited to what a contract projection can repair (e.g. missing help JSON entry) without touching UI behavior owned by other tracks. |
| Docs | `docs/verification/CONTRACT_PLATFORM_9.md`, CHANGELOG entry (end of track, minimal) | |

## Explicitly NOT owned (other tracks / master authorities)

- `src/operators/**` runtime semantics (scientific-algorithms-9 #883,
  model-runtime-multimodal-9 #884). This track **reads** operator sources for
  the scanner and links the registry in tests; it does not rewrite operator
  behavior. Exception: adding a *missing schema field* is allowed only when a
  scanner finding proves the implementation already accepts the parameter
  (schema catches up to implementation — the exact #872/#879/#880 pattern),
  and only for drift not owned by an open -9 PR.
- `src/agent/harness/**` semantics (spatial-scientist-harness-9 #885). The
  preflight action-id guard is a *test-side* projection; if it finds a bad
  action id we file it in REVIEW_LOG with a failing test and fix only when
  unowned.
- `src/cli/**` (scientific-mlops-9 #886) — read-only scan surface.
- `src/geospatial/**` (geospatial-data-fabric-9 #887) — untouched.
- `src/app/**` UI behavior (Workbench track) — read-only scan surface; no
  empty-state/menu semantic changes from this track.
- Scheduler (`WorkflowRunCoordinator → TaskCenter → JobEngine`), Pi runtime,
  QGIS rendering authority, Dataset/Experiment stores, plugin host — all
  read-only.

## Shared-file policy

`CMakeLists.txt` (root + tests/) and `CHANGELOG.md` are shared with the five
open PRs: edits are deferred to the final milestone of each logical unit,
kept minimal (one target add, one test registration block), and rebased just
before PR. `scripts/verification_ladder.py` similarly gets one additive
commit at the end of M5.

## Cross-track bug protocol

Found a business bug while projecting contracts:
1. Capture minimal evidence (file:line + failing contract test).
2. Fix in-place only if it is a *contract projection* gap (schema/help/data
   missing an already-accepted fact).
3. Otherwise record in `REVIEW_LOG.md` with the failing test name and leave
   the semantic fix to the owning track.
