# Slices — RS14-05 Process-aware Experiment Grader

每 slice：RED（先写失败测试，确认因缺能力失败）→ GREEN（最小实现）→ REFACTOR → narrow ctest → adversarial 复核 → commit → progress.md。

| Slice | 交付 | 测试 target | 核心行为覆盖 |
|---|---|---|---|
| A | `grader_types` (Rubric/Evidence/Report serde + 严格版本/形状校验) + `grader_error` closed 词表 + `grader_json` canonical writer + `grader_sha256` + `grade()` stub facade + CMake 骨架 + ADR 0174 | `test_grader_schema` | happy serde roundtrip；外来版本拒绝；缺失/未知字段 typed 拒绝；权重和=weight、总分校验；重复 id；budget 超限；canonical 双跑 byte-identical；digest 确定 |
| B | `grader_matcher`：evidence 键索引；stage 状态匹配（含 acceptedAlternatives、orderedAfter、minDistinctStates）；artifact_state facts 子集匹配；metric 取值定位（缺失→indeterminate，never zero-fill）；歧义消解（确定性：recordedAtUtc 新者优先，再 evidenceId 字典序，且必引用） | `test_grader_matcher` | 匹配 happy；按钮顺序无关（无 ordered 时乱序证据仍匹配）；ordered 声明后违反→not_earned；多候选歧义确定性；budget/cap；invalid input |
| C | `grader_scoring`：partial credit 数学（tolerance band、linearWindow、clamp）；hard constraints（forbidden/required → cap/zero，cited）；pathway 确定性选择；verdict 规则；**理由链完整性不变量：任何 earned<maxPoints 的 criterion 必有 ≥1 reasonCode** | `test_grader_scoring` | 全分路径引用 evidence；扣分/零分/indeterminate 全部有 reason+explanation；cap/block；pathway 并列字典序；双跑 digest 一致 |
| D | `grader_adapters`（纯 JSON→JSON）：workflow provenance 文档→stage/provenance items；checkpoint→checkpoint items；metric 文档(MetricRecord 形状)→metric items；ExperimentRun 投影形状→artifact_state/replay items；`verifier_verdict`/`artifact_grade` 形状透传 + fake provider | `test_grader_adapters` | 各 adapter happy/malformed（typed 拒绝，不静默丢）；版本 envelope 检查；O(n) 有界 |
| E | `grader_answers`：concept keyword 匹配（每组 anyOf、归一化小写/空白）、点值累加、misconceptions 扣减 clamp≥0、matchStrategy 诚实标注、64KiB 上限 | `test_grader_answers` | 概念命中/未命中引用 answer evidence；误解扣分有解释；空答案=0+indeterminate?（否：答案缺失=not_earned+reason `grader:i-evidence-missing`；答案在但零命中=0 分 + 明确 explanation）；unicode 归一化 |
| F | exemplar packs：`data/grader/examples/`（1 个完整教学 rubric + 2 个 evidence bundle（good/weak student）+ 期望报告 digest），测试经真实 engine 验证；`docs/integration.md`（接线点：LabGradeEmbedding、verify/、ExperimentStore 只读采集、capsule readiness） | `test_grader_exemplar` | good bundle→pass（分数=手算值钉死）；weak bundle→partial 且学生反馈指向缺失项；digest 钉死；离线 |
| G | mutation tests：对 exemplar bundle 做系统变异（删 stage 证据、改 metric 值越过 tolerance、删 answer、注入 forbidden evidence、篡改 report digest）→ 断言分数真实变化/方向正确/typed reason 出现 | `test_grader_mutation` | 每个关键准则的 oracle 杀伤力；digest 灵敏度；分数单调性（删证据不得涨分） |

## 顺序与并行
A → B → C 严格串行（schema 是地基）。D/E 可并行（互不依赖，仅依赖 A）。F/G 依赖 A-E。实现由主 agent 串行执行（编译资源纪律），adversarial-reviewer subagent 在 A-C 后、E 后、最终 PR 前各做一轮独立 review。
