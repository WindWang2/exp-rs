# BASELINE — Track 9: MCP/Pi Surface Contract Parity (R4)

实测时间：2026-09-27（本地时区）。所有实测值来自 worktree `/home/kevin/project/exp-rs-mcp-surface-r4`（基 `origin/master`）。

## 1. Git 基线

| 项 | 实测值 |
|---|---|
| origin/master SHA | `15e5c66b543ef3874cb929f17529ef456bd6c059`（与写作值 `15e5c66b5` 一致） |
| 领先/落后 | `0 0`（origin/master...master 同步） |
| 工作树 | `../exp-rs-mcp-surface-r4`，分支 `hardening/r4-mcp-surface`；主仓库根目录保持只读 |
| 开放 issue | **0**（`gh issue list --state open` 实测为空，与写作值一致） |

## 2. 计数锚定复核（3.1 表逐项）

| 锚点 | 写作值 | 实测值 | 偏差说明 |
|---|---|---|---|
| `src/agent/spatial_tools` 工具文件 | 21 | **21**（.cpp/.h 逻辑对计数；物理文件 41） | 一致 |
| `spatial:` 前缀**字符串出现** | 8 | **16 个唯一字符串**（含 geometric_registration/list_models/raster_inspect/spectral_inspect/terrain_profile/terrain_viewshed_inspect/validate_boa_physics/vector_inspect 等写作值未列者） | 写作值 8 为**注册名**口径，16 为字符串 grep 口径；真实注册集以 SpatialToolRegistry 运行时为准（WP-A 快照给实测）。写作名单只是子集 + 少量笔误（如 `spatial:inspect_layer` 只出现在 parity 测试里） |
| `mcp_server.cpp` 体积 | 132,788 B | **129,974 B**（2814 行）；路径实测为 `src/agent/mcp_server.cpp`（非 `src/agent/mcp/`） | 写作路径偏差已修正；体积差异为写作时点不同 |
| `surfaceAllowedPrefixes` 调用链 | mcp_server.cpp:1636 → surface_registry.h:78 | **完全一致**（mcp_server.cpp:1636；surface_registry.cpp:204 实现，h:78 声明） | 一致 |
| `pi/exp-rs-spatial.ts` 体积 | 11,454 B | **11,189 B** | 写作时点差异 |
| `EXP_RS_TOOL_CATEGORIES` 默认类目 | 11 | **11**（meta,spatial,data,temporal,cartography,symbology,workflow,workspace,layout,harness,mission） | 一致 |
| C++ 白名单前缀表 | — | **35**（surface_registry.cpp `allowedPrefixTable()`） | 新增实测锚点 |
| pi 测试文件 | 5 | **5**（bridge_parity / fake_mcp_server / mcp_bridge / no_drift / scientific_workflow_compiler_11） | 一致 |
| C++ 表面测试 | 3 | **3**（surface_e2e / surface_parity / surface_protocol；Catch2） | 一致 |
| runner | — | C++：ctest + Catch2（`sicnu_discover_tests`）；pi：`node --test pi/test/`（各测试文件头注释明示） | 实测确认 |
| 构建 | — | cmake 3.30.5（/tmp/cmake-3.30.5-linux-x86_64/bin）+ ninja（/home/kevin/pwb-sdks/root/usr/bin）+ Qt6（/usr/lib/cmake/Qt6）+ `CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`；构建目录 `build-r4`（全新，ENABLE_TESTS=ON，Debug，-j2） | 新增实测 |

## 3. 在途 PR 盘点与 file-overlap map

实测 open PR：#1334(30 文件) / #1335(19) / #1336(13) / #1337(25) / #1338(29) / #1339(38) / #1340(24)。与本轨道白名单相交者：

| PR | 相交文件 | 本轨道策略 |
|---|---|---|
| **#1334**（默认拒绝沙箱，安全轨道） | `src/agent/mcp_server.cpp/h`、`src/agent/tool_catalog/surface_registry.cpp`、`src/agent/tool_catalog/meta_protocol_tools.cpp`、**`pi/exp-rs-spatial.ts`**、`tests/CMakeLists.txt`、`tests/test_surface_protocol.cpp`、`tests/test_surface_e2e.cpp`、`tests/test_mcp_server.cpp` | **不重复其安全改动**。策略：基于 origin/master 开发；本轨道对 mcp_server.cpp 的改动限于帧解析/错误应答/分发健壮性（非 sandbox/trust-gate 逻辑）；对 surface_registry 只读引用不修改；test_surface_protocol 的扩展以**新增 TEST_CASE 为主、不动 #1334 已有段落**（其改动对本轨道主要是新增 section，合并冲突面 = 文件尾部追加区，rebase 成本低）。逐文件理由：见 DECISIONS.md D-1 |
| #1335（master 修复） | `tests/CMakeLists.txt`（ctest discovery） | 本轨道在 tests/CMakeLists.txt 仅追加新测试目标段落；冲突面小，rebase 平凡 |
| #1336/#1337/#1338/#1339/#1340 | 与白名单**零相交**（i18n/io/workflow/operators） | 无需处理 |

