# R16-B Issue / PR / Branch Matrix (recorded 2026-10-17, baseline 46791bc65)

## Open PRs and file-conflict surface

| PR | Track | Files vs R16-B ownership | Conflict risk |
| --- | --- | --- | --- |
| #1513 (r15-b, DRAFT) | geodata/dataset/experiment persistence | none in `science_context`/`scientific_state`/`suitability`/`preflight`/`planner`/`repair_planner` | none |
| #1514 (r15-a) | science algorithms | none in ownership | none |
| #1515 (r15-c) | model runtime / GPU / chunk | touches `tests/CMakeLists.txt` | **avoided**: R16-B adds no CMakeLists edits (tests extended inside already-registered TUs) |
| #1516 (r15-d) | Pi/MCP/workflow | none in ownership | none |

## Issues touching track 03/04 semantics — verified state on master

| Issue | Item | Evidence state on master @46791bc65 |
| --- | --- | --- |
| #1465 | broker cache key drops typed failure cause | **已修复/fixed** — `src/science_context/broker.cpp:301-318` (`failureDigest` in `CacheKeyMaterial`, inline `#1465` comment) |
| #1465 | 4× 3-arg `std::equal` OOB | **已修复/fixed** — `src/scientific_state/asset_state_json.cpp` length gates at the four `*FromString` matchers (inline `#1465` comments) |
| #1465 | static-init file I/O throw | **已修复/fixed** — `src/recipes/recipe_registry.cpp` `defaultDirectory()`/`reload()` use `std::error_code` forms; comments cite #1465/#1463 |
| #1465 | terminology dedup suffix unused | **未验证/不属于本轨道** — `src/help/terminology_provider.cpp` outside both R16-B tracks; left for its owner |
| #1466 | suitability integral fail-open (P1) | **已修复/fixed** — `src/suitability/suitability_goal.cpp:55-72` `boundedIntegral` fails typed; regression `tests/test_suitability_adversarial.cpp:846,1567` |
| #1447 | `capability_router.cpp:613` asInt overflow | **已修复/fixed** — `isInt()` gate in `authoritySpecFromCandidate` (inline `#1447` comment) |
| #1447 | `preflight/capability_mirror.cpp:23` parseFromStream throw | **已修复/fixed** — `parseText()` try/catch + `stackLimit` (inline `#1450` comment) |
| #1447 | other 7 sites (help/faultlab/agent_ops/sdk/cli) | outside R16-B ownership; untouched |
| #1505 review | capability facts stable lifetime (P1) | **可复现/OPEN** — `broker.h:65-69` raw pointer into optional; consumer `agent_adapter.cpp:208`. → WP-T03-1 fixes it |
| #1505 review | recipeSearch not under broker lock (P1) | **已缓解/mitigated** — `RecipeRouter` internally synchronized (`search()` :231, `sourceInfo()` :107, swap under lock :128); WP-T03-2 adds the missing regression |
| #1467 | silent scene-read failures → NaN (core-fork) | outside ownership (src/core); not touched |
| #1488 | canonical_json INT64 overflow | verify/grader module; outside ownership |

## Local worktrees discovered (`git worktree list --porcelain`)

- Pre-existing (other tasks; never reset/cleaned): `hardening/planner-repair-capability-graph`, `hardening/verifier-grader-explain-evidence`, `agent/cpp-build-run-13`, `agent/ds41-http-fetch-strict`, `agent/rs14-experiment-debugger`.
- R16-B (this task): `exp-rs-r16-b-t03`, `exp-rs-r16-b-t04`, `exp-rs-r16-b-integration` — all cut fresh from `origin/master`.
- Sibling R16 waves on this host (`exp-rs-r16-a-t01/t02`, `exp-rs-r16-c-t05`, …) belong to the other four prompts; their worktrees, build dirs and branches are never written by R16-B. The shared cross-prompt build lease `/home/kevin/project/exp-rs-r16-build.lock` serializes expensive configures/builds.
