# PR / ISSUE / REVIEW MATRIX — R10-A

Status vocabulary: FIXED_IN_MASTER / IN_OPEN_PR / STILL_REPRODUCIBLE / OUT_OF_SCOPE / UNCERTAIN / PENDING_MAPPING.
Master = `8a0864276` (2026-10-13). Review-thread claims re-verified against source, not labels.

## Issues

| issue | domain overlap w/ R10-A | sub-item status on master | action here |
|---|---|---|---|
| #1461 analysis cluster (4×P1+4×P2) | full (A2/A3/A5) | **all 8 FIXED_IN_MASTER** (see BASELINE.md evidence table; #1469/#1472 landed fixes with in-source #1461 comments) | none — re-verified, do not re-fix |
| #1455 operators runtime/io cluster | partial: `io_fabric_operators.cpp`, `otb_operator_base.cpp` | P1 `model_runtime.cpp:941` → IN_OPEN_PR #1477 (excluded). P2 io CancelToken → **FIXED_IN_MASTER** (`contextCancelToken(context)` wired + progress). P2 OTB watchdog → **FIXED_IN_MASTER** (`SICNU_OTB_MAX_RUNTIME_S`, comment cites #1455) | none on those; keep as regression-check targets |
| #1447 untyped jsoncpp (9 sites) | none of the 9 sites are in R10-A paths (help/faultlab/science_context/agent_ops/sdk/cli/preflight) | sites OUT_OF_SCOPE individually; pattern relevant | Wave-A sweep of own domain for same pattern; fix only reproduced hits |
| #1450 single-authority drift (SHA-256 forks etc.) | read-only / shared contract | PENDING_MAPPING (which forks live in R10-A paths?) | read-only; prefer shared contract, no local dialect |
| #1454 task-center cluster | excluded (#1477 owns task_center.*) | IN_OPEN_PR #1477 | none |
| #1456 geospatial egress/cache | `query_planner.cpp` excluded (#1475) | PENDING_MAPPING for non-excluded sub-items | check overlap; likely OUT_OF_SCOPE |
| #1460 cli/sdk external_process | excluded (#1477 sdk/exprs) | IN_OPEN_PR #1477 | none |
| #1447/#1448/#1449/#1453/#1457/#1458/#1459/#1462–#1468 | mostly stores/app/help/teaching/agent_ops/python/platform | PENDING_MAPPING (subagent to classify vs R10-A surface) | classify only; fix only in-scope STILL_REPRODUCIBLE |

## Open PR file/semantic conflict matrix (R10-A owned paths × in-flight PRs)

| path family | #1475 | #1476 | #1477 | conflict |
|---|---|---|---|---|
| src/analysis/** | — | — | — | none |
| src/operators/{rs,gdal,otb,opencv,io}/** | — | — | — | none |
| src/operators/framework/model_catalog*, rs_json_params.cpp | OWNED | — | — | excluded here |
| src/operators/runtime/model_runtime.* | — | — | OWNED | excluded here |
| src/processing/algorithms/temporal/temporal_workspace.cpp | OWNED | — | — | excluded here |
| src/processing/framework/task_center.* | — | — | OWNED | excluded here |
| src/processing/{algorithms!(temporal),providers,features,contracts}/** | — | — | — | none |
| src/runtime/chunk/** | — | — | — | none (verify #1477 doesn't touch) |
| data/processing/** | — | — | — | none |
| tests/CMakeLists.txt | — | — | — | shared hot file: serial, minimal increments |
| CHANGELOG.md | — | — | #1477 touches | append-only edit at PR time, rebase if needed |

Semantic conflicts to watch: #1475's `rs_json_params.cpp` (file URI) and `model_catalog` changes
interface with our operator params handling — read their final shape after merge; do not fork dialects.

## Review threads

- #1475 open threads on `query_planner.cpp` (maxItems truncation) and `rs_json_params.cpp` (file URI):
  owned by #1475 author — do not touch, do not mark resolved.
- #1474's five remaining threads → #1476 (not our domain).
