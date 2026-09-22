# RECON — RS14-09 Scientific Task Planner (Goal → Scientific Plan)

Track: `RS14-09-scientific-planner` · Branch: `agent/rs14-scientific-task-planner` · Worktree: `../exp-rs-wt-rs14-scientific-planner`

## 1. origin/master 刷新

- Prompt 基线 HEAD：`4f6632e1f6bb41f90729800d0c7bf569ff34edb3`（PR #1145 合并后）。
- `git fetch origin --prune` 后 `origin/master` = `4f6632e1f` — **与 prompt 基线一致，master 未前移**。
- 工作分支 `agent/rs14-scientific-task-planner` 已从 `origin/master` 创建，独立 worktree，不与任何其他 track 复用 build 目录。

## 2. Open PRs（启动时）

- `gh pr list --state open` → **空**。无需要去重的并行 PR。PR 创建前将再次动态检查。

## 3. Open issues 逐条 dedupe（避让矩阵）

本 track 只做"新能力层：goal → 可解释科学计划（不执行）"。逐条核对 prompt 避让清单（#1146–#1187 全部 44 个 open issues）：

| Issue 区域 | 与本 track 关系 | 处置 |
|---|---|---|
| #1146/#1147/#1164/#1165 SAR 科学正确性/domain/state | 无交集（不碰 SAR 算子实现） | 避让 |
| #1148/#1149/#1168/#1169/#1170 Mission 持久化/状态映射/规模 | 只读消费 `MissionStage` 阶段词汇概念，不改 mission 存储 | 避让 |
| #1152/#1158 Workflow/D17 cancellation | 无交集（planner 不执行） | 避让 |
| #1153 ImportCenter 死锁 | 无交集 | 避让 |
| #1154/#1155 jsoncpp depth bomb | 新代码全部用 `Json::Reader` 前先看仓库惯例；本 track 不引入新的远程 JSON 解析面（输入文档由调用方给定，reader 带 stackLimit 惯例将在实现时遵循 — 若发现无法隔离，记 blocked，不修） | 避让（观察项 O-1） |
| #1150/#1183 光谱 NoData/背景统计 | 无交集 | 避让 |
| #1151/#1187 capability mirror / contract projection 红灯 | **关键避让**：本 track 不修改 `data/agent/capabilities/*`、`data/processing/algorithm_meta/**`、`data/contracts/*snap*.json`，不新增 capability entries，因此不会加重或触发 drift 门 | 避让（硬边界） |
| #1156/#1157/#1181 插件生命周期/工具目录缓存 | 无交集 | 避让 |
| #1159/#1182 TaskCenter cancel/retry | 无交集（不执行） | 避让 |
| #1160 VRAM ledger | 无交集 | 避让 |
| #1161/#1171/#1172/#1173/#1184 Catalog/Dataset/Experiment 现有缺陷 | planner 只通过 interface 查询资产事实（fake provider TDD），不改 store | 避让 |
| #1162/#1163 geospatial mirror/cache | 无交集 | 避让 |
| #1174/#1175/#1178 原子发布/sidecar/Windows | 无交集（不发布文件） | 避让 |
| #1176 WBF/CatalogRecordStore 性能 | 无交集 | 避让 |
| #1177 performance observatory | 无交集 | 避让 |
| #1179 测试 oracle potency | 反向相关：本 track 的新测试必须真正能在回归时失败（red-first 证据） | 遵守其精神，不修旧测试 |
| #1180 georeferencer UAF | 无交集 | 避让 |
| #1185 CLI concurrency hang | 无交集（不改 CLI） | 避让 |
| #1186 P3 review batch | 无交集 | 避让 |

**结论：无一个 open issue 恰好是本方向内容；无阻塞项。**

## 4. 代码现状审计（关键事实，含文件:行号）

### 4.1 名称空间是自由的

全仓库（src/ tests/ docs/ .planning/ data/ pi/）grep `ScientificGoal|ScientificPlan|PlanningContext` → **0 命中**。三个核心类型名可安全使用。

### 4.2 已有的"计划"词汇 — 必须避让、可以下沉

