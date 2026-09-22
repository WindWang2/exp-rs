# PLAN — RS14-09 Scientific Task Planner (Goal → Scientific Plan)

自我 review 记录见文末 §13。设计分歧决策随文给出（依据：现有架构、测试、最小事实源、可维护性、科学正确性）。

## 1. Problem statement

平台已有 158 个 `rs:` 算子、能力知识层、科学契约层和执行引擎，但**没有一层把"用户想做什么科学"变成"应该按什么顺序做什么"**：意图词汇和 preflight 能回答"缺什么事实"，workflow compiler 能把 *已起草的 IR* 编译成执行计划，但从 goal + 数据状态 + 约束出发、可解释、可比较多个方案、能说"数据不够/需要人决策"的**规划决策层**不存在。学生面对按钮不知道过程；Agent 面对工具目录不知道编排。本 track 交付这个缺失的层 — 只规划，不执行。

## 2. User stories

**本科生实验视角（teaching mode）**
- T1 作为学生，我给出目标（"监测两期影像间的水体变化"）和我的数据清单，得到一份**分步科学计划**：每步为什么存在、前置条件是什么、期望数据状态如何迁移、如何验证结果 — 而不是一个按钮。
- T2 作为学生，autonomy=guided 时计划中标注"由你决定"的参数以占位出现（hidden-answer 版），我不被替做实验；教师/自测可用 explanation 版看逐层理由与思考题。
- T3 作为学生，数据不足时得到 **typed 的 insufficient-data 问题**（缺第 2 期、缺定标状态…），而不是被静默降级的假计划。

**AI Agent 视角（agent mode）**
- A1 作为 Agent，我消费 machine-readable `ScientificPlan` JSON（versioned、指纹、typed verdict/openQuestions），获得候选方案（含 why/whyNot）与代价/风险注记，再决定是否请求执行。
- A2 作为 Agent，我把计划投影成 workflow-neutral IR（`workflow_ir` 1.0 形状），交未来 adapter 进入既有 lowering 链 — 本 track 不执行。
- A3 作为 Agent，我提交外部（LLM）提议计划时，确定性校验器拒绝不合法提案并给出 typed 原因（未知算子、违反前置条件、越界步数…），绝无静默 fallback。

## 3. Architecture

新增纯 C++20 模块 `src/planner`（静态库 `sicnu_planner`，依赖 jsoncpp + `Sicnu::Contracts`，**无 Qt/QGIS/agent/operators 依赖**），命名空间 `sicnu::planner`。单一方向依赖：planner → contracts（词汇与 per-operator 科学契约）；一切能力面/资产面通过注入 interface。

```
src/planner/
  sha256_util.{h,cpp}            最小 SHA-256（FIPS 向量测试钉死；指纹用）
  planner_vocab.h                闭合词汇表（goal kinds, step roles, verdicts,
                                 question kinds, risk kinds, cost classes…）
  scientific_goal.{h,cpp}        ScientificGoal + JSON（envelope scientific_goal/1.0）
  planning_context.{h,cpp}       PlanningContext + PlannerAssetFacts + JSON
                                 （envelope planning_context/1.0）
  provider_interfaces.h          CapabilityProvider / DatasetFactsProvider 接口 + DTO
                                 （PlannerCapability; 资产事实复用 PlannerAssetFacts）
  scientific_plan.{h,cpp}        ScientificPlan（envelope scientific_plan/1.0）+ canonical
                                 JSON + scientificPlanFingerprint + fail-closed reader
  planner_rules.{h,cpp}          确定性规则表：goal kind → 阶段化能力族序列 + 状态门
  planner_core.{h,cpp}           planScientificWork(...) — 确定性基线：DAG 组合、
                                 状态迁移门、verifier targets、open questions、verdict
  planner_constraints.{h,cpp}    约束/资源/风险评估：budget 检查、风险注记（stochastic、
                                 超预算、数据缺口）、cost 聚合
  planner_proposal.{h,cpp}       ProposalProvider 接口 + 确定性提案校验器
                                 （typed 拒绝，无静默 fallback）
  plan_ir_projection.{h,cpp}     ScientificPlan → workflow_ir 1.0 形状文档（含 contracts
                                 domain → artifact_facts domain 的显式映射，fail-closed）
  plan_teaching.{h,cpp}          teaching 投影：hiddenAnswerView / explanationView，
                                 autonomy policy 门控
```

