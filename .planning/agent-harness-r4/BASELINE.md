# BASELINE — Track 8: Agent Harness Adversarial Hardening (R4 Deep Edition)

实测时间：2026-09-27（Linux worktree）。所有计数为本地实测（`find`/`stat`/`ls`），非沿用任务书写作值。

## 1. 实测基线

| 项 | 值 |
|---|---|
| `origin/master` SHA | `15e5c66b543ef3874cb929f17529ef456bd6c059`（2026-09-26 08:28 +0800，merge #1333） |
| 本地 master 与 origin/master | 0 领先 / 0 落后 |
| worktree | `../exp-rs-agent-harness-r4`，分支 `hardening/r4-agent-harness`（跟踪 origin/master） |
| 开放 issue | **0**（`gh issue list --state open` 实测为空） |
| 开放 PR | #1334 (fix/review-p1-security)、#1335 (fix/review-p0-build-restore)、#1336 (closure-ui-runtime-r4)、#1337 (closure-workflow-contracts-r4)、#1338 (closure-io-processing-r4) |

## 2. 锚定表复核（任务书 3.1 vs 实测）

| 锚点 | 任务书写作值 | 实测值 | 命令 |
|---|---|---|---|
| `src/agent` 文件总数 | 259 | **259** ✓ | `find src/agent -type f \| wc -l` |
| `src/agent/harness` .h/.cpp 对 | 47 对 | **47 个 .h + 46 个 .cpp**（`intent_vocabulary.h` 无配对 .cpp，任务书 47 对按 47+46 理解成立） | `ls src/agent/harness/*.h \| wc -l` |
| `harness_verification.cpp` 体积 | 16,709 B | **16,286 B**（漂移 -2.5%，以实测为准） | `stat -c %s` |
| `run_loop.cpp` 体积 | 24,139 B | **23,605 B**（漂移 -2.2%） | `stat -c %s` |
| 既有 harness 测试目标 | 9 | **9** ✓（catalog/error/eval_corpus/evals/evidence/grounding/lab_evals/lab_injection + harness9_contracts，tests/CMakeLists.txt 实证） | `ls tests/test_harness*.cpp` |
| 失败注入传统测试 | 3 | **3** ✓（test_model_failure_matrix / test_io_atomic_failures / test_verification_failure_11） | `ls tests/test_*failure*.cpp` |
| 教师令牌 getenv 先例 | 1（lab_copilot.cpp:446） | 待 #1335 rebase 后复核（#1335 正是修 teacher token 的 PR） | `rg SICNU_LAB_TEACHER_TOKEN src/agent` |

## 3. 在途 PR file-overlap map 与逐文件策略

实测各 open PR 触及的 `src/agent` 文件（`gh pr view <n> --json files`）：

| PR | 触及 src/agent 文件 | 与本轨道重叠判定 | 策略 |
|---|---|---|---|
| **#1334** fix/review-p1-security | 仅 7 个：`llm_config_manager.{h,cpp}`、`mcp_server.{h,cpp}`、`tool_catalog/meta_protocol_tools.cpp`、`tool_catalog/surface_registry.cpp`、`tool_catalog/workspace_containment.h` | **零 harness 文件重叠**（任务书预估"~30 文件"高估；实测 harness 重叠 = 0）。`mcp_server.cpp` 是 WP-A 工具幻觉类的测试对象但**只测不改** | 无需避开或等待；mcp 侧仅测试引用，不改实现 |
| **#1335** fix/review-p0-build-restore | `src/agent/CMakeLists.txt`、`src/agent/harness/lab_copilot.cpp` | 本轨道不新增 `src/agent` 源文件（新代码全部落在既有文件内），不触碰 `lab_copilot.cpp` | 天然避开；若最终必须改 `src/agent/CMakeLists.txt`，PR 正文声明 rebase 顺序 |
| **#1337** closure-workflow-contracts-r4 | `harness/grounding_probes.cpp`、`harness/harness_actions.cpp`、`harness/workflow_analysis.cpp`、`harness/workflow_facts.cpp` | `grounding_probes.cpp` 是 WP-D 接缝 | **WP-D 全部测试侧新增**（`tests/test_grounding_evidence_r4.cpp`），不改 `grounding_probes.cpp` 实现；`evidence.{h,cpp}` 不在 #1337 清单内，可改 |
| **#1336** closure-ui-runtime-r4 | 无 src/agent 文件（i18n/help/lab-pack） | 无重叠 | 忽略 |
| **#1338** closure-io-processing-r4 | 无 src/agent 文件 | 无重叠 | 忽略 |

