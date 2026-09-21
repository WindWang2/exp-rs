# Plan — RS14-05 Process-aware Experiment Grader

ADR: `docs/adr/0174-process-aware-experiment-grader.md`（实现 slice A 时落盘）

## Problem statement

现有评分能力（D4 `src/agent/output_verifier.h` + `sicnu.lab.rules/1`）只对**最终产物**断言，且显式不做 partial credit。遥感本科生实验和（未来的）Agent 实验执行都会产生大量**过程证据**：workflow 状态转移、provenance 记录、checkpoint、metric 文档、学生答案。没有任何组件能：
1. 按教师配置的 versioned rubric 对**过程**给分（含 partial credit）；
2. 让每个得分/扣分/零分都**追溯到具体 evidence**（理由链）；
3. 支持 hard constraints、tolerance、required stages、alternate valid pathways；
4. 以**状态转移**而非"按钮顺序"为准；
5. 同时产出学生版反馈与教师版诊断。

## User stories

**本科生视角**：我提交实验后，看到的不是"92 分"，而是"过程完整性 14/20：缺 `accuracy_assessment` 阶段完成记录（workflow provenance 中无该 nodeExec Completed 状态）；结果质量 18/20：overall_accuracy=0.83 低于阈值 0.85（tolerance 0.02 内，得满分……）；解释题 Q2 未命中要点 `train_validate_split`（答案 rubric）"。我知道**下一步该做什么**。

**AI Agent 视角**：我拿到 machine-readable GradeReport（typed reason codes + evidence ids + 每准则状态），可以判断"去补采哪条证据能提高得分"，而不是重跑整个实验。同一 grader core 未来被 outcome verification loop 消费（本 track 不做 loop）。

## Architecture（一句话）

`src/grader/`（target `sicnu_grader`）= **纯 C++20 + jsoncpp 叶子库**（零 Qt/GDAL/sicnu 依赖，对齐并发 track #1189/#1191 的轻量 lane 先例）：三个 versioned value documents（Rubric / EvidenceBundle / GradeReport，serde 严格拒绝外来版本）+ fail-closed 校验器 + evidence matcher（状态/指标/答案，键索引）+ partial-credit 评分引擎（hard constraints、alternate pathways、verdict）+ 纯函数 adapters（provenance/checkpoint/metric/run JSON → evidence items）+ 三种呈现投影（student/teacher/machine）+ 顶层 `grade()` facade。**核心不做 I/O；store 采集由未来 adapter 接线（integration 文档）。**

### 单一事实源纪律
- 指标**公式**仍在 `evaluation.*`（我只按路径读值，`metricValueAtPath` 语义对齐："never zero-filled" → 缺失=indeterminate，不是 0 分静默）。
- 产物断言仍归 D4；`artifact_grade` evidence kind 只**消费**外部评分结果。
- repeat/reproducibility 身份逻辑仍归 `RepeatExecutionClassifier`；我只消费其 verdict 形状的 evidence。
- verifier 契约判定仍归 RS14-10 `src/verify/`（未合并，不链接）；`verifier_verdict` evidence kind + fake provider 先行，接线点写入 `docs/integration.md`。
- 不新建第二套 registry/provenance/experiment store；本模块**无状态**（除输入文档）。

## Public API / data schema

Schema ids（closed set，reader 拒绝外来版本，typed 错误）：
- `sicnu.grader.rubric/1` — `GradingRubric`
- `sicnu.grader.evidence/1` — `GradeEvidence`
- `sicnu.grader.report/1` — `GradeReport`

### Rubric（教师配置，versioned）
```
schema, rubricId, revision, title, totalPoints(=100), passingScore(default 60)
dimensions[]: dimensionId(closed vocab 建议 data_preparation|process|result_quality|interpretation|reproducibility|efficiency，允许其他显式 id), title, weight(=Σcriteria.maxPoints), criteria[]
criterion: criterionId, title, maxPoints, kind(closed: stage|metric|artifact_state|provenance|checkpoint|answer|artifact_grade|verifier_verdict|replay_readiness|custom),
           evidenceKey, requiredCount(default 1), expected{kind-specific},
           stage: {acceptedAlternatives[], orderedAfter[], minDistinctStates[]}
           metric: {mode at_least|at_most|equals|range, value, tolerance(default 0), linearWindow{from,to}?}
           artifact_state: {expectedState, facts{} 子集匹配}
           answer: {questionId, concepts[{conceptId, description, keywords[][], points}], misconceptions[{pattern, deductPoints, explanation}], matchStrategy:"keyword"}
           hints: {student, teacher}
hardConstraints[]: constraintId, title, kind(forbidden_evidence|required_evidence), evidenceKey, effect(cap→capPoints | zero), 
requiredStages[]: stageKey, orderedAfter   （跨维度声明，matcher 额外校验并在报告呈现）
alternatePathways[]: pathwayId, description, criterionIds[]（命名路径；确定性选择规则见下）
budgets: maxCriteria(默认 512), maxEvidenceItems(默认 10000) —— 超限=typed hard error（绝不静默截断）
```