- `src/agent/harness/workflow_planner.{h,cpp}`：**staged compiler**（intent→grounding→candidates→IR→analysis→repair→lower→plan，ADR 0149/0163）。输入是 *agent 已经起草的 IR 文档 + 事实*，输出 `CompiledWorkflow`（含 alternatives/missingFacts/refusals/plan/workflowJson）。**它是 plan-to-workflow 编译器，不是 goal-to-plan 规划器** — 没有目标分解、没有从 goal+数据状态出发的规划语义。"planner" 一词在 harness 已被占用。
- `src/agent/harness/agent_plan.h`：`AgentPlan` v2（envelope `kind:"execution_plan"`, schema "2.0"），`compilePlanToWorkflowJson` 是唯一 plan→engine JSON 桥。
- `src/agent/harness/workflow_ir.h`：`WorkflowIr` 1.0（`kind:"workflow_ir"`），typed DAG，fail-closed reader，`irLimits()`（≤64 nodes），fact 词汇 `artifact_facts`（domain 词汇：`surface_reflectance|toa|dn|db|linear_power|index|categorical|masked|unknown`），fact provenance `observed|declared|derived|assumed|unknown`，`IrRefusal.decisionRequired`。
- 其他 Plan*：`StepPlan`（workflow_run 执行态）、`WorkflowPlanOptimizer`（引擎 DNE/CSE）、`TileMemoryPlan`、`MosaicPlan`、`SeriesPlanner`（cartography）、`FabricPlan`。全部执行侧/领域内，无 goal 语义。
- **结论**：新类型必须是 *上游的科学决策层*（goal 分解、备选、未决问题、代价/风险），向下游 `WorkflowIr/AgentPlan` **投影**，不得复用/遮蔽这些名字，不得成为第三套 IR 真相源。

### 4.3 Capability Graph — 通过 interface 依赖的正确切口

- `src/agent/harness/capability_knowledge.h`（:64 class）：加载 `data/agent/capabilities/*.json`（15 个领域文件），查询 API `entryForOperator/operatorsForIntent/familyDefault/entryIdsForSurface`，条目含 modality/band_roles/radiometric(acceptable+warn 状态)/temporal(min_scenes,max_gap_days)/resource(cost_class,large_raster_safe)/verification(expected_kind+checks)/limitations。**全部 const，只读。**
- `src/agent/harness/capability_catalog.h`（:85）：per-operator sidecar v2，`family`（11 canonical families）、`determinism`、`prerequisites`、`failure_modes`、`applicability`、`teaching_use`。
- `src/agent/harness/capability_relations.h`（ADR 0154）：`chains` 边（带 `when` 事实门，如 radiometric_state ∈ {dn,toa}）、`exclusive` 边、`composeChain(targetId, facts)`、`requires_shared_grid`+`grid_fixer:"rs:align"`；DAG 校验。
- `src/agent/harness/capability_graph.h`：`resolveGoalIntent/evaluateFeasibility/capabilityCandidates/missingFactsForIntent/preparationForWhyNot/solutionPathsForIntent`。
- **这一层编译进 `sicnu_agent` SHARED（Qt）**。纯 planner 模块不能链接 → 定义最小 `CapabilityProvider` interface + DTO，测试用 in-memory fake，`docs/integration.md` 写明未来 adapter 接线点（CapabilityKnowledge/CapabilityCatalog → Provider）。

### 4.4 契约层 — 可链接的纯事实源（关键发现）

- `src/contracts/` = **纯 C++20 + jsoncpp、无 Qt 的 STATIC 库 `sicnu_contracts`**（src/contracts/CMakeLists.txt:7-38）。
- `scientific_contract.h:42` `ScientificContract`：per-operator `inputDomain/outputDomain`（词汇 `kNumericDomains`，scientific_contract.cpp:22，21 个域：dn/reflectance/radiance/temperature/amplitude/sigma0/gamma0/beta0/phase/displacement/db/index/probability/mask/classes/features/count/vector/table/none/any）、`noDataPolicy`、`timeAlignment`、`seedPolicy`（风险：stochastic）、`atomicPublication`、`refusalCodes`。`findScientificContract(id)`（:116）— **注册表数据是库内自包含静态数据（scientific_contract.cpp），不需要 operators lib 参与**。
- 这给了 planner **state-transition 词汇与 per-operator 输入/输出域的单一事实源**，通过合法链接复用（不是复制）。planner 的"expected state transitions"= 契约 inputDomain→outputDomain 对。
- 注意：contracts `kNumericDomains` 与 harness `artifact_facts` domain 词汇**不同名**（reflectance vs surface_reflectance/toa…）— 仓库现状即双词汇并存。Slice F 投影时需要一张小的、fail-closed 的 domain 映射表（在 integration.md 记录等价关系），不做隐式改名。

### 4.5 数据/资产状态词汇（planner 经 interface 查询）