集成接线点（本 track 不实现，写进 `docs/integration.md`）：harness `CapabilityKnowledge`→`CapabilityProvider` adapter；`DatasetUnderstanding`/`AssetState`→`DatasetFactsProvider` adapter；IR 投影→`readWorkflowIr`；`MissionContext`→`PlanningContext` 投影；teaching→LabSpec/copilot 未来消费。

### 决策记录（摘要，详单见 DECISIONS.md）

- **D1 模块位置/依赖**：`src/planner` 纯库，链接 `sicnu_contracts` 复用 `kNumericDomains` + `findScientificContract`（input/outputDomain = state transitions 的单一事实源）。备选"不依赖 contracts 自带词汇"被否：那是第二真相源。
- **D2 与 harness 边界**：不链接 sicnu_agent（Qt SHARED）。与 WorkflowIr/AgentPlan 的关系 = **上游决策层，投影 conform**，不平行、不遮蔽名字。
- **D3 状态轴词汇**：planner 内部状态轴 = contracts `kNumericDomains`（链接复用）；IR 投影时经显式映射表转为 `artifact_facts` domain（两词汇在仓库本就并存，映射 fail-closed + golden 钉死）。
- **D4 step role 词汇**：镜像 `MissionStage` 五阶段（import/preprocess/analyze/verify/publish）+ `record`（科学记录，映射 experiment 語义）→ 未来 mission 投影零翻译。
- **D5 规则表放代码不放 JSON**：确定性、可 review、无数据文件漂移面；表带 `kPlannerRulesRevision` 暴露在 plan 文档（observability）。规则只表达"目标种类需要哪些能力族、按什么阶段序"，**具体算子选择经 CapabilityProvider 查询**，不硬编码算子链。
- **D6 指纹**：SHA-256/16 hex over canonical compact JSON（仓库既有惯例）；自带 sha256_util，FIPS 向量 + 与已知向量互检。
- **D7 teaching 语义**：`hiddenAnswerView` 把 `studentDecision` 步参数替换为占位 + 保留结构；`explanationView` 附 per-step rationale/thinking questions；两视图均为 plan 的纯函数；autonomy=guided 时**禁止**填充决策参数、禁止标记可完成 — 与 lab copilot 约束（ADR 0155）同向。
- **D8 LLM 提案**：提案以 `scientific_plan` 1.0 JSON 提入；校验器做结构/schema/词汇/算子存在性（CapabilityProvider）/前置条件满足性/预算检查；失败 → `ProposalRejection{code, reasons[]}`（typed，提案不入候选）。

## 4. Public API / data schema

### 4.1 文档 schema（全部 versioned, fail-closed reader, canonical JSON, 指纹）

- `ScientificGoal`（`kind:"scientific_goal"`, `schema_version:"1.0"`）：
  `goalId, kind∈{measurement,classification,change,monitoring,detection,temporal_analysis,map_product}, subject, quantity?, temporalScope?{start,end,minScenes,maxGapDays}, acceptanceCriteria[] {criterionId, check, target?}`。
