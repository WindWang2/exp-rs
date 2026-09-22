# Recon — RS14-08-capability-graph

Track: Capability Graph 遥感能力状态转换图
Baseline: origin/master `4f6632e1f` (PR #1145 merged). Worktree: `../exp-rs-wt-rs14-capability-graph`, branch `agent/rs14-capability-graph`.

## 0. 动态去重（执行时实测）

- `git fetch origin --prune` 完成；`origin/master` = `4f6632e1f`，与 prompt 基线一致，master 未前移。
- Open PR：**0 个**（gh pr list 实测为空）。
- Open issues：#1146–#1187 与 prompt 避开清单一致，全部避开。
- 并行 worktree 实测存在：`exp-rs-wt-rs14-scientific-planner`、`exp-rs-wt-rs14-verifier`（同批 RS14 方向）→ 本 track 必须独立 build 目录、独立数据文件命名空间。

## 1. 已有能力（代码考古结论）

### 1.1 稳定只读投影源
- **`AtomicAlgorithmRegistry`** (`src/processing/framework/atomic_algorithm_registry.h`)：线程安全单例，`listDescriptors()` 是权威枚举。
- **`AlgorithmDescriptor`** (`src/processing/framework/algorithm_descriptor.h`)：**Qt-free**（仅 jsoncpp/string/vector）。字段：id/displayName/group/inputs/outputs(`PortDescriptor` 含 `DataType`、`rsContract`=`x-rs-contract`)/`AgentMetadata`（tags、purpose、costClass、largeRasterSafe、facadeOf、taskFamily…）。稳定性行为级保证（ADR 0124 byte-reproducible、`algorithm_descriptor_validator.h`）。
- **`x-rs-contract`**（端口级）：dataKind/bands/gridRelation/radiometricState/categorical/noData/crs —— descriptor 自带机器可读事实。

### 1.2 已有 JSON 知识层（均不动，只读参考）
- `data/processing/algorithm_meta/*.json`（v1 sidecar 57 个，`AlgorithmMetaStore`，descriptor-wins 合并）。
- `data/processing/algorithm_meta/capability/`（**157 个 v2 sidecar** + `capability_relations.json`，`CapabilityCatalog` 强校验 `schema_version==2`；由 `scripts/capability_knowledge_tool.cpp gen-meta` 生成，drift 字节门禁）。
- `data/agent/capabilities/*.json`（15 个手写 family 条目，`CapabilityKnowledge`；**#1151 红灯区，绝不修改**）。radiometric 形态：`{"acceptable":["dn","toa"],"warn":["surface_reflectance"]}`。
- **`capability_relations.json`**（ADR 0154）：`{chains:[{from,to,when:{radiometric_state:[...]},why}], exclusive, requires_shared_grid, grid_fixer}` —— **最接近的先行物**，但它是"算子→算子"静态授权关系图，不是一阶数据状态转换模型。

### 1.3 命名冲突警报
- **`capability_graph` 名字已被占用**：`src/agent/harness/capability_graph.{h,cpp}` 是 Harness 7.0 的 intent→capability 解析器（`resolveGoalIntent`/`evaluateFeasibility`/tool `harness:resolve_intent`）。本 track 模块命名 **`capability_state_graph`**（目录/目标/命名空间均不冲突，namespace `sicnu::capability_state`）。

### 1.4 已有状态词表（复用，不新造）
- **ADR 0114 五态封闭词表**（`src/processing/algorithms/satellite_products.h:212-218`）：`digital_number / radiance / toa_reflectance / surface_reflectance / brightness_temperature`，metadata key `SICNU_RADIOMETRIC_STATE`。
- **合法辐射转换 DAG**（`radiometric_transition.{h,cpp}`）：dn→radiance→toa→sr、dn→toa、radiance→bt；一切逆变换非法。本图与它语义对齐（信息不可逆=非法边），但它是**物理合法性权威**（QString/Qt 域），本图是**能力语义投影**（Qt-free），二者通过词表常量字符串对齐，不代码级依赖。
- capability knowledge 使用短别名 `dn/toa`；本图词表用长规范形，loader 内做别名归一（dn→digital_number, toa→toa_reflectance, sr→surface_reflectance, bt→brightness_temperature），文档明示。

### 1.5 已有"为什么不可用"层（定位差异，不重复）
- 五处 preflight：`scientific_preflight`（intent 级）、`algorithm_preflight`（PLAN→PREFLIGHT→EXECUTE，参数+数据事实）、`eo_preflight`（模型）、`temporal_preflight`、`workflow_preflight_tool`。
- `workflow_explain`（Compiler & Grounding 11）：决策溯源 zh-CN 模板。
- 它们回答"**这一次执行**为什么失败"；本图回答"**能力空间里**为什么 B 之前必须 A / 还有什么路径"——静态语义图 vs 运行时检查，互补不重叠。

### 1.6 可复用的工程接缝
- **Qt-free STATIC 目标模板**：`src/contracts/CMakeLists.txt`（C++20 + jsoncpp canonical-target-with-fallback 块，`target_include_directories PUBLIC ${CMAKE_SOURCE_DIR}/src`）。
- **Agent 工具接缝**：`SpatialTool` 契约（`src/agent/spatial_tools/spatial_tool.h`，inputSchema/outputSchema 为 `Json::Value`，`execute` 返回 typed `SpatialToolResult`），`SpatialToolRegistry::registerBuiltinTools()` 注册即自动经 MCP `handleSpatialToolCall` + `AgentToolCatalog` 暴露，**无需改 mcp_server.cpp**。已有 `spatial:search_capabilities`（capability_tools.cpp）——只搜 knowledge 条目，无状态图查询。
- **测试**：Catch2；`sicnu_add_io_test` 证明轻量注册可行；jsoncpp 安全读法 = `stackLimit=128` + try/catch + `isIntegral()`（`src/runtime/worker/worker_protocol.h:164-195`）。
- **versioned schema 惯例**：`inline constexpr` 当前版本常量 + 缺失/未来版本硬拒绝（model manifest、MapSpec、experiment store 均如此）。
- ADR 编号：现有最高 **0171**（CLAUDE.md 的"从 0166 起"已过期，以 ls 为准）→ 本 track 用 **0172**。
- `.planning/` 无统一小写惯例（各 track 自定）；按本 track prompt 要求使用 recon.md/plan.md/slices.md/progress.md。
- `docs/integration.md` 不存在 → 本 track 新建，写接线点。

## 2. 缺口（本 track 要补的真实空白）

1. **没有一阶数据状态图**：状态（数据事实合取）是节点、算子是转换边——现状只有算子→算子关系（capability_relations）与运行时 preflight，缺少可查询的状态语义层。
2. **没有 explain "为什么要先 A 再 B"** 的结构化 API（workflow_explain 是运行时决策溯源，非能力空间解释）。
3. **没有结构化路径候选 API**（compose_chain 是授权链查找，不是状态空间搜索；且以算子为中心）。
4. 边级语义元数据（information loss、assumptions、caveats、evidence/verifier hooks）无处承载。

## 3. 不做什么（边界）

- 不修 #1151 capability mirror / #1187 contract scanner / #1140 algorithm search；不修改 `data/agent/capabilities/**`、`data/processing/algorithm_meta/**`、`capability_relations.json` 任何字节。
- 不建第二套 Registry / 第二套 provenance / 第二套 experiment store；一切节点/边事实投影自 Registry descriptor 或 authored transitions.json。
- 不实现 planner policy：`shortestPath` 只给结构候选（显式 `structural_only:true`），不做成本优化决策。
- 不做 GUI dock（vendored QGIS 全量链接在 20-track 并发 + 无 ccache + -j1/-j2 约束下不可承受；以 Qt-free explanation API + docs/integration.md 接线点替代，PR 中显式声明此限制）。
- 不改 mcp_server.cpp（SpatialTool 注册即达）；不改 src/cli/cli_commands.cpp（20 track 并发下避免热点大文件冲突）。
- 不引入网络依赖；全部离线。

## 4. 风险

| 风险 | 缓解 |
|---|---|
| 与 capability_relations 语义重叠被质疑重复 | ADR 0172 明确三轴差异：状态中心 vs 算子中心；边元数据承载；查询类型。只读引用不修改 |
| 双词表（dn/toa 短别名 vs 长规范形）造成混乱 | loader 归一 + 验证器拒绝未知值 + 文档表 |
| 全量构建不可承受 | Qt-free 核心 + 独立轻量 test target（Catch2+jsoncpp+本库闭包）；`cmake --build --target` 只编译必要闭包 |
| authored transitions 与未来算子漂移 | validator 的 dangling-operator 检查做成可报告 finding（graph 层面允许"算子不在当前投影中"的降级报告，不 crash、不静默）；G slice 一致性测试守门 |
| 20 track 并发 root CMakeLists 冲突 | 单行 `add_subdirectory` delta，rebase 时 union 验证 |

## 5. 与其他 19 tracks 的接口

- 上游只读消费：`AlgorithmDescriptor`（稳定）、`AtomicAlgorithmRegistry::listDescriptors()`（稳定）。
- 下游提供：`sicnu_capability_state_graph`（Qt-free STATIC）的查询 API；`capability_state:*` SpatialTool 族（read-only）。
- 未来接线点（写入 docs/integration.md）：GUI dock、dataset observed-state provider（从 `SICNU_RADIOMETIC_STATE` 等 metadata 读取真实观测态）、mission/workflow 编译器的结构候选输入。
- 若并行 track 需要观测态：本 track 定义 `ObservedState` value object（纯 map），不依赖任何尚未存在的类型。

## 6. 与 open issues 去重矩阵

| Issue | 关系 | 动作 |
|---|---|---|
| #1151 capability mirror 缺 11 算子 | 红灯区 | 完全不触碰该目录/测试；本图独立 authored metadata，不依赖其完整性 |
| #1187 contract projection 9 RED | 红灯区 | 不触碰 src/contracts 任何文件；本图不使用 ContractDescriptor |
| #1140/#1145 search/registry | 只读复用 | 只调用 listDescriptors()/AlgorithmSearch 既有事实，不修改 |
| #1146/#1147 SAR 科学性 | 相关事实 | transitions.json 的 SAR 条目只按**当前已声明契约**撰写，不"顺手修正"科学错误 |
| #1161-#1184 其余 | 无关 | 不触碰；若实现中发现相关现象记录为 observed，不修复 |
