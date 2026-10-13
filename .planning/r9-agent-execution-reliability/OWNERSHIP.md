# R9 Ownership（方向 B）

## 本分支拥有（可修改）
- src/sdk/exprs/**（WP-A ipc_channel、WP-B external_process、WP-C plugin_registry）
- src/operators/runtime/**（WP-D model_runtime）
- src/processing/framework/task_center.*（WP-E；仅 task_center 本体，不动 C 的测试基建）
- src/python/isolated/**（WP-F worker pool / adapter）
- tests/ 下新增定向测试（按现有 CMake 约定注册；tests/CMakeLists.txt 若需追加仅做最小 append）
- .planning/r9-agent-execution-reliability/**（本目录）

## 不拥有（绝不修改）
- src/analysis、src/geospatial、src/data、src/dataset、src/experiment（A）
- src/app、src/teaching*、src/help、src/faultlab、src/science_context、src/preflight、tests 基建（C）
- src/core/**、根 CMakeLists、公共 tests 基建：只读；如必须改，PR 里单列"公共文件最小兼容片段"
- pi/**：本轮预计只读（如需改 pi/ 桥接，单独 work package 评估）
- master 分支本身（只读）

## 冲突矩阵（path + symbol）

| 公共/相邻文件 | 本分支接触方式 | 冲突风险与处理 |
|---|---|---|
| src/core/radiometric_state.cpp（PRRT_...WRz） | 只读 | 归 A；PR 描述转交 |
| tests/CMakeLists.txt | 仅当新增测试文件时 append 注册 | 与 A/C 并行 PR 可能文本冲突 → 保持最小追加、放文件末尾固定锚点 |
| src/processing/framework/**（非 task_center） | 只读（output_committer 等已修勿动） | 无 |
| src/plugins/** | 只读（#1462 侧已修） | 无 |

## Subagent 名额（≤3）
1. Explore ①：PR review threads + B1 审计（已完成）
2. Explore ②：Issue→master 映射（已完成）
3. 预留：独立对抗式 review（两轮复用同一名额，轮换 reviewer 角色；实现由主 Agent 完成，reviewer 未接触补丁）