- `PlanningContext`（`kind:"planning_context"`, `schema_version:"1.0"`）：
  `assets[] PlannerAssetFacts{ref, kind∈{raster,vector,collection,model}, modality, numericDomain, crs?, resolutionM?, bandRoles[], dates[]?, qualityMaskAvailable, state∈{ready,missing,offline,...镜像 AssetState}, calibrationState?}`,
  `constraints{maxSteps?, forbiddenOperators[], requiredDeterminism?, allowedFamilies[]?}`,
  `quality{requireUncertainty?, requireValidationSplit?, minAccuracy?}`,
  `resourceBudget{maxSteps?, maxEstimatedRamMb?, maxCostClass?}`,
  `mode{kind∈{standard,teaching,agent}, autonomy∈{full,guided,minimal}, studentDecisionDefault}`。
- `ScientificPlan`（`kind:"scientific_plan"`, `schema_version:"1.0"`）：
  `planId, goalId, goalKind, rulesRevision, mode, verdict∈{feasible, feasible_with_gaps, infeasible}, verdictReasons[],
   steps[] PlannerStep{stepId, role∈{import,preprocess,analyze,verify,publish,record}, operatorId?, family,
     inputs[] {fromStepId|assetRef, as}, params?, preconditions[] {kind∈{asset_state,numeric_domain,min_scene_count,grid,band_role}, ...typed detail},
     expectedTransitions[] {assetRef, fromDomain, toDomain}, verifierTargets[] {check, target?},
     costClass, estimatedRamMb?, riskNotes[], studentDecision?}`,
  `alternatives[] {alternativeId, summary, whyChosen?, whyNot?, plan?}`,
  `openQuestions[] {questionId, kind∈{insufficient_data,ambiguity,decision_required}, blocking, detail, suggestion?}`,
  `cost{aggregateCostClass, totalEstimatedRamMb?}, risks[] {kind, detail, stepId?}`。

C++ API（`sicnu::planner`，全部纯函数/纯值对象）：
```cpp
PlanningResult planScientificWork(const ScientificGoal&, const PlanningContext&,
                                  const PlannerProviders&);   // 确定性基线 (Slice B/C/D)
ProposalOutcome validateProposal(const Json::Value& proposalDoc,
                                 const ScientificGoal&, const PlanningContext&,
                                 const PlannerProviders&);    // Slice E
Json::Value projectPlanToIr(const ScientificPlan&, std::string* error); // Slice F
TeachingViews teachingViews(const ScientificPlan&, const ModePolicy&);   // Slice D/G
bool scientificGoalFromJson / planningContextFromJson / scientificPlanFromJson  // fail-closed
Json::Value …ToJson  // canonical, 成员排序
std::string scientificPlanFingerprint(const ScientificPlan&)        // SHA-256/16
```

## 5. Migration / compatibility

纯新增。不改任何现有行为；共享文件 delta：root CMakeLists +1 行、tests/CMakeLists +helper+测试块、.gitignore +3 行、CHANGELOG +1 条。所有 reader fail-closed 且只接受 `1.0`；未来演进走 additive-optional + 显式版本集合（仓库惯例）。

## 6. Observability

- 每份 plan 携带 `rulesRevision`、`verdictReasons[]`、`risks[]`、`openQuestions[]` — 计划自身可解释。
- canonical JSON 确定性 → 同输入重放字节一致（测试钉死）。
- 指纹（SHA-256/16）供日志/ledger 引用计划身份。

## 7. Security / trust boundary

- planner 不做 I/O、不打开数据、不执行；输入文档由调用方注入（信任边界在调用方）。
- JSON reader 限深限幅遵循 jsoncpp stackLimit 惯例（对齐 #1154/#1155 的纪律：planner reader 解析的文档来自本地调用方，但仍以 `CharReaderBuilder` + stackLimit 构造，不引入新的无界解析面 — 观察项 O-1 的最小兼容纪律）。
- 提案校验 fail-closed：未知算子/词汇/超界一律 typed 拒绝。

## 8. Performance budget

