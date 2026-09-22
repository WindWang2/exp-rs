# Recon — RS14-05 Process-aware Experiment Grader

Track: `RS14-05-process-grader` · Branch: `agent/rs14-process-grader`
Baseline: `origin/master` = `4f6632e1f6bb41f90729800d0c7bf569ff34edb3` (re-fetched at start; master had NOT moved since prompt generation).

## 1. 已有能力（master 上真实存在的）

### 评分现状（最重要的定位事实）
- **D4 artifact grader** `src/agent/output_verifier.h:75-160`：`LabGradeResult`（verdict pass|fail|unverifiable，`score = 100 − Σ failed weights`，blocking 封顶，evidence 数组 + digest）。ADR 0150 明确：那是**产物级**评分，"No partial-credit curves — explainability beats smoothness"、"an evidence-less deduction is a P0 defect"。**过程级 + rubric 版本化 + partial credit + 多维度评分是已声明的缺口，不是重复。**
- 评分规则文档 `sicnu.lab.rules/1`（`data/labs/grading/<lab_id>.rules.json`，ADR 0150 §3）：artifact 断言规则，per-lab 无版本演进。
- `LabGradeEmbedding`（`src/experiment/bridge/lab_report.h:66-89`）：实验侧**故意留空的 typed grade 插槽**（`status recorded|unavailable` + `inlineResult`）。→ 本 track 的 GradeReport 是它的天然未来填充物（本项目不填，写在 integration 文档里）。

### Evidence 来源（全部只读可查）
- `ExperimentStore`（`experiment_store.h`）：只读 getter 齐全（`runById`, `listRunsByCursor`, `metricRecordForRun`, `benchmarkResultsFor`, lineage `outgoingEdges/incomingEdges`，`isReadOnly()`）。
- `MetricRecord` / 公式（`evaluation.h`）：**公式唯一家园**（"All formulas are implemented exactly once here"）。metric 文档用 `metricValueAtPath`（`metric_path.h`，"never zero-filled"）取值。
- Workflow provenance（`workflow_provenance.h`）：每 run 一个 `provenance_<runId>.json`，nodeExec 记录 **state、elapsed、cacheHit**，edges `consumed|produced|reusedFrom`，byte-stable sorted serialization，envelope kind+closed version set。→ 最适合做"状态转移 evidence"。
- Workflow checkpoint（`workflow_checkpoint.h`）：`checkpoint_<runId>.json` + `history/` 归档；`StepPlan`（`workflow_run.h:53-101`）记录 per-step status/digest/times。
- `WorkflowRunState` 枚举（`workflow_run.h:15-26`）+ 严格 transition 校验；`run_bridge.h:38-46` 有 closed vocabulary `Running|Completed|Failed|Canceled|Interrupted`。
- `EvidenceProjector`（`experiment/evidence.h`）：run → kEvidenceSchemaVersion=1 摘要文档，"never computes, estimates or invents evidence"。诚实性契约的先例。
- `RepeatExecutionClassifier`（`repeat_execution.h`）：New|SameExecution|SameIdentity|EquivalentRerun|Deviated + reasons —— reproducibility 维度可直接消费其 verdict 作为 evidence，不重复身份逻辑。
- `LineageGraph`（`lineage.h`）：kinds asset|dataset|…|run|artifact|metric，edgeKinds derived_from|evaluated_on|produced|consumed|…
- Lab 链：ADR 0166 `data/labs/lab-registry.json` 单一权威；`LabSpec`（`src/app/widgets/lab_spec_loader.h`）有 `thinkingQuestions`（学生答案形状的唯一现有雏形，无持久化 student-answer 记录类型）。

### 新叶子模块惯例（RS14 并发 track 与仓库先例）
- 版本化 schema：`sicnu.<name>.v1` / `sicnu.<name>/1` 字符串 id 或整型 `k<Thing>SchemaVersion` + 严格 reader 拒绝外来版本。
- 并发 track #1189 agentbench 与 #1191 verifier 都是 **纯 C++20 + jsoncpp 叶子库、零 Qt、轻量 Catch2 test lane（秒级构建）**；root CMakeLists +1 `add_subdirectory`、tests/CMakeLists 追加块、.gitignore 加 `.planning` markdown 例外 —— 中央文件最小 delta 惯例。
- `jsoncpp` 归一为单一 INTERFACE target（root CMakeLists:650-674），`#include <json/json.h>`。
- canonical JSON：`canonicalizeJsonRfc8785` 是 Qt 侧（`src/data/execution_fingerprint.h:142`）；jsoncpp 侧叶子库各自做 sorted-key 确定性序列化（agentbench json_writer、verifier 自带）。
- `.planning/*` 默认 gitignore，per-track 白名单 1 行（`.gitignore:127-130` 惯例）。
- `docs/integration.md` **master 上不存在**（#1188/#1189/#1191 都会新建；我方只 additive 追加自己的章节）。

## 2. 并发 track 去重矩阵（2026-09-22 动态检查：6 个 open PR）