`pi/exp-rs-spatial.ts`：#1334 也在改它（README 声明沙箱环境变量），本轨道对其改动限于**测试只读断言所需**（类目字符串提取已由既有 C++ 测试做）——本轨道计划不改 exp-rs-spatial.ts 生产逻辑，只加 pi/test 用例 → 与 #1334 冲突面趋近于零。

## 4. 漂移史取证（git log 实测）

- `071810af3 fix(ci): qualify surfaceAllowedPrefixes in test_agent_tools_3 (#1305)`——编译期漂移（裸调用不在 scope），证明表面测试与注册层的耦合靠人肉维护。
- `bbeafbb86 fix(pi): single shared McpBridge ... no_drift.test.mjs guards the single implementation`——pi 侧双实现漂移的先例与防线。
- mcp_server.cpp 内在协议合同：#620（执行错误走 isError 结果而非 JSON-RPC error）、#634（cancelled→TaskCenter 映射）、#644（显式 null id、bounded maps）、#645（AbortSignal→notifications/cancelled、errorCode 透传）、#701（-32002 未初始化门、compact listing、GUI-hide 单谓词）。

## 5. 三处表面 diff 漂移基线（Phase 0 快照结论）

既有覆盖（不重复建设）：
- `test_surface_parity.cpp` 已含：tools/list == union projection **逐字段**（name/description/inputSchema，canonical JSON 序列化）；registry→projection 全量 sweep（含 schema 相等）；pi 类目 ⊆ surfaceFamilies（单向）；allow-list 拒绝消息渲染前缀表；CLI leg（真实二进制 superset 语义）。
- `test_mcp_server.cpp` 已含：-32601 未知 method；未知工具 -32602；cancel_execution 终态语义。

**实测缺口**（本轨道工作清单）：
0. **【Phase 0 实测新发现】master 的 pi 测试基线红**：`node --test pi/test/` = 19 测试 / 14 过 / **5 红**。5 个全是结构断言落后于 mcp_bridge.ts 单实现重构（failDesyncedStream 爬梯）的陈旧测试：bridge_parity 3 个"both bridges"测试仍期望 exp-rs-spatial.ts 持有传输结构（与 no_drift 的"单实现"守卫自相矛盾）；no_drift 2 个（溢出分支正则期望旧 `this.exited = true` 内联形状 + 行为测试未等 exit 事件即断言 alive===false 的竞态）。**行为本身正确**（mcp_bridge.test.mjs 全过，含轮询版 desync 测试）。修复属本轨道领地（pi/test/），是 Oracle"pi 全绿双跑"的前提 → 记 D-9。
1. **快照锁死机制不存在**：无 `tests/surface_diff_snapshot.json`，无 pi 侧逐工具 parity（pi 测试从不读 wire dump；类目检查只在 C++ 侧单向）。
2. pi 侧映射规则（`piToolName` ":"→"_"、"exprs_" 前缀、类目过滤、wait/status 工具名冲突）**零测试**。
3. 协议健壮性缺口（C++）：-32700 非 JSON 行 / 行超长哨兵、-32600 非对象帧、**缺 method 的请求返回 -32601（JSON-RPC 2.0 规范应为 -32600 Invalid Request——真实缺陷）**、-32002 未初始化门未见测试、notifications/cancelled→任务取消映射（#634）未见测试、显式 null id 应答合同（#644）未见测试、tools/call arguments 非对象 -32602 未见测试、spatial 工具缺必填参/类型错位的结构化 error 未见测试、tools/list 前缀过滤合同未见直接测试。
4. pi 侧健壮性缺口：AbortSignal 竞态（abort 后不 resolve、cancelled 通知恰一次）零覆盖；truncated 标记透传零覆盖；schema 往返零覆盖。

**结论**：三处 diff 在"进程内路径"上已近清零（既有 parity 测试约束），漂移风险集中在 **pi 消费端无逐工具锁死** 与 **快照缺失**。WP-A/B 按 prompt 预案转为"固化防回归"（快照 + 双端比对器），火力按预案转移到 WP-C/D 用例加深。账本记录此重分配。

## 6. 本轨道边界声明

白名单：`src/agent/spatial_tools/`、`src/agent/tool_catalog/`（只读引用为主）、`src/agent/`（mcp_server.cpp/h 协议健壮性）、`pi/`（exp-rs-spatial.ts 不动生产逻辑；pi/test/ 加用例）、`tests/`（含 CMakeLists.txt）、`docs/adr/`、`.planning/mcp-surface-r4/`。
不做：#1334 安全逻辑、新工具/新前缀/新类目、`src/workflow`、`src/agent/harness`、`src/operators`、等待线上 CI。