### Evidence bundle（采集方投影，versioned）
```
schema, subject{experimentId?, runId?}, items[]
item: evidenceId(唯一), kind(与 rubric 同闭集), key, state?, value?,
      facts{}(自由载荷：digest、edgeKind、answerText、status、level…), source(溯源标注), recordedAtUtc?
```

### GradeReport（确定性，无 wall clock；digest = sha256(canonical body minus digest)）
```
schema, rubricRef{rubricId, revision, digest}, evidenceDigest, subject
score, totalPoints, verdict(pass|partial|fail|blocked), passingScore
hardConstraintOutcomes[{constraintId, violated, evidenceIds, effect, explanation}]
dimensions[{dimensionId, weight, earned, criteria[{criterionId, maxPoints, earned, status(earned|partial|not_earned|indeterminate|capped), reasonCodes[](closed grader:i_*/grader:e_*), evidenceIds[], explanation}]}]
requiredStageOutcomes[], matchedPathways[]
digest
```
投影（纯函数）：`renderStudentFeedback(report)`（每个丢分点一条"缺什么证据/差多少"）、`renderTeacherDiagnostics(report)`（弱证据覆盖、indeterminate 清单、pathway 选择依据）、`renderMachineSummary(report)`（typed codes + evidence ids，Agent 可消费）。

### 评分语义（确定性，全文档化）
- 校验失败（版本/形状/权重和/重复 id/未知 kind/budget）→ **typed hard error，不产报告**（fail-closed）。
- evidence 缺失/不满足 → 该 criterion `not_earned` 或 `indeterminate`（indeterminate = 无法判定，如 metric 键存在性未知），**绝不绝静默 0 分**：每条必带 reasonCode + explanation。
- partial credit：metric `tolerance` 带=满分；`linearWindow` 内线性（teacher-configured）；answer concepts 按点值累加、misconceptions 扣减并 clamp ≥0；stage 全有全无（状态匹配是离散的）。
- hard constraint 违反 → 全分 `blocked`/封顶（capPoints），违反本身引用 evidence。
- alternate pathway：candidate pathway = 其全部 criterionIds 都 earned>0；**确定性选择**：earned 总分最高的；并列取 pathwayId 字典序；报告列全部 matched。路径选择只影响"哪些准则被计入解释"，不改分数数学。
- verdict：blocked→blocked；score≥passingScore→pass；0<score<passingScore→partial；score==0→fail。

## Migration / compatibility
全新模块，additive-only。中央 delta：root `CMakeLists.txt` +1 行 `add_subdirectory(src/grader)`；`tests/CMakeLists.txt` 追加轻量测试块；`.gitignore` +1 行 `.planning` 白名单；`docs/integration.md` 新建。删除这四处 delta 即完全回滚（kill-switch）。`LabGradeEmbedding.inlineResult` 是未来注入点（本 track 不改 experiment/ 代码）。

## Observability
- 报告自带 rubricDigest + evidenceDigest（可复现审计）。
- `planning/progress.md` 记录每 slice RED/GREEN 证据。
- 报告 body 无 wall clock → 双次运行 byte-identical（测试锁定）。

## Security / trust boundary
- 核心不读文件/网络/DB；adapters 只做纯 JSON 变换（调用方负责已脱敏输入，遵循 `RunEnvironment::redactSecretKeys` 惯例；answer 文本视为不可信输入：长度上限 + 校验，匹配是常量时间无关的纯字符串操作）。
- rubric 是教师权威文档：学生不可篡改（digest 锚定；调用方验证）。
- 失败封闭：未知版本/kind/形状一律拒绝，不做 best-effort 猜测。

## Performance budget
- `grade()` O(items + criteria)，键索引 O(1) 查找；≤10k items + ≤512 criteria → 毫秒级。
- answer 匹配 O(answerText × keywords)；answerText ≤ 64KiB/criterion。
- 无堆爆炸路径：不复制 evidence bundle（const 引用 + 索引视图）。
- 测试 lane 秒级构建（无 Qt/QGIS/GDAL 链接）。

## Test strategy
7 个轻量 Catch2 target（`ctest -R "^test_grader" -j1`）：schema / matcher / scoring / adapters / answers / exemplar / mutation。每 slice RED-first（先证明因缺能力而失败）。全部离线；确定性双跑 digest 断言；mutation 测试保证 oracle 杀伤力（删关键 evidence 必须真实扣分）。

## Work packages（= slices.md）
A schema → B matcher → C scoring → D adapters → E answers → F exemplar → G mutation。

## Definition of Done
公共 DoD + track DoD：
1. 教学端到端：exemplar pack（监督分类实验）→ 学生能看到"缺哪条证据/差多少"、教师看到诊断（弱覆盖/indeterminate/pathway 依据）。
2. machine-readable：`renderMachineSummary` typed codes。
3. 单一事实源：只消费不重算；无第二 registry/store/provenance。
4. unsafe/unknown/unsupported 全 typed，无 silent fallback。
5. 离线可用（exemplar 在 repo 内）。
6. 资源上界：budgets typed；无无界路径。
7. PR 前动态去重（master/6 open PRs/issues）。