- `src/data/asset_types.h:75` `AssetState`（Registered/Resolving/Ready/Missing/UnavailableSource/Offline/AuthenticationRequired/Error/Stale）、`AssetCapability` flags。
- `src/agent/contracts/spatial_contracts.h` `DatasetUnderstanding`（schema 1.0：size/CRS/band roles/nodata/radiometric state）。
- `src/core/radiometric_state.h`：`RadiometricUnit` FSM `canTransition`（光学域）。位于 sicnu_core（Qt 依赖，不链接；planner 用 contracts 域词汇表达"期望迁移"，integration.md 声明与该 FSM 的语义对应）。
- `src/data/temporal_workspace_types.h:21` `TemporalCollectionRecord`（dates 序列；ADR 0163 明确留下 "planner wiring" 未被认领）。

### 4.6 Mission / Teaching / Agent 现状

- `src/app/workbench/mission_stage.h:54` `MissionStage {Import, Preprocess, Analyze, Verify, Publish}` — **已存在的阶段轴**。planner 的 step role 词汇应与之一致（镜像+drift 注记），使计划可直接被 mission timeline 消费。
- `mission_context.h`：`MissionContext`（AOI/时相/选区）是现成的"规划上下文"执行侧载体；planner 的 `PlanningContext` 是**纯值对象**，未来由 mission context 投影填充（integration.md 接线点）。
- Teaching：LabSpec（`lab_spec.h`）、lab copilot 约束（ADR 0155：不给答案、evidence-based）、grader 2.0。planner teaching 模式 = 生成 hidden-answer 版 + explanation 版投影，参数占位由 autonomy policy 控制；不复制 grader。
- Agent：`AgentToolCatalog`、`harness:plan/compile_workflow` 等工具已存在；本 track 只提供 machine-readable 计划文档 + IR 投影，**不注册新执行工具、不接 MCP**（避免与 harness 工具面重复；未来 adapter 可在 `docs/integration.md` 记录）。

### 4.7 序列化/测试/CMake 惯例

- JSON：jsoncpp（`<json/json.h>`）；envelope = `kind` + `schema_version`，fail-closed reader，支持显式版本集合；canonical 序列化成员排序、byte-stable；指纹 = SHA-256 前 16 hex（`planFingerprint`/`workflowIrFingerprint` 惯例）。**纯库中无现成非 Qt SHA-256**（Qt 用 `QCryptographicHash`，src/data/execution_fingerprint.cpp:113）→ planner 模块自带最小 SHA-256（FIPS 180 测试向量钉死）。
- 测试：Catch2 v3，`tests/sicnu_test_main.cpp` 共享 runner；narrow helper 先例 `sicnu_add_sdk_test`（tests/CMakeLists.txt:125）+ 手工 `add_executable` 块（:1407 `test_scientific_contracts`）。ctest 名带 `TEST_PREFIX "<name>::"`，narrow 选择用 `ctest -R "test_xxx::" -j1`。
- CMake：新模块 = `add_subdirectory(src/planner)` 一行（root CMakeLists.txt ~:793，contracts 之后）+ `src/planner/CMakeLists.txt`（copy contracts 的 jsoncpp resolution block）+ tests helper `sicnu_add_planner_test` + 测试注册。**中央文件 delta 最小（3 处 append-only）**。

## 5. 已有能力 vs 真实缺口

**已有**（复用/投影，不重建）：意图词汇与意图→需求表（`intent_vocabulary.h`、`typedIntentDocument`）；capability 知识/关系/图（只读查询）；per-operator 科学契约（domains/transitions/refusals/seed）；执行侧 plan/IR 文档与 lowering 链；mission 阶段轴；teaching 约束与 grader；facts 词汇与 provenance 纪律。

**真实缺口（本 track 关闭）**：
- **G1** 没有 goal 层类型：无 `ScientificGoal`（kind 词汇 measurement/classification/change/monitoring/detection/temporal_analysis/map_product）、无版本化 goal 文档。
- **G2** 没有规划上下文类型：数据状态/约束/质量要求/资源预算/mode（teaching|agent|standard）无统一值对象。
- **G3** 没有 goal→plan 的确定性规则/图基线：从 goal+资产事实出发组合 ordered DAG（阶段序 + 状态迁移门 + verifier targets）的逻辑不存在（harness 从 agent-authored IR 出发）。
- **G4** 计划文档不存在：preconditions、expected state transitions（契约域对）、verifier targets、alternatives(why/whyNot)、cost/risk 注记、open questions（insufficient_data/ambiguity/decision_required, blocking 标志）无任何载体。
- **G5** LLM proposal seam 不存在：可注入的 proposal provider interface + 确定性校验（invalid → typed refusal，不静默 fallback）缺失。
- **G6** plan→workflow-neutral IR 投影缺失（目标形状 = 现有 `workflow_ir` 1.0 文档，数据级 conform，不链接 agent lib）。
- **G7** teaching 投影（hidden-answer + explanation，autonomy policy 门控）与 agent/teaching 语义一致性测试缺失。
- **G8** golden 规划场景（离线、确定性重放）缺失。