- 纯内存计算，无 I/O：规划一次 = O(steps × rules × provider queries)；规则表 ≤ 8 goal kinds × ≤ 6 阶段；provider 调用每步 ≤ 2。目标：10k 资产 context 下 < 50 ms（不作为门，作为上界注记）；`IrLimits`-风格自限：plan ≤ 64 steps、≤ 8 inputs/step、id ≤ 64 chars、text ≤ 512 chars（超界 = typed 拒绝，不截断）。

## 9. Test strategy

- 框架 Catch2（仓库惯例），narrow 目标 `sicnu_add_planner_test`（无 Qt/QGIS，秒级）。
- 每 slice red-first；每行为至少：happy path / invalid input / boundary / serialization round-trip / deterministic replay。
- 教学与 agent 语义一致性：同一 plan 在两 mode 下投影一致性 + guided 不可替做的负向测试。
- Golden 场景（Slice G）：`data/planner/golden_scenarios/*.json` 自包含（goal+context+capability fixture → 期望 plan canonical JSON），`ctest -R "test_scientific_planner_golden::"` 离线重放。
- 资源纪律：仅 `ctest -R "test_scientific_planner" -j1` narrow 运行；单 target 增量编译。

## 10. 分阶段 work packages

| WP | 内容 | Slice |
|---|---|---|
| WP0 | recon/plan/slices 文档 + 骨架 CMake + vocab + sha256 | A0 |
| WP1 | goal/context/plan schema + JSON + 指纹 | A |
| WP2 | 规则表 + 确定性基线 planner（DAG/状态门/verifier targets/questions/verdict） | B |
| WP3 | 约束/资源/风险 | C |
| WP4 | alternatives + insufficient-data 深化 | D |
| WP5 | proposal provider + 校验器（fake provider） | E |
| WP6 | IR 投影 + domain 映射 | F |
| WP7 | golden 场景 + teaching/agent 一致性 | G |
| WP8 | docs/integration.md + CHANGELOG + review/修复/regression | R |

## 11. Rollback / kill-switch

模块自包含：revert 分支即完全移除；共享文件改动均为 append-only 独立 commit（便于 union 冲突时逐块取舍）。无运行时行为依赖 — kill-switch = 不链接（无任何现有 target 引用 `sicnu_planner`）。

## 12. Definition of Done

公共 DoD（计划/代码/测试/文档一致；端到端可走通；targeted tests 绿；无新增 warning/leak/deadlock；无未处理 P0/P1/P2；动态去重完成；两轮 review）+ track 专属：
1. 教学端到端场景 ≥1（水体变化或 NDVI 监测）：学生得到 hidden-answer + explanation 双视图，guided 模式参数留白。
2. Agent 机器可读接口 ≥1：versioned plan JSON + IR 投影文档（不执行）。
3. 单一事实源：科学语义仅链接 `sicnu_contracts`；能力面仅经 interface；无数据复制。
4. unsafe/unknown/unsupported 全 typed（verdict/openQuestions/rejection codes），无静默 fallback。
5. 离线：全部测试与 golden 不依赖网络。
6. 资源上界：plan 上限表 + typed 拒绝；测试秒级。
7. PR 前动态去重记录在案。

## 13. 自我 review（计划完成后过一遍）

- 与 harness 边界：✅ 不撞名（`workflow_planner/AgentPlan/WorkflowIr` 均未复用）、不平行真相源（§9 layering 图）。
- 教学约束：✅ D7 与 ADR 0155 同向；负向测试在 slices G。
- open issues：✅ §3 矩阵；O-1（jsoncpp stackLimit 纪律）以最小兼容方式处理，不修 #1154/#1155 本体。
- schema versioned：✅ 三个 envelope 均 1.0 + fail-closed。
- 性能：✅ §8 上界 + 自限表。
- 资源：✅ 纯模块小测，避免 QGIS 链接 — 与 recon §4.7 惯例一致。
- 遗留开放点：`data/planner/` 目录为新增（golden 场景），命名遵循仓库 `data/<域>/` 惯例；如 review 认为应归 `data/agent/` 再调整（本 track 独占目录，无冲突）。
