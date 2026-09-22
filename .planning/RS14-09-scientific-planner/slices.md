# SLICES — RS14-09 Scientific Task Planner

粒度纪律：每 slice = 明确 red test → minimal implementation → refactor → green → narrow test → commit → progress.md 更新。禁止先写大堆实现最后补测试。

| Slice | 交付 | RED 先行测试（新测试文件/用例） | GREEN 最小实现 | commit 粒度 |
|---|---|---|---|---|
| **A0** | CMake 骨架 + `planner_vocab.h` + `sha256_util` | `test_scientific_planner_vocab.cpp`：词汇闭合性（isKnown*/表有序无重复）；SHA-256 FIPS 向量（""/"abc"/56-byte 双块） | 空库 `sicnu_planner` + root `add_subdirectory` + `sicnu_add_planner_test` helper + `.gitignore` 3 行 | `feat(planner): skeleton, closed vocabularies, sha256 (slice A0)` |
| **A** | `ScientificGoal` / `PlanningContext`(+asset facts+mode) / `ScientificPlan` 值对象 + canonical JSON + fail-closed reader + 指纹 | `test_scientific_planner_schema.cpp`：round-trip；未知 kind/schema_version 拒绝（typed error）；指纹稳定性（同内容同指纹、改任意字段变指纹）；canonical 成员排序（重排输入键 → 同输出字节）；上界（>64 steps 拒绝） | 三个 `{h,cpp}` + envelope 常量 + reader/toJson + fingerprint | `feat(planner): versioned goal/context/plan schema (slice A)` |
| **B** | 规则表 + `planScientificWork` 确定性基线：goal kind → 阶段化族序列；CapabilityProvider 选算子；状态迁移门（contracts inputDomain→outputDomain）；verifier targets；verdict + openQuestions（基本） | `test_scientific_planner_core.cpp`：change 场景产出 import→preprocess(calibrate 门)→analyze→verify→publish DAG 且顺序合法；asset domain=dn + analyze 需 reflectance → 插入校准步并记录 dn→reflectance 迁移；未知 goal kind → infeasible+reasons；缺第 2 期资产 → openQuestion(insufficient_data, blocking)；同输入重放 plan 字节一致 | `planner_rules.{h,cpp}` + `planner_core.{h,cpp}` + provider_interfaces.h（fake provider 在测试内） | `feat(planner): deterministic goal→plan baseline (slice B)` |
| **C** | 约束/资源/风险：budget 检查、forbidden/requiredDeterminism、cost 聚合、风险注记（stochastic=seedPolicy、超预算、large raster） | `test_scientific_planner_constraints.cpp`：maxCostClass 违约 → verdict 降级/typed risk；forbidden 算子被绕开或产生 typed question；stochastic 算子 → risk 注记（来自 contracts seedPolicy）；超 maxSteps → typed 拒绝 | `planner_constraints.{h,cpp}` 接入 core | `feat(planner): constraint/resource/risk evaluation (slice C)` |
| **D** | alternatives（operator 候选并行方案 + why/whyNot）+ insufficient-data 深化（多问题排序、blocking 语义）+ teaching/agent mode 投影（hiddenAnswerView/explanationView + autonomy 门） | `test_scientific_planner_alternatives.cpp` + `test_scientific_planner_teaching.cpp`：两日期可用性不同 → 主/备方案 why 互换；teaching guided → studentDecision 步参数为占位且无答案泄漏；standard 模式不产生占位；两视图对同一 plan 纯函数一致 | alternatives 生成 + `plan_teaching.{h,cpp}` | `feat(planner): alternatives, insufficient-data, teaching views (slice D)` |
| **E** | `ProposalProvider` 接口 + 确定性提案校验器 | `test_scientific_planner_proposal.cpp`：fake provider 合法提案 → 通过并成为候选（附 proposal 来源注记）；未知算子/未知 domain/前置不满足/超步数/未知 schema → ProposalRejection typed codes + reasons，且不入候选、无静默 fallback | `planner_proposal.{h,cpp}` | `feat(planner): proposal validation seam with fake provider (slice E)` |
| **F** | `projectPlanToIr`：ScientificPlan → `workflow_ir` 1.0 形状文档（kind/schema_version/inputs/nodes{operator,params,inputs,outputs,verification,resource_estimate_mb,determinism,semantic_output,source}/outputs/expectations）+ contracts→artifact_facts domain 映射（fail-closed） | `test_scientific_planner_ir.cpp`：投影文档字段形状与 `workflow_ir.h` 结构一致（golden 对照手抄样例）；domain 映射表全覆盖（每 contracts 域有映射或显式 unsupported → typed error）；无 state 依据步拒绝投影；plan 无 steps → typed 拒绝 | `plan_ir_projection.{h,cpp}` | `feat(planner): workflow-neutral IR projection (slice F)` |
| **G** | golden 场景（`data/planner/golden_scenarios/`，自包含 fixture）+ 端到端一致性（teaching+agent 同源） | `test_scientific_planner_golden.cpp`：≥4 场景离线重放字节比对（ndvi measurement；two-date water change 含替代方案；temporal monitoring insufficient-data；classification teaching guided）；场景文件自身 schema 校验 | golden JSON + 读入重放测试 | `test(planner): golden planning scenarios (slice G)` |
| **R** | `docs/integration.md`、CHANGELOG、review 修复、targeted regression、PR | review findings 驱动 | 文档 + 修复 commits | `docs(planner): integration map + review remediation (slice R)` |

## 行为覆盖矩阵（每核心行为至少）

- happy path：B（change DAG）/ D（alternatives）/ E（合法提案）/ F（投影）/ G（golden）
- invalid/unsafe input：A（schema 拒绝）/ C（budget/forbidden）/ E（typed 拒绝）/ F（fail-closed 映射）
- boundary：A（64 steps 上界）/ B（0 资产）/ E（空提案）
- persistence/serialization：A（round-trip+canonical）/ G（golden 字节）
- deterministic replay：B/G（重放字节一致）
- compatibility：envelope 只收 1.0，additive 演进纪律写入 integration.md
- cancellation/resource budget：C（上界拒绝，无静默截断）
- teaching/agent 语义一致性：D/G（同 plan 双投影一致 + guided 负向）

## 测试命令（资源纪律）

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=1 CTEST_PARALLEL_LEVEL=1
cmake --build <build> --target test_scientific_planner_core -j1   # 按 target 增量
ctest -R "test_scientific_planner" -j1                             # narrow
```
