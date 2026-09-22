# Recon — RS14-04-labspec2-runtime（LabSpec 2.0 + Undergraduate Lab Runtime）

Date: 2026-09-22 · Baseline: `origin/master@4f6632e1f`（与本 prompt 声明基线一致）· Branch: `agent/rs14-labspec2-runtime`

## 0. 动态去重（执行时实测，非 prompt 快照）

**open PRs（6 个，prompt 生成时为 0）：**

| PR | 主题 | 与本 track 的边界裁定 |
|---|---|---|
| #1190 RS14-18 Undergraduate Curriculum Pack | `sicnu.curriculum/1` 课程组织层（10 模块/16 labs），在既有 lab 契约链之上做教学组织 + `sicnu.curriculum.progress/1` 学生自报进度文档 | **最近邻**。它做"课程→lab 的组织与投影"，且其进度文档**明确不构成成绩/运行证据**；本 track 做"单门实验的运行时状态机 + 机器可验证 checkpoint"。不重叠、可互补：其 progress 文档可未来投影自本 track 的 session。文件碰撞点：`lab-registry.json`（双方 append 条目）、`tests/test_labspec.cpp`（双方加用例）、`tests/CMakeLists.txt`（双方加 target）、`gen_lab_*.py`、`.gitignore`、`docs/labs` 生成页。lab 编号：#1190 用 lab15/16，**本 track 用 lab17/18/19**。 |
| #1188 RS14-17 Reproducibility Capsule | `sicnu.capsule` v1：ExperimentRun 的可移植投影/摘要/digest | 复现语义分工：capsule 消费 **已记录的 ExperimentRun**；本 track 的 session 只**引用** execution refs（experiment run id），复现性要求（seed/确定性声明）是 spec 层需求 + session 头部记录。不实现 capsule、不复制其 digest 体系。 |
| #1189 Agent Benchmark | data/agent/bench 轨迹评测 | 无接触。 |
| #1191 RS14-10 Unified Verifier | 统一科学结果验证层（ADR 0172） | 本 track 的 checkpoint 验证是**教学 artifact 存在性/操作历史**级别的轻验证，不是科学结果验证引擎；最终评分/验证引擎归该 track。checkpoint verdict 词表 `pass|fail|unverifiable` 沿用 OutputVerifier 既有约定（仅命名对齐，不调用不复制）。 |
| #1192 RS14-08 Capability State Graph | 能力状态转换图（ADR 0173） | 无接触。allowed_tools 白名单是 spec 层静态声明 + session 记录，不做能力图。 |
| #1193 RS14-09 Scientific Task Planner | goal→ScientificPlan 决策层 | 本 track **不做** planner/agent 执行，无接触。 |

**open issues**：避让清单（#1146–#1187）全部记录在案；本 track 设计上不触碰 mission/workflow-engine-内部/plugin/sar/spectral 检测等区域。注意 #1154/#1155 jsoncpp depth bomb：**本 track 新增解析器一律 `stackLimit` + try/catch**（现有 lab 解析器未设 stackLimit，是既有缺陷，不扩大修复——仅新代码采用硬化模式，并在 PR 已知限制中注明）。

## 1. 已有能力（master 现状，单一事实源清单）

### 1.1 LabSpec 契约（ADR 0146）
- Schema `data/schemas/labspec.schema.json`（draft-07，`additionalProperties:false`）：`spec_version` 整数 enum **[1,2]**。v2 = v1 严格超集（objective_zh/glossary/expected_artifacts/param_ranges/grading_rules/principles/prerequisite_knowledge）。steps 线性（operator_id XOR action XOR manual；params 需 operator_id）。
- **权威加载器**：`src/app/widgets/lab_spec_loader.{h,cpp}`（QtCore+jsoncpp，namespace `lab`）：结构校验、typed `LabSpecError{path,labId,reason,line}`、拒绝 unknown keys、拒绝 v2 键入 v1、拒绝不支持的 spec_version。**不识别 v3/任何 runtime 概念**。
- **Harness 轻目录**：`src/agent/harness/lab_spec.{h,cpp}` `LabSpecCatalog`（id/labIds/stepDoc（扣留参数答案）/stepIndexFromMessage）。
- **注册表**：`data/labs/lab-registry.json`（`sicnu.lab-registry/1`，lab 身份唯一权威：canonical/aliases/course_index/grading_rules/pipeline/data_spec 指针），门禁 `scripts/check_lab_registry.py`（强制 `spec_version ∈ {1,2}`、id==stem、v2 键检查）。
- **文档是构建产物**：`scripts/gen_lab_docs.py --check`（零 diff 门禁）；`gen_lab_packs.py --check`（pack 清单同步门禁）。
- ** migrator**：`scripts/upgrade_labspec.py`（文本级 v1→v2 + D3 `.labspec.json` 合并）。C++ 层无 migrator。

