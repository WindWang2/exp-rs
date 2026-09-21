# Plan — RS14-08-capability-graph（Capability State Graph 遥感能力状态转换图）

## 1. Problem statement

平台把 70+ 算子以"列表/搜索"暴露（Toolbox、`search_algorithms`、capability sidecar）。学生和 Agent 只能看到"有哪些算子"，看不到"数据处在什么状态、哪个算子能把数据带到哪个状态、为什么做 B 之前必须先做 A、走这条路丢了什么信息"。已有的 capability_relations 是算子→算子授权关系；五处 preflight 是运行时单次执行检查。缺失的是**能力空间的一阶数据状态语义图**：状态为节点、算子为转换、边承载假设/信息损失/资源/科学告诫/验证钩子。

## 2. User stories

**本科生实验视角**
- US1：导入 DN 影像想直接算 NDVI → 系统解释"当前 radiometric_state=digital_number，rs:ndvi 需要 toa_reflectance/surface_reflectance"，并给出先 rs:radiometric_calibration 再 rs:atmospheric_correction 的准备路径与每步理由（含 warn：DN 直接算指数会被标记质量降级）。
- US2：同一算子不同参数到达不同状态（rs:radiometric_calibration 的 reflectance/radiance/brightness_temperature 三分支）→ 图中是三条边，教学上可见"定标不是一件事"。
- US3：查看任意路径的"信息损失"说明 → 理解为什么 SR→TOA 逆变换永远不可行（教学点：信息不可逆）。

**AI Agent 视角**
- US4：machine-readable 查询"当前观测态下哪些能力可用/不可用、不可用缺哪些事实"（typed result，无 silent fallback）。
- US5：结构化最短路径候选（显式 `structural_only` 标记，不含 planner policy），供未来 planner 消费。
- US6：只读、确定性输出、schema versioned，可离线。

## 3. Architecture

```
                ┌────────────────────────────（只读）────────────────────────────┐
AlgorithmDescriptor ──extract──▶ OperatorFacts ──project──▶ ┌──────────────────────────────────────┐
（Qt-free header）               （plain struct）            │  sicnu_capability_state_graph        │
                                                            │  (STATIC, C++20, jsoncpp, 无Qt)      │
data/capability_state_graph/ ──load──▶ AuthoredTransitions ─│  schema · loader · validator         │
transitions.json (v1, 手写)                                 │  state matcher · queries · explain   │
                                                            └──────────┬───────────────────────────┘
                                              capability_state:*       │ read-only Json::Value
                                              SpatialTool 族（薄适配）◀──┘ → MCP / AgentToolCatalog 自动暴露
```

- **单一事实源纪律**：图不持有任何独立于 Registry/descriptor 或 authored JSON 的能力事实。operator 节点由 descriptor 投影；状态/转换语义由 authored transitions.json 提供（该文件是**新的、独立的** authored 层，与 capability_knowledge（手写）、capability sidecar（生成）三足鼎立但互不重复：本层只写"状态转换语义"这一个维度）。
- **无 Qt**：核心库 C++20 + jsoncpp，比照 `sicnu_contracts` 模式；descriptor 投影为 header-only 内联（`algorithm_descriptor.h` 本身 Qt-free）。

## 4. Public API / data schema（namespace `sicnu::capability_state`）

### 4.1 C++ schema（全部 versioned：`kSchemaVersion = 1`，序列化根 `"schema":"exp.capability_state.graph.v1"`）

```cpp
// state_predicate.h —— 原子谓词 + 合取
struct StatePredicate { Fact fact; Matcher matcher; std::vector<std::string> values; };
//   Fact（封闭词表 v1）：radiometric_state | masked | grid_aligned | modality
//                     | sar_calibration_domain | crs_projected
//   Matcher：AnyOf（observed ∈ values）| IsTrue | IsFalse
//   radiometric_state 值域 = ADR 0114 五态长规范形；loader 归一别名 dn/toa/sr/bt
using PredicateSet = std::vector<StatePredicate>;   // 语义=合取；canonical 排序后序列化
//   规范 id：state:radiometric_state=toa_reflectance+masked=true （排序、去重、确定）

// capability_node.h
enum class NodeKind { DataState, Operator };
struct CapabilityNode { std::string id; NodeKind kind;
                        std::string operatorId;   // Operator 节点：registry id
                        PredicateSet state;       // DataState 节点：谓词合取
                        std::string label, description; };

// state_transition.h —— authored 转换元数据（prompt 要求的全部边字段）
struct EvidenceHook { std::string metadataKey; std::string expectedValue; }; // verifier 钩子
struct ResourceHints { std::string costClass; bool largeRasterSafe; bool supportsCancellation; };
struct StateTransition {
    std::string id, operatorId, why;
    PredicateSet requires;          // required state（部分合取，缺省=不约束）
    PredicateSet produces;          // produced state（对所列 fact 做"替换"语义）
    std::vector<std::string> assumptions;      // assumptions
    std::string informationLoss;               // information loss
    ResourceHints resource;                    // compute/resource hints
    std::vector<std::string> caveats;          // safety/scientific caveats
    std::vector<EvidenceHook> evidence;        // evidence/verifier hooks
    std::map<std::string,std::string> whenParams; // 可选参数条件（同一算子多边）
};

// capability_edge.h —— 展开后的图边（结构层）
struct CapabilityEdge { std::string transitionId, fromNodeId, toNodeId;
                        enum class Origin { Authored, Derived } origin; };
```

