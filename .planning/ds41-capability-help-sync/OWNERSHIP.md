# OWNERSHIP — ds41-capability-help-sync

## 可写（本 Track 主 owner）

| 范围 | 用途 |
|---|---|
| `data/processing/algorithm_meta/**` | Layer-A sidecar、Layer-B `capability/` sidecar 与关系图的重新生成、authored 键补全、损坏文件修复 |
| `data/processing/algorithm_meta/README.md` | 生成纪律说明、ADR 引用修正 |
| `pi/knowledge/capability-*.md`, `pi/knowledge/capability-index.md` | 仅由 `capability_knowledge_tool gen-pages` 重新生成 |
| `tests/test_capability_surface_parity.cpp`（新增） | surface parity gate |
| `tests/test_capability_completeness.cpp`（新增） | help/capability 完整性 gate |
| `tests/test_algorithm_meta_drift.cpp` | 仅更新事实性 pin（43→实测值，附证据注释） |
| `tests/CMakeLists.txt` | append-only 注册两个新测试 |
| `.planning/ds41-capability-help-sync/**` | 本 Track 规划文档（markdown） |
| `.gitignore` | append-only：un-ignore 本 Track planning 目录（按仓库惯例） |

## 只读（生产实现，禁止为对齐 metadata 改算法行为）

- `src/operators/**`（含 `rs_schema.h`、所有 operator 实现、`rs_operator_error.h`）
- `src/processing/framework/algorithm_meta_store.*`、`algorithm_descriptor.*`、`atomic_algorithm_*`（生成器本体属于共享基础设施；如需 bug 修复必须最小化并在 PR body 标注）
- `src/agent/harness/capability_catalog.*`、`capability_knowledge.*`、`capability_pages.*`、`capability_relations.*`（同上）
- `src/cli/**`、`src/agent/mcp_server.cpp`、`src/agent/tool_catalog/**`、`src/help/**`（surface 实现只读；parity gate 通过测试侧断言，不改投影行为）
- `data/agent/capabilities/**`（ADR 0142 可行性层，Scientific Verification Track 协作面）
- `CMakeLists.txt` 根文件、各 src CMakeLists（除 tests 外层外不碰）

## 共享 append-only

- `CHANGELOG.md`（如仓库规范要求）
- `.planning/` 其他 track 目录：只读

## 冲突对策

发现并行 PR 已修改共享文件（尤其 `tests/CMakeLists.txt`、`data/processing/**`）时：先 rebase 到最新 `origin/master`，做最小集成，不复制他人实现。