### 1.2 数据包 / 判分 / 报告
- `sicnu.lab-pack/1`（`data/labs/packs/*.pack.json`，17 个）：committed-fixture 输入硬性 sha256+bytes；generated-samples/tmp 为 presence+soft-size。加载/校验 `src/agent/lab_data_pack.{h,cpp}`（只读、流式哈希、typed load errors）。
- 判分：`sicnu.lab.rules/1`（`data/labs/grading/*.rules.json`）+ `src/agent/lab_grader_kernels.h`（断言内核）+ `OutputVerifier::gradeArtifact()`（verdict `pass|fail|unverifiable`）。**最终评分引擎另有 track，本 track 不做。**
- 离线：ADR 0147 offline classroom bundle（`sicnu.offline_bundle/1`，manifest sha256，size ceiling）；ADR 0166 offline-labs-registry-and-classroom-safety。

### 1.3 运行时/持久化可复用设施
- **Experiment**：`ExperimentStore`（SQLite，`<project>/.sicnu/lab/experiments.db`，validated 状态转换 `experiment.bad_transition`）；`ExperimentRunRecorder`/`run_bridge`（closed `ExecutionEvent` 词表）；`LabRunRecorder`（guided lab 执行自动记录为 experiment run）。
- **Workflow**：`WorkflowRun`/`WorkflowRunState`（validated transitions + attempt/resumeOf envelope）；`WorkflowCheckpointManager`（tmp+fsync+rename 原子持久化、resume ghost 抑制）；ADR 0162 Workflow IR v2（`StepKind::{Operator,Interactive,Review,Composite}`、`SessionMode`）；`liftLabSpecToWorkflow()` 已存在。
- **Provenance**（不建第三套）：`ProvenanceGraph`（canonical sorted JSON）+ `ArtifactStore`/derivation + ExperimentStore lineage；harness 侧投影范式（`provenance_projection.h`/`evidence.h` 只写自己的 sidecar）。
- **Typed 错误词表惯例**：`sicnu::data::Result<T>`+`Diagnostic{code,message,severity}`（点分小写 code，如 `dataset.cursor_mismatch`、`experiment.bad_transition`）；`RSOperatorError` ErrorCode enum；harness 稳定 wire codes。
- **JSON 硬化惯例**：`CharReaderBuilder` + `builder["stackLimit"]=N` + try/catch（plugin_manifest 等 6 处采用；**lab 解析器尚未采用**）。
- **测试**：Catch2 全仓；轻目标样板 `sicnu_add_d17_test`（Catch2+Qt6 Core/Gui）与 `sicnu_add_io_test`（无 Qt）；`test_labspec`/`test_lab_data_pack`/`test_lab_grader_kernels` 等既有 lab 契约测试。

### 1.4 UI 现状
- `GuidedWorkflowWidget`（`src/app/widgets/`，已发货 dock）：内存态 step index，**无任何学生进度持久化**；`stepCompleted` 等信号无消费者；重启全丢。
- `LabStepCard.isCompleted` 字段存在但**从未置 true**（D17 workbench 未接主窗口）。
- `lab_copilot`（Qt-free 教学问答）：hint 锚定 current_step、TEACHING_REFUSAL、**零学生模型/零历史**；host 每次调用必须传 lab_id/current_step。
- QSettings 仅身份（`lab/studentName` 等）。

## 2. 缺口（= 本 track 的真空隙，全部无既有实现）

1. **spec 层**：无 stages/checkpoints/allowed-tools 白名单；无结构化 questions/reflections（thinking_questions 是裸字符串）；无 hint 政策（预算/升级）；无可复现性要求；data-pack 依赖未在 spec 声明（pack 文件存在但 spec 不引用）。
2. **运行时**：无 `LabRuntimeSession`——无 stage/attempt/checkpoint/artifact-ref/student-choice 状态机；无 session 持久化/断点恢复/离线包 session 语义；hint 揭示无记录；checkpoint 无机器可验证契约。
3. **版本**：`spec_version` enum 无第 3 代；无 C++ migrator/derived-plan 机制让旧 spec 进入运行时。