## 6. 不做什么（Non-goals）

1. 不执行任何 workflow/算子；不注册 TaskCenter 任务；不接 MCP 工具面。
2. 不实现 LLM vendor integration（只有 fake provider + 确定性校验器）。
3. 不修任何 open issue（尤其 #1151/#1187 capability/contract drift — 不触碰其数据文件）。
4. 不链接 Qt/QGIS/sicnu_agent；新模块 = 纯 C++20 + jsoncpp + `sicnu_contracts`。
5. 不创建第三套 Registry/provenance/experiment store；不复制 capability knowledge 数据。
6. 不动 `data/agent/**`、`data/processing/algorithm_meta/**`、`data/contracts/**`。
7. 不做 GUI。

## 7. 风险

- **R1 词汇漂移**：planner 镜像 MissionStage/域词汇 → 用 drift 注记 + integration.md 对应表 + fail-closed 校验缓解；不新增静默改名。
- **R2 双 domain 词汇**（contracts vs artifact_facts）→ Slice F 映射表显式、fail-closed、golden 钉死。
- **R3 与 harness 边界模糊** → 命名纪律：新类型全部 `sicnu::planner` 命名空间，`Scientific*` 前缀；文档写明 layering（planner upstream of WorkflowIr/AgentPlan）。
- **R4 编译资源**：20 tracks 并行 → 纯模块小单测（catch2 单 exe，秒级），`ctest -R "test_scientific_planner" -j1`；绝不全量构建验证。
- **R5 scope 膨胀**：规则表只覆盖 goal kind × 主干路径；不追求全算子编排完备性（capability relations `composeChain` 未来可作 provider 增强）。

## 8. 与其他 19 tracks 的接口

- 提供：`src/planner/*` 纯库（`sicnu::planner`）、版本化文档 schema（scientific_goal/planning_context/scientific_plan, v1.0）、`CapabilityProvider/DatasetFactsProvider/ProposalProvider` interfaces、`docs/integration.md` 接线点。
- 消费：`sicnu_contracts`（已存在的纯事实源）。
- 未来接线（本 track 不实现）：harness CapabilityKnowledge→Provider adapter；mission context→PlanningContext 投影；IR 投影→`readWorkflowIr`；teaching→LabSpec/lab_copilot；agent 工具面注册（harness:scientific_plan 之类，留给后续 track）。
- 文件级独占写区：`src/planner/**`、`tests/test_scientific_planner*.cpp`、`.planning/RS14-09-scientific-planner/**`、`docs/integration.md`（新建）、`data/planner/**`（golden 场景，新建目录）。
- 共享文件最小 delta：root `CMakeLists.txt`（+1 add_subdirectory）、`tests/CMakeLists.txt`（+1 helper + 测试注册块）、`.gitignore`（+3 行 append-only）、`CHANGELOG.md`（+1 条目）。

## 9. 与 harness 的 layering 声明（单一事实源承诺）

```
ScientificGoal + PlanningContext          (本 track, 新)
        │  deterministic rule/graph planner (rules + provider facts + contracts)
        ▼
ScientificPlan (v1.0)                      (本 track, 新 — 决策层: DAG/preconditions/
        │  planIrProjection() (Slice F)     state transitions/verifier targets/alternatives/
        ▼                                   open questions/cost/risk)
workflow_ir 1.0 形状文档  ──(future adapter)──▶ readWorkflowIr → AgentPlan v2 → engine JSON
        ▲                                    (harness, 已存在 — 不改)
teaching 投影 (hidden-answer + explanation) (本 track, 纯投影)
```

事实源分工：operator 科学语义（domains/refusals/seed）= `sicnu_contracts`（链接复用）；能力面元数据（cost class/verification checks/family）= `CapabilityProvider` interface（本 track 定义，未来 adapter 接 capability knowledge，数据不复制）；资产状态 = `DatasetFactsProvider` interface（调用方注入，来自 DatasetUnderstanding/AssetState）。
