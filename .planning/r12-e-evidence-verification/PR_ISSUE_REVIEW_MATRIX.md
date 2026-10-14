# R12-E PR / Issue / Review Matrix (re-adjudicated on BASE_SHA 28abef3582)

Method: every claim re-read in current source at file:line; merged-PR review threads checked via GraphQL (not REST inference). Verdicts: LIVE / ALREADY_FIXED / OUT_OF_SCOPE.

## Open PRs as active writers

- **#1483 (R12-A)**: files = src/analysis/**, src/core/temporal_cube.cpp, src/operators/rs/**, src/processing/algorithms/**, tests/CMakeLists.txt, science test files. Review threads: 0. Collision risk with R12-E: only tests/CMakeLists.txt (append-only, distinct anchors).
- **#1484 (R12-C)**: files = pi/**, src/runtime/chunk/scratch_registry.*, src/science_context/asset_state_provider.*, tests/CMakeLists.txt, 2 test files. Review threads: 0. `asset_state_provider.*` explicitly off-limits for R12-E.

## Issue-by-issue adjudication

### #1464 quality(verify,grader,faultlab) — 2×P2
| Claim | Verdict | Evidence @BASE |
|---|---|---|
| grader_matcher.cpp recordedAtUtc raw string compare | ALREADY_FIXED | grader_matcher.cpp:113 `parseSequencingStamp` — closed ISO-8601 subset, civil-date/leap/offset/fraction validation, instant compare `atOrBefore` (:256-290); invalid stamp → Unprovable, never byte-order verdict |
| fault_registry.cpp:158 `cell_size` accepted-but-inert | ALREADY_FIXED | fault_transforms.cpp:367-453: `cell_size` type/finite/>0 validated, written to `grid.extras["leakage_cell_size"]`; consumed by fault_observables.cpp:192. Remaining audit: parity for ALL declared-optional params → E04 audit |

### #1450 quality(authority) — P1/P2 cluster
| Claim | Verdict | Evidence @BASE |
|---|---|---|
| workflow_checkpoint.cpp unbounded parse | OUT_OF_SCOPE (C) | src/workflow owned by R12-C; commit d5ab7bc0b0 claims closure |
| faultlab canonical_json LC_NUMERIC | ALREADY_FIXED | ClassicNumericLocale per-thread pin, canonical_json.cpp:~60-110 |
| Four SHA-256 forks | **LIVE** | md5-distinct: verify_sha256.cpp, preflight/sha256.cpp, grader_sha256.cpp, faultlab/util/sha256.cpp; no cross-module parity gate located → E06 |
| radiometric vocabulary double-track | OUT_OF_SCOPE (A) | src/core/radiometric_state.cpp + src/operators/rs — R12-A domain |
| suitability digests not RFC8785 | ALREADY_FIXED | suitability_report.cpp:73, suitability_goal.cpp:341 use canonicalizeJsonRfc8785 |
| experiment_studio version gate `&&` | OUT_OF_SCOPE (D) | src/experiment_studio — R12-D domain (also #1458) |

### #1447 bug(json) — P1 cluster (9 sites; only R12-E-owned sites listed)
| Claim | Verdict | Evidence @BASE |
|---|---|---|
| fault_transforms.cpp role_a/role_b untyped | ALREADY_FIXED | Type gates → typed refusal faultlab.fault_unsupported_params (~:93-135) |
| preflight capability_mirror.cpp:23 unguarded parse | ALREADY_FIXED | stackLimit=1000 + try/catch Json::Exception (capability_mirror.cpp:13-38) |
| fault_scenario.cpp:400 same shape | **LIVE** | loadFaultScenarioFile: no stackLimit key, no try/catch; grader_json.cpp:224 documents CharReader throws on nesting bomb; fault_types.h declares never-throws API → E05 RED target |
| help/terminology/science_context/agent_ops/sdk/cli sites | OUT_OF_SCOPE (C/D) | owner directions |

### #1465 bug(help,science_context) — 4×P2
| Claim | Verdict | Evidence @BASE |
|---|---|---|
| asset_state_json.cpp 4× 3-arg std::equal OOB | ALREADY_FIXED | length-gate + comment at :164/:198/:237/:276; regression test tests/test_scientific_state_nul_tokens_r8.cpp exists |
| broker cache key / terminology dedup / agent_adapter static IO | OUT_OF_SCOPE (C/D) | science_context + help |

### #1466 bug(teaching,suitability) — P1+P2
| Claim | Verdict | Evidence @BASE |
|---|---|---|
| suitability_goal.cpp integer fields fail-open (P1) | ALREADY_FIXED | boundedIntegral fails closed on non-double/out-of-range (suitability_goal.cpp:~50-75) |
| teaching_admin/teaching items | OUT_OF_SCOPE (D) | script_adapters, batch_assessment, lab_session_state |

### #1448 (stores fail-open) / #1468 (Windows test hygiene)
OUT_OF_SCOPE for R12-E (workflow/stores = B/C; CI test-infra = G). #1468 Windows lane marked NOT_RUN (no Windows host).

## Review-thread resolution state
GraphQL reviewThreads on #1483/#1484: zero nodes → nothing to re-adjudicate. No `resolution=UNKNOWN` carryovers for R12-E files from merged PRs located so far; any found during audits will be logged here.