| Track / PR | 它做什么 | 与本 track 边界 | 动作 |
|---|---|---|---|
| #1191 RS14-10 Unified Verifier (`src/verify/`) | 判定**契约**（Pass/Indeterminate/Fail 三态 + typed `verify:e_*`/`i_*`），PR 明文："不产分数；grader 保有评分语义" | 我 = 评分语义层。verifier verdict 可作为我的 evidence 之一 | 不链接；定义最小 `verifierVerdict` evidence kind + provider interface；integration 文档写接线点 |
| #1189 Agent Benchmark (`src/agentbench/`) | agent **轨迹**基准（trace 输入、8 metrics、failure taxonomy、PASS/FAIL 三态） | 输入不同（trace 文件 vs 实验 evidence bundle）、语义不同（基准 vs 教学评分 + partial credit + 学生反馈） | 不依赖、不复制；machine-readable 报告投影保证未来可互操作 |
| #1188 RS14-17 Capsule (`src/experiment/capsule/`) | 实验 run → 可移植封装文档（投影，验证完整性） | 它投影"实验是什么"，我评"过程做得如何"；我的 reproducibility 维度可消费 replay-readiness 类 evidence | 不依赖；evidence kind 层面预留 `replay_readiness` 自由载荷 |
| #1190 Curriculum Pack | 课程组织层 | 无交集 | 无 |
| #1192 Capability State Graph | 能力状态转换图 | 无交集（它描述能力，不评学生） | 无 |
| #1193 Scientific Task Planner | goal→plan 决策层 | 它产出计划；我的 rubric 的 requiredStages 是教师配置，不从 planner 读 | 无 |

## 3. 与 open issues 的去重（不触碰区域确认）

本 track 全部为新增模块 + 最小中央 delta，不修改：workflow 执行路径（#1152/#1158）、mission runtime（#1148/#1149/#1168-1170）、ExperimentStore 写路径/一致性（#1161/#1171-1174/#1184）、plugins（#1156/#1157/#1181）、jsoncpp 解析（#1154/#1155）、CLI concurrency（#1185）、原子发布（#1174/#1175/#1178）、geospatial mirror（#1162/#1163）、spectral（#1150/#1146/#1147/#1164/#1165/#1183）。
若实施中发现上述区域问题 → 记录 "observed/blocked"，不修。

## 4. 缺口 → 本 track 交付

1. **过程维度评分不存在**：现有 D4 只评最终产物；workflow/provenance 里的状态转移、checkpoint、metric、lineage 证据没有任何评分消费方。
2. **Rubric 不是 versioned 多维度文档**：`sicnu.lab.rules/1` 是 per-lab artifact 断言，无维度权重/partial credit/alternate pathway/tolerance。
3. **理由链覆盖"得分"**：D4 只解释扣分（deductions）；满分路径、partial credit 的依据、alternate pathway 的选择同样需要 evidence 引用。
4. **学生答案推理评分不存在**：`thinkingQuestions` 无任何评分消费方。
5. **教师版诊断 / 学生版反馈双呈现**：不存在。
6. **Agent outcome verification 的评分投影**：无 machine-readable 评分接口。

## 5. 不做什么（Scope 边界）

- 不实现 LabRuntime 主体；不修复 ExperimentStore open issues（只读 evidence adapter）。
- 不做 Agent loop / 不做轨迹回放（agentbench 的领域）。
- 不做 LLM 自由评分（答案评分是结构化 teacher-authored rubric 匹配，确定性）。
- 不链接 Qt/GDAL/其他 sicnu 库（纯 C++20 + jsoncpp 叶子库，轻量 test lane）。
- 不做 artifact 像素/统计断言（D4 的领域）；artifact 评分结果仅作为 evidence kind 输入。
- 不做第二套 Registry / provenance / experiment store。

## 6. 风险

| 风险 | 缓解 |
|---|---|
| 与 #1191 verifier 语义重叠（都叫"验证"） | 命名/文档显式分工：verifier=契约判定，grader=评分；我接受它的 verdict 作为 evidence，不实现三态契约判定引擎 |
| 中央 CMake delta 冲突（4 个并发 PR 都动 root/tests CMakeLists、.gitignore、docs/integration.md） | 追加式最小 delta；最终 rebase 做 union verification |
| evidence bundle 无限大 | 硬上限（typed `evidence.truncated` 标志，永不静默截断）+ 分页采集约定 |
| 评分语义漂移成第二真相源 | 一切分值来自 rubric 文档；公式只在我模块一处；metrics 公式仍归 evaluation.*（我只读值，不重算公式）|
| "按钮顺序"伪评分 | matcher 以状态转移 + evidence 存在性为准；顺序仅当 rubric 显式声明 `ordered` |

## 7. Extension seams（已确认）

- 新目录 `src/grader/`（target `sicnu_grader`，pure C++20 + jsoncpp）。
- root `CMakeLists.txt` +1 `add_subdirectory(src/grader)`；`tests/CMakeLists.txt` 追加轻量 test 块；`.gitignore` +1 白名单行；`docs/integration.md` 新建（additive）。
- `data/grader/`（exemplar packs，离线）。
- ADR `docs/adr/0174-process-aware-experiment-grader.md`（0172 已被 #1188/#1191 占用×2、0173 被 #1192 占用；仓库既有重复编号惯例，取 0174 避歧义）。