## 3. 关键设计裁定（动态事实 vs prompt 文本的分歧处理）

1. **"LabSpec 2.0" 命名冲突**：master 已消费 `spec_version: 2`（静态内容超集）。版本号 append-only 不复用 → 本 track 的运行时世代落地为 **`spec_version: 3`**（campaign 名保留 "LabSpec 2.0"，repo 内契约名 LabSpec v3 / `runtime` block）。schema enum {1,2}→{1,2,3}，v1 禁 v2+v3 键、v2 禁 v3 键、v3 全允许（additive）。
2. **不硬阻塞执行**："自由探索 + checkpoint 可验证"：session 层永不阻止操作执行（执行归 TaskCenter/JobEngine）；gate checkpoint 只产生 `blocked_advance` 状态供 UI/教师消费，杜绝脚本化死流程。
3. **checkpoint ≠ 评分引擎**：check kinds 仅 3 种（artifact_present / operator_invoked / question_answered），全部基于 session 自身记录 + 注入文件系统；不调用 OutputVerifier/grading rules；verdict 词表对齐既有 `pass|fail|unverifiable`。
4. **不建第二套 catalog**：v3 深校验放新模块 `src/lab/`（Qt-free），权威加载器 `lab_spec_loader` 只做**最小扩展**（识别 v3 键不拒绝），避免并行真相源；harness `LabSpecCatalog` 不动。
5. **新模块目录 `src/lab/`**（`src/runtime/` 已被 chunk-execution runtime 占用）：static lib `sicnu_lab_runtime`，jsoncpp+std，Qt-free 源纪律 + layer guard；根 CMakeLists 仅 +1 行 `add_subdirectory`。
6. **ADR 编号**：取 **0174**（0172/0173 已被 open PR #1191/#1192 声明）。rebase 时若冲突则顺延并全仓改名。

## 4. 不做什么（Scope 边界）

- 不实现最终评分引擎/grading kernel 扩展（另有 track）。
- 不做 Agent planner / agent execution；Agent 侧仅要求 LabSpec v3 + session JSON 机器可读。
- 不重写 Offline Lab framework；不迁移 14 个既有 v2 spec 内容（保持 v2 原样，adapter 派生默认 plan）。
- 不实现 curriculum 层（#1190 领域）；不做 capsule（#1188 领域）；不触碰避让清单 issues。
- 不建第三套 registry/provenance/experiment store/catalog。
- 不修 #1154/#1155（仅新代码自采用 stackLimit 硬化）。

## 5. 风险

| 风险 | 缓解 |
|---|---|
| 与 #1190 合并冲突（registry/test_labspec/CMake/gen 脚本/生成文档） | append-only 式增量（registry 追加 lab17-19 条目、test 追加用例、CMake 追加 target）；rebase 时语义 union + 重新生成文档；lab 编号错开 |
| `test_labspec`/`check_lab_registry.py`/`gen_lab_docs.py --check` 等既有门禁被新 exemplar 触发 | exemplar 走 ADR 0146 data-commit 全路径：registry 条目 + pack + 文档再生成 + 门禁扩展 {1,2,3} |
| GUI loader 扩展破坏 v1/v2 行为 | v1/v2 校验路径零改动（快照测试先行锁定）；v3 键仅"接受"，深校验在 src/lab |
| session 持久化非确定 | canonical sorted-key JSON、无时间戳（seq 单调）、tmp+fsync+rename、byte 级 round-trip 测试 |
| checkpoint 验证逃逸为大文件 IO | 注入 fs 接口 + byte budget 上限（默认 64 MiB），超限 typed `unverifiable` |
| exemplar fixtures 需离线确定 | 纯代数生成器（无 RNG/时间戳），generated-samples provenance，pack presence 校验 |

## 6. 与其他 19 tracks 的接口点（写入 docs/labspec-runtime-integration.md）

- **→ #1190 curriculum**：`LabSessionStore::listSessions()` 可作为其 progress 投影源；接口 = session JSON 只读。
- **→ #1188 capsule**：session.execution_refs 指向 experiment run id；capsule 照常消费 ExperimentRun，不感知 session。
- **→ #1191 verifier**：checkpoint verdict 词表对齐；未来 verifier 可作为更强 checkpoint check kind 的 provider（本 track 预留 check-kind versioning，不实现）。
- **→ 未来 agent track**：`src/lab` 全部 API 为 Qt-free 纯 C++ 值语义 + typed result，可直接被 harness 工具包装（本 track 不注册任何 agent tool）。
