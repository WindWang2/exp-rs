# Recon — teaching foundation at master `a9dc33fa7`

## Reference chain (who loads what)

```
data/curriculum/undergraduate_rs.curriculum.json (sicnu.curriculum/1)
  └─ src/agent/harness/curriculum_catalog.cpp  resolveLabReference / validateCurriculumManifest
     ├─ lab_id → data/labs/<id>.lab.json  (shallow probe: id==stem, spec_version, steps[])
     ├─ lab-registry.json canonical/aliases → canonical source file
     └─ out_of_scope → "external"
data/labs/lab-registry.json (sicnu.lab-registry/1)  ← scripts/check_lab_registry.py (gate)
  ├─ canonical[].source → *.labspec.json (D3)
  ├─ canonical[].grading_rules/pipeline/data_spec → grading/ pipelines/ data-specs/
  ├─ alias_packs{legacy pack 名 → canonical id} ↔ packs/<name>.pack.json
  └─ wrapper .lab.json carries DUPLICATE grading pointers (now parity-checked)
data/labs/*.lab.json (D2 v1/v2[/v3])  ← loader src/app/widgets/lab_spec_loader.cpp (v3 shallow)
data/labs/*.labspec.json (sicnu.labspec.v1)  ← src/recipes/lab_document.cpp (parse)
  └─ step-less wrapper + registry → mergeWrapperOverD3 (src/recipes/lab_source.cpp)
src/lab/spec_runtime.cpp  ← v3 `runtime` block deep validation (typed lab.runtime.*)
src/lab/session_store.cpp ← <root>/<labId>/<studentId>-<seq>.session.json, tmp+rename atomic
scripts/gen_lab_packs.py → packs/*.pack.json (sicnu.lab-pack/1)
  └─ src/agent/lab_data_pack.cpp verifies: committed sha256 hard, generated soft
data/agent/scientific_recipes/*.json ← recipe compiler committed artifacts (drift gate:
  tests/test_recipe_equivalence, byte-level) — NEVER RAN on master (module not compiled)
tools/sample_foundry.cpp → data/samples + manifest.json (per-host determinism, ADR 0164)
```

## Status matrix (现状矩阵)

| 组件 | 权威数据源 | 调用者 | 错误模型 | 现有测试 (master 状态) | 本 slice 后 |
|---|---|---|---|---|---|
| curriculum harness | curriculum json | sicnu_agent (registered) | typed issues, fail-closed | test_curriculum **未注册** | registered (heavy lane) |
| lab-registry | lab-registry.json | catalog, recipes, py 脚本 | py exit1; C++ lab_source 解析失败静默空 view | check_lab_registry.py 手跑 | + v3 门禁 + pointer parity |
| LabSpec loader | .lab.json | app | version-strict, typed | test_labspec (registered) | unchanged |
| v3 runtime (sicnu_lab_runtime) | runtime 块 | 无生产消费者（by design） | LabResult typed, "never throws" | test_lab_runtime **未注册** | registered (Qt-free lane) |
| session store | session.json | 仅孤儿测试 | typed corrupt/io/spec_drift; tmp+rename | 同上 | + 溢出/io 修复 + Windows fsync |
| recipe compiler (sicnu_recipes) | LabDocument+catalog | **无（模块从未编译）** | closed diag vocabulary | test_recipe_* 全部未注册 | target created, 8 suites registered |
| committed recipes | data/agent/scientific_recipes | 无 | — | drift gate 未运行 | drift gate live |
| sample foundry | 固定 catalog+manifest | CLI + test_sample_fixtures | typed fail, CLI exit codes | registered | + pin 恢复 |
| packs | packs/*.pack.json | lab_data_pack verifier | committed hard / generated soft | test_lab_data_pack (registered) | generator 机器无关化 |
| grading fixtures | tests/fixtures/lab + rules | output_verifier | 校验失败=usage | test_lab_grading/kernels (registered) | + corpus sha256 E2E 抽查 |

## 历史/并行线索

- `agent/flash-lab-foundry-determinism`（tip 150336cd0，基点 2caac836c）：对
  `tools/sample_foundry` 的 F-1032-P1 三修复（hostile GDAL env / shapefile
  sidecar 删除 / stale prune）功能层面 master 已全部具备；**仍 live 的缺口是
  pin 不恢复现场 + ADR 0164 缺恢复条款** → 本 slice S13 修复（RAII +
  ADR append），其余不移植。
- `rs14-unified-verifier`：旧平行实现，现代 master 权威是 `src/verify`；未触碰。
- Open PR 去重（创建 PR 前将复核）：#1237 owns `src/teaching/**`（未触碰，只读其
  recon——其 LabSessionState 消费 session_store 的 typed 失败面，S5/S6 修复对其
  是纯强化）；#1239 teaching_admin、#1240 science_context（recipe_router 是
  recipes 的潜在消费者，只读方向）；#1238/#1240/#1241/#1237 都把根
  `CMakeLists.txt` 与 `tests/CMakeLists.txt` 当 append-only 共享文件——本 slice
  同样只做追加（foundation lane 追加在 tests/CMakeLists.txt 文件尾，
  `add_subdirectory(src/recipes)` 追加在 src/lab 之后），union merge 风险最小。