**结论**：任务书担心的 #1334 大面积重叠不成立（实测 0 个 harness 文件）。唯一实现侧敏感点：不改 `grounding_probes.cpp`（#1337 在途）、不改 `lab_copilot.cpp`（#1335 在途）、不改 `mcp_server.cpp`（#1334 在途）。本轨道的实现改动集中在：`harness_verification.{h,cpp}`、`agent_plan.{h,cpp}`、`context_ledger.{h,cpp}`、`evidence.{h,cpp}`、`context_checkpoint.{h,cpp}`（均无在途 PR 重叠）+ `tests/`。

## 4. 评审材料通读

任务书点名的四份材料（`PROJECT_REVIEW_DOSSIER_5.0.md`、`AUDIT_DOSSIER_ISSUES_747_760.md`、`PR_TRIAGE_REPORT_2026-09-16.md`、`docs/PARALLEL_TRACKS_10.md`）**在 master 根目录实测不存在**。按任务书预案"以 git 历史与 PR 描述为准"，替代材料：`review/DEEP_REVIEW_R2.md`、`review/COVERAGE_LEDGER.csv`、`review/GOAL_MATRIX.csv`、`review/findings/`、`review/issues/`、`CLAUDE.md`、`CONTEXT.md`、`ISSUES.md`、`TEST_INFRA.md`。

## 5. 构建与工具链事实

| 项 | 值 |
|---|---|
| cmake | `/home/kevin/toolchain/cmake-dist/bin/cmake`（不在默认 PATH） |
| ninja | `/home/kevin/pwb-sdks/root/usr/bin/ninja`（用户 shell 有 `ninja -j40` alias，本轨道一律显式全路径 + `-j2`） |
| 编译器 | `/usr/sbin/c++`（GCC） |
| Qt6/依赖 | `CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`；configure 实测全依赖本机齐备（Qt6/OpenCV 5.0.0/jsoncpp/GDAL/protobuf 7.36.1），无需网络 |
| 构建目录 | `build-r4`（全新，Debug + ENABLE_TESTS=ON，配置耗时 357s） |
| lab 档 | `SICNU_LAB_PROFILE=ON` 会 **FORCE `ENABLE_TESTS=OFF`**（cmake/SicnuLabProfile.cmake 实测）→ 本轨道不可用，标准构建 + 点名目标构建 |
| 资源纪律 | `CMAKE_BUILD_PARALLEL_LEVEL=2`、`ninja -j2`、`CTEST_PARALLEL_LEVEL=1`、`QT_QPA_PLATFORM=offscreen`；只点名构建本车道测试目标（全仓 ~2000 测试目标，全量构建在本轨道预算内不可行也不必要——Oracle 只要求 harness 车道） |

## 6. 关键实测事实（修正任务书心智模型）

1. **`run_loop.{h,cpp}` 不是模型驱动的运行循环**：它只注册 `harness:diagnose_run`（有界诊断预算、只提议不执行）。任务书五条防线图中"run_loop.cpp 消费 LLM 产出"的前提不成立。
2. **真实 LLM 产出消费链**（rg + 通读实证）：
   - SSE 层：`src/agent/llm_streaming_client.cpp` —— `parseSseLine` 累积工具调用；`emitParsedToolCallOnce` 对 `finish_reason=length` / 参数非 JSON 的处理是**仅 qWarning 丢弃（#701），无类型化信号**；参数累积与 `m_buffer` **无上限**；空 name 工具调用静默跳过。
   - 白名单/缺参层：`src/processing/framework/tool_call_dispatcher.{h,cpp}` —— `classify`/`validateCall`/`rejectionReason`；未知工具拒绝是裸字符串 "Algorithm not registered: %1"（MCP 侧为 `UNKNOWN_TOOL`，harness_error 有 `TOOL_NOT_FOUND` 但生产分派未用——三种未知工具词表并存）。
   - 计划层：`src/agent/harness/agent_plan.{h,cpp}` —— `readAgentPlan`（INVALID_PLAN 各分支）、`validateAgentPlan`（未知 operator id 白名单 = `RSOperatorRegistry`+`AtomicAlgorithmRegistry`）、`compilePlanToWorkflowJson`；**step params 逐字透传，无计划级参数 schema 校验；无大小/步数/深度限制**。
