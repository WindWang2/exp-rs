# PLAN — Track 8 R4（按实测接缝修订版）

基线见 BASELINE.md。TDD 节拍：每接缝一红→最小实现变绿→原子提交；拒绝同义反复断言（期望值来自本文件写明的独立合同推演，不用实现反算）。

## 失败矩阵 8 类 → 真实接缝映射（WP-A）

| # | 失败类 | 真实注入点 | 期望合同（独立推演，写入用例注释） | 现状 | 动作 |
|---|---|---|---|---|---|
| 1 | 幻觉引用 | `validateAgentPlan`：plan step 引用注册表外 operator | FAIL=INVALID_PLAN + "Unknown operator id" + `search_capabilities` 建议 | 已实现 | 对抗用例钉住（含文件引用幻觉路径） |
| 2 | 格式错误 | `readAgentPlan`：顶层数组/标量而非 object | INVALID_PLAN，结构化拒绝，不崩溃 | 已实现 | 对抗用例钉住 |
| 3 | 拒答/空产出 | `readAgentPlan`：steps=[] / SSE 空 arguments | steps=[] ⇒ INVALID_PLAN；SSE 空 args ⇒ 结构化 `{}` 或丢弃+可观测信号 | 部分实现 | 用例钉住 + 可观测性补齐 |
| 4 | 超长截断 | `LlmStreamingClient`：`finish_reason=length` | 截断工具调用必须**可观测**（类型化信号/结构化拒绝），不得只 qWarning | 仅 qWarning | **最小实现**：`malformedToolCall` 类型化信号（携带原因/finish_reason/name）+ 缓冲上限 |
| 5 | 部分 JSON | `LlmStreamingClient`：arguments 前半合法后半垃圾 | 解析失败必须可观测（非静默丢弃），携带解析错误定位 | 仅 qWarning | 同 #4 信号路径 |
| 6 | 工具缺参 | `ToolCallDispatcher::validateCall`：缺必填参数 | `{valid:false, errors:[{code,parameter,...}]}` 含字段名 | 已实现 | 对抗用例钉住（含 0 参数/错型/空串） |
| 7 | 工具幻觉 | dispatcher 拒绝 + MCP `UNKNOWN_TOOL` | 未知工具名白名单拒绝；生产词表统一为 harness `TOOL_NOT_FOUND`（分派路径） | 三词表并存 | 测试钉住现状合同；词表统一列为 backlog（跨领地，不动） |
| 8 | 自相矛盾 | `validateAgentPlan`：同一 plan 内两 step 声明同一输出路径（产出互斥陈述） | 至少 1 条 INVALID_PLAN issue（`OUTPUT_PATH_COLLISION` 码），consistency 探针语义 | **不存在** | **最小实现**：计划级一致性探针（agent_plan.cpp，无在途 PR 重叠） |

## WP-B 验证器鲁棒性（`tests/test_verifier_robustness_r4.cpp`）

八类畸形输入 → `verifyArtifact`/expectations/SSE JSON 解析：空路径、不存在路径、损坏文件（截断 GeoJSON/GTiff 头）、超大逻辑栅格（VRT 100k×100k，断言有界时间与有界采样）、深嵌套 JSON（1e5 层，解析器不得栈溢出——实测 jsoncpp 行为后钉合同）、非 UTF-8 路径字节、数字溢出（1e308 extent）、类型错位（classValues 给字符串/expectedExtent 给数组）。合同：**任何输入必须终止且给结构化 Verdict**（FAIL 或 warning 类 check），禁止崩溃/挂起。

## WP-C ContextLedger 完整性（`tests/test_context_ledger_completeness.cpp`）

真实合同（context_ledger.h 头注释独立推演）：三记录路径（planBinding / decision / runSummary）字段完备；绑定重写原地更新；decision 生命周期（record→resolve→未知 id false）；understanding 命中/未命中；asset context staleness（文件删除/mtime 变化→stale 标记）；model contract 替换；**有界驱逐**（>8/>20/>32 oldest-first）；**run summary token 预算**（>8192 逐出至回落）；并发写（多线程 record 无崩溃无丢失行）；畸形输入（空串、超长键、非 UTF-8、null Json）。合同：**任何输入终止 + 存储有界 + 字段完备**。

## WP-D Grounding/Evidence oracle（`tests/test_grounding_evidence_r4.cpp`，纯测试侧）