### 4.2 状态语义（合并规则，v1 固定）
`to_state = from_state 以 produces 替换`：复制 from 节点谓词集，用 produces 中同 fact 谓词**替换**、新 fact **追加**。requires 是部分合取（只约束它列出的 fact），使一个转换能从多个状态出发（如 apply_mask 不关心辐射状态）。状态节点全集 = 所有 requires/produces 的规范合取闭包。

### 4.3 查询 API（graph_queries.h，全部返回 bounded `Json::Value` + typed error，确定性排序）
```cpp
capabilitiesAccepting(graph, observed)        // 哪些能力接受当前状态（含逐谓词满足明细）
producersOf(graph, targetPredicate)           // 哪些转换能产出满足目标的状态
explainUnavailable(graph, operatorId, observed) // typed: unknown_operator|no_transition|unsatisfied{fact,expected,observed}
                                              //   + preparations[]（能产出缺失事实的转换）
shortestPath(graph, observed, target)         // BFS 结构候选；响应含 "structural_only": true； hops 上界 + 确定性平局（transition id 字典序）
```

### 4.4 解释投影（graph_explain.h）
- `explainPath(path)` → 分步 zh-CN 模板（why/informationLoss/caveats 编排），教学语态；
- `explainWhyFirst(graph, aOpId, bOpId)` → "为什么要先做 A 再做 B"：给出 A 的哪条 produced fact 是 B 的哪个 required fact 的前提；不存在前提时显式返回 `no_dependency`（typed，不编造）。

### 4.5 Agent 工具（`src/agent/spatial_tools/capability_state_tools.{h,cpp}`，薄适配）
- `capability_state:query`（abilities accepting / unavailable explain / producers）
- `capability_state:path`（structural candidates）
- `capability_state:explain`（why-first / path 讲解）
- inputSchema/outputSchema 走 SpatialTool 契约；在 `spatial_tool.cpp` 注册（一行）。
- 观测态输入 = 显式 facts map（Agent 从数据 metadata 取；v1 不自动内省数据集，接口留好）。

### 4.6 JSON schema（authored，`data/capability_state_graph/transitions.json`）
`{"schema_version":1, "transitions":[...]}`；loader：stackLimit=128 安全读 + 版本硬拒绝 + 未知键/fact/别名错误 typed 报告。

## 5. Migration / compatibility
- 纯新增：新目录 `src/capability_state_graph/`、新数据目录、新测试、root CMakeLists 一行、`spatial_tool.cpp` 一行注册。零修改既有行为。
- 未来：observed-state provider（数据集元数据→ObservedState）在 docs/integration.md 定义 DTO 接线点，本 track 用 fake 完成 TDD。

## 6. Observability
- 所有查询响应携带 `schema`、`generated_from`（registry snapshot 计数 + authored transitions 计数）、确定性排序；loader/validator findings 结构化（code + subject + detail），错误码 typed machine-readable（`CAPSTATE_*` 前缀）。

## 7. Security / trust boundary
- 只读；无网络；JSON 解析 stackLimit 硬化（不重蹈 #1154/#1155）；authored 数据离线随仓库分发；不执行任何算子。

## 8. Performance budget
- 图规模 ≤ 500 节点 / ≤ 2000 边（当前 157 算子 × 多转换，远小于上界）；所有查询 O(V+E) 以内、BFS 上界 hops=16；无动态分配热点；加载一次、查询只读。测试断言查询耗时上界不必要（数据量小），但 API 无无限循环路径（validator 先拒环？不——环允许存在但 BFS 有 visited 集合 + hop 上界）。

## 9. Test strategy
- Catch2，`tests/test_capability_state_graph.cpp`（+必要时第二个文件），轻量链接（Catch2 + jsoncpp + sicnu_capability_state_graph），`ctest -R capability_state_graph -j1`。
- 每 slice：RED→GREEN→REFACTOR；覆盖 happy/invalid/boundary/serialization/deterministic replay/compat。
- G slice：golden path（DN→NDVI 教学场景端到端）+ 跨 slice 不变量（全图 validator zero-error on committed data）。

## 10. Work packages（→ slices.md）
A schema+loader+validator → B registry projection → C matcher → D authored metadata → E queries → F explain → agent tool → G golden/consistency。

## 11. Rollback / kill-switch
- 纯新增模块：revert 单 PR 即完全回退；运行时零行为变化（不注册时零开销）；无 feature flag 需求。

## 12. Definition of Done
prompt 公共 DoD + track DoD 全项；外加：
- [ ] committed transitions.json 通过 validator 零 error（一致性测试守门）
- [ ] DN→NDVI golden 教学场景端到端（explain + path 双 API）
- [ ] `capability_state:*` 三工具经 SpatialToolRegistry 可查（machine-readable）
- [ ] 解释输出 deterministic（两次构建逐字节一致）
- [ ] docs/integration.md + ADR 0172 就位