3. **`ContextLedger` 不是"每工具调用一行"的账本**：它是有界存储（plan bindings ≤8 / decisions ≤20 / understanding ≤32 / asset contexts ≤32 / model contracts ≤8 / run summaries ≤12 且总 token 预算 8192），QMutex 线程安全。任务书 WP-C 的"三路径逐行记账"前提不成立。
4. **#1325 crash-safe 持久化的真实落点**：`harness/context_checkpoint.{h,cpp}`（`HarnessSessionStore`：QSaveFile 原子写、64KiB 文档上限、8 会话上限、staleness→resume 回退 grounding；sessionId 有 `sessionIdRejection` 字符集校验，无路径穿越）+ `agent_loop/session_journal.{h,cpp}`（自研 `writeFileAtomic`：O_EXCL 索名 + fsync + 原子 rename）+ `src/workflow/workflow_checkpoint.h`（**Track 10 领地，只测不改**）。`loadSession` 对损坏文档复用 `INVALID_PLAN` 码（语义漂移，记录为发现）。
5. **`verifyArtifact` 是磁盘产物核验**（GDAL/OGR），探测网格有界（64×64 采样、4096 unique 上限）；expectations 由调用方映射，verifyArtifact 自身不解析 JSON 文档。
6. **自相矛盾探针（失败类 8）现状不存在**：`validateAgentPlan` 无同一输出路径冲突检测（`kOutputPathCollision` 码已存在于词表）。WP-A 最小实现落点：计划级一致性探针。

## 7. 本轨道边界声明

**白名单**：`src/agent/`（限 `harness/` + 直接支撑）、`tests/`（含 `tests/CMakeLists.txt`）、`.planning/agent-harness-r4/`。
**明确不做**：教学判分自动化；`src/workflow`（Track 10）、`src/operators`（Track 7）、mcp/pi 表面（Track 9）——`workflow_checkpoint.h`/`tool_call_dispatcher` 属邻接领地：**只读+只测不改实现**（测试文件在 `tests/` 白名单内，链接其库合法，先例 `test_verification_failure_11` 链 `sicnu_contracts`）。
**在途重叠规避**（见第 3 节）：不改 `grounding_probes.cpp` / `lab_copilot.cpp` / `mcp_server.cpp` / `src/agent/CMakeLists.txt`。

## 8. WP 前提修订（依据第 6 节证据，按任务书"前提不成立→记账后跳过或收窄"规则）

| WP | 任务书前提 | 实测 | 修订 |
|---|---|---|---|
| WP-A | 失败矩阵注入 run_loop | run_loop 是诊断工具；真实链 = SSE/白名单/计划三层 | 8 类失败重新锚定到三层真实接缝（映射见 PLAN.md） |
| WP-B | 验证器消费 LLM 产出 JSON | verifyArtifact 消费磁盘产物+结构体 | 畸形输入锚定 verifyArtifact 入参 + expectations + SSE 层 JSON 解析（递归/深嵌套的真实风险点） |
| WP-C | ledger 每工具调用一行 | 无此机制 | 收窄为真实存储合同的完整性/边界（驱逐、预算、替换、staleness、线程安全、畸形输入） |
| WP-E | run_loop 步进恢复 | 恢复合同在 HarnessSessionStore + SessionJournal | 注入矩阵重锚定到 checkpoint 损坏/撕裂/超限/staleness/并发/foreign-version |
| WP-D/F/G | — | 前提成立 | 按原计划（WP-D 纯测试侧；WP-F 锚定 harness/lab tools 入参校验；WP-G 语料单源） |