确定性预录表驱动：`probeDatasetFacts` 未知 scope ⇒ 类型化错误（I/O 前）；未知引用 ⇒ ENTITY 类错误不猜测；`probeLimits()` 与 ProbeLimits 漂移锚；`probeModelManifest` 未知 id ⇒ typed-unknown、artifact stat-only。evidence：`harvestUncertainty` 预录 StepPlan 表 → 逐字段断言 harvest（闭合键集：uncertainty/uncertaintyOutput/uncertainty_band/confidence；跨步骤按输出路径匹配）；sidecar 三写一读 round-trip；`writeProvenanceSidecarIfAbsent` 不覆盖引擎 sidecar；不可写路径 ⇒ `SidecarResult.error` 非空且产物不受损；`readVerificationEvidence` 跨 run 拒绝。浮点容差 ≤1e-12。桩仅测试编译单元。

## WP-E 崩溃恢复压力（`tests/test_runloop_recovery_r4.cpp`）

注入矩阵 → `HarnessSessionStore` + `SessionJournal`（src/workflow 引擎 checkpoint 只读不改）：

| 注入点 | 方式 | 恢复后断言 |
|---|---|---|
| 1 损坏会话文档 | 直接向会话目录写截断/垃圾字节 | loadSession fail-closed 类型化错误，不崩溃、不返回半状态 |
| 2 撕裂写模拟 | 原子写中途失败（不可写目录/超限文档） | 旧文件完整可读；`saveSession` 类型化错误 |
| 3 超 64KiB 文档 | 构造超限 state | 先压缩一次，仍超限 ⇒ 类型化失败，**不落盘** |
| 4 foreign schemaVersion | 手工改版本字段落盘 | loadSession fail-closed |
| 5 resume 边界/staleness | 保存后改输入文件 mtime/删除 | resumeSession 回退 grounding；decisions 存活 |
| 6 并发保存 | 双线程同目录 save | 无撕裂文档（复用 journal durability 先例） |
| 7 恶意 sessionId | `../`、控制字符、127+1 长度 | `sessionIdRejection` 全拒绝，路径不逃逸 |
| 8 取消竞态（journal 面） | `requestCancel` 后 resume | 取消状态保持，无复活执行（对齐 test_agent_loop_resume 既有合同） |

## WP-F 边界矩阵（`tests/test_harness_boundaries_r4.cpp`）

锚定 harness/lab 工具入参校验入口（`plan_tools` harness:plan/execute_plan 的入参合同层）+ `HarnessError` 形状：空参数对象、未知字段、1e6 字符串、空数组、单元素数组、布尔给字符串位、null 传播。合同：typed error + 字段名 + `errorEnvelope` 形状稳定（code/category/retry_class/recoverable/suggested_actions）。计划级大小/步数上限：现状无限制（BASELINE 6.2）——最小实现保守上限（步数 ≤4096、文档 ≤4MiB，超限 INVALID_PLAN），先例：HarnessSessionStore 64KiB bound。

## WP-G 语料收口

`tests/data/harness_adversarial_corpus.json`：结构化失败样本（`_schema` 版本字段 + 每样本 {id, failure_class, target_seam, payload, expected{verdict|code|observable}}）。加载器（tests/ 内共享 helper）做 schema 自校验。被 ≥2 目标复用（failure-matrix 扩展 + verifier robustness）。新目标注册进 `tests/CMakeLists.txt`（`sicnu_add_test`），双跑全绿。

## 提交序列（≥10 原子提交目标）

C1 planning 工件 → C2 WP-A#2/1（plan 层对抗用例）→ C3 WP-A#4/5（SSE 可观测信号最小实现+用例）→ C4 WP-A#8（一致性探针最小实现+用例）→ C5 WP-A#3/6/7（dispatcher/SSE 对抗用例）→ C6 WP-B（verifier 鲁棒性+修复）→ C7 WP-C → C8 WP-D → C9 WP-E → C10 WP-F → C11 WP-G（语料+注册）→ C12 复审修复。每个提交独立可编译（编译 = 本车道目标点名单）。

## Token 记账口径

账本记录"本轮/累计 tokens"为**估算值**：无法从 harness 内精确读出自耗，口径 = 会话工具回报（subagent 用量精确）+ 主线按活动量估算（读源 ~行数×系数、编译等待、写作量）。任务书 280M 预算与本轨道实际 Token 消耗若出现数量级偏差，以账本实记为准、PR 如实报告，不为凑数注水。
