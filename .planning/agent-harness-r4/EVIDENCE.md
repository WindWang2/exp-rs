# EVIDENCE — Track 8 R4（随验证进度滚动补全）

> 状态标记：[待验证] = 已落盘待编译运行；[红] = 红跑实证；[绿] = 双跑全绿实证；[收窄] = 前提修订/在途 PR 规避。

## A. 发现的真实缺陷（与本轨道修复）

| # | 缺陷 | 位置 | 证据 | 处置 |
|---|---|---|---|---|
| A1 | `readAgentPlan` 对非 string 的 `kind`/`schema_version`/`goal`/`plan_id`/`intent` 直接 `.asString()` → 抛 `Json::LogicError`，违反头注释 "Returns false with a typed error, never throws" 合同 | src/agent/harness/agent_plan.cpp:69-93 | 合同对照 + jsoncpp 语义；对抗用例 test_harness_adversarial_matrix "type-confused discriminator" | [待验证] 本轨道修复：判别字段读期类型化拒绝；内容字段退化默认值 |
| A2 | `validateAgentPlan` 对非 string 的 `verification`/`role`/title/接线字段同样抛异常 | agent_plan.cpp:215-248 | 同上 | 本轨道修复：结构化 issue |
| A3 | 计划级自相矛盾（重复输出名=两个 step 声明同一产物）无任何探针 | agent_plan.cpp validateAgentPlan | 词表有 `kOutputPathCollision` 但计划层不查 | 本轨道最小实现：Duplicate output name issue |
| A4 | 计划无步数上限（失控模型可令校验/编译无限走图） | agent_plan.cpp | 实测无任何 bound | 本轨道修复：`kMaxPlanSteps=4096` |
| A5 | SSE 层截断/坏参数/空名工具调用仅 qWarning 丢弃（#701 遗留可观测性缺口）；arguments 累积与 SSE 行缓冲无上限 | src/agent/llm_streaming_client.cpp:245-325 | 通读实证 | 本轨道修复：`malformedToolCall` 类型化信号 + 1MiB/8MiB 双上限 |
| A6 | **`probeModelManifest` 永远失败**：`modelContracts()` 返回数组（context_ledger.cpp:282），而探针按对象键检查 `contracts.isMember(modelId)`（grounding_probes.cpp:428-435）→ 抗幻觉面全失效 | grounding_probes.cpp | 逐行对照；红跑（见 §C） | [收窄] **#1337 已含逐字修复**（其分支 diff 实证，grounding_probes.cpp:450 区域）；本轨道不重复修，避免合并冲突——只钉 master 上成立的合同（unknown/empty id 类型化拒绝） |
| A7 | `validateAgentPlan` 接线查重为 O(steps²) any_of | agent_plan.cpp:204-213 | 通读 | 顺手修复：id 集合 O(1) 查（保持前向引用合法语义，预收集全量 id） |
| A8 | `validateAgentPlan` 的 inputs 名与 pins slot lambda 同样可抛（非字符串 name） | agent_plan.cpp:224,388 | 独立评审复现实证 | 已修：类型化 issue + isString 守卫 |
| A9 | `planFingerprint`/`planSummary` 对非 object step 抛；harness:plan 在校验前调 planSummary | agent_plan.cpp:508,584 | 独立评审复现实证 | 已修：isObject 跳过 + 3 用例 |
| A10 | SSE 网络路径 `m_buffer` 与 tool-call index 映射均无上限（400 MiB 无换行行全量缓冲；100k index +22 MB） | llm_streaming_client.cpp:140,233 | 独立评审实测 | 已修：onReadyRead 未闭合尾部队界 + cancel；index ≤64 |
| A11 | `expectedExtent` 非数值成员抛 `asDouble()` 异常 | harness_verification.cpp:261 | 独立评审 + WP-B 规格 | 已修：四成员 isNumeric 守卫 + 用例 |

## B. 交付物清单（对照任务书 3.2 下限）

| 交付物 | 下限 | 实际 | 状态 |
|---|---|---|---|
| 失败注入类 | ≥8 | 8 类全落真实接缝：幻觉引用/格式错误/拒答/超长截断/部分JSON/缺参/工具幻觉/自相矛盾（+类型困惑第9类） | [待验证] |
| 对抗用例 | ≥20，≥3 新文件 + ≥2 既有扩展 | 新文件 6（adversarial_matrix/verifier_robustness_r4/context_ledger_completeness_r4/grounding_evidence_r4/runloop_recovery_r4/harness_boundaries_r4）+ 既有扩展 2（test_llm_streaming_client/test_tool_call_dispatcher） | [待验证] |
| 验证器对抗用例 | ≥8 | verifyArtifact 8 类畸形输入 + 解析层深嵌套 2 例 | [待验证] |
| 原子提交 | ≥10 | 见 git log（分 WP 提交） | [待验证] |
| 触碰文件 | ≥12 | 实现 5（agent_plan.{h,cpp}, llm_streaming_client.{h,cpp}）+ 测试 8 + CMake 1 + 语料/加载器 2 + planning 4 | [待验证] |
| 语料文件 | ≥1 且被 ≥2 目标复用 | tests/data/harness_adversarial_corpus.json + tests/adversarial_corpus.h（schema 自校验），消费者：adversarial_matrix + llm_streaming_client | [待验证] |

## C. 关键验证记录

### C1 车道 ctest 双跑（Oracle 门禁）

命令（worktree 的 build-r4，全新配置、Debug、ENABLE_TESTS=ON、offscreen、串行）：

```
QT_QPA_PLATFORM=offscreen CTEST_PARALLEL_LEVEL=1 LD_LIBRARY_PATH=$PWB_SDKS/root/usr/lib \
  ctest -R "harness|verifier|grounding|evidence|ledger|llm_streaming|tool_call_dispatcher|model_failure|io_atomic|verification_failure" -j1
```

两轮逐项一致：**98% tests passed, 3 tests failed out of 126**（123/126）。
3 个失败的归属（全部非本轨道引入，均有实证）：

| 失败 | 归属 | 证据 |
|---|---|---|
| test_grounding_probes_11_NOT_BUILT | 纯 verifier 库兄弟目标，master 上同样未构建（链 sicnu_verifier，与车道无关） | ctest -N 列表 |
| harness_lab: teacher path is credential-gated (×2) | **master 既有缺陷**：`constantTimeEquals` 的 `return diff;` 在相等时返回 false（fail-always 而非 fail-closed）→ 教师面永不放行。#1335（fix/review-p0-build-restore）已含逐字修复 `return diff == 0;`（pr1335 分支实证）。按 BASELINE §3 规避决策不重复修 | INFO 探针实证返回 TEACHING_REFUSAL；gh pr diff 1335 实证 |

本车道新增/扩展的 20 个测试目标（6 新文件 + 2 既有扩展 + 12 个 harness 基线目标）**全部通过**，各目标实测：adversarial_matrix 88 断言/16 例、verifier_robustness_r4 41/10、context_ledger_completeness_r4 122/14、grounding_evidence_r4 全绿、runloop_recovery_r4 122/9、harness_boundaries_r4 210/4、llm_streaming_client 55/15、tool_call_dispatcher 226/27。

### C2 红跑证据（TDD 节拍）

- WP-A 计划层：type-confusion（kind/schema_version 为数组/对象时 readAgentPlan 抛 `Json::LogicError`）、duplicate output names 无探针、5000 步无上限——三条红均由对抗用例先红后绿（提交 2ca1c6663）。
- WP-B：verifyArtifact 对 classValues 含字符串/对象成员抛 `Value is not convertible to double`（红）→ isNumeric 守卫（绿）；expectedExtent 同类问题在复审中发现并同法修复。
- 语料可用性红：adversarialCorpusPath 的 `__FILE__` 语义错误使语料从未加载（复审 P1-7 实证）→ 修复后两目标真实复用。
- 教师令牌红：#1335 修复前的既有失败（见 C1 表）。

### C3 环境事实（复现用）

cmake=/home/kevin/toolchain/cmake-dist/bin/cmake；ninja=/home/kevin/pwb-sdks/root/usr/bin/ninja；Qt/GDAL 等在 $PWB_SDKS/root/usr；运行时需 LD_LIBRARY_PATH=$PWB_SDKS/root/usr/lib（libodbc 等不在默认路径）。构建纪律：-j2、CTEST_PARALLEL_LEVEL=1。本环境每次 cmake 重配置会刷新 sicnu_feature_probes.h 的 mtime 使 ninja 判脏 ~2600 边（deps 数据库按 mtime），故 CMakeLists 一次性定稿后不再 configure。

## D. 与任务书前提的偏离台账

| 任务书前提 | 实测 | 处置 | 依据 |
|---|---|---|---|
| run_loop 消费 LLM 产出、五条防线图 | run_loop 仅注册 diagnose 工具 | WP 重锚定 | BASELINE §6.1/§8 |
| ContextLedger 每工具调用一行 | 有界事件存储 | WP-C 收窄为真实合同钉测 | BASELINE §6.3 |
| #1334 与本轨道大重叠（~30 文件） | harness 重叠 0 | 无需规避 | BASELINE §3 |
| 4 份评审材料存在 | 根目录不存在 | 以 review/ 目录 + PR 描述替代 | BASELINE §4 |
| WP-D 修 grounding_probes 聚合 | #1337 在途同文件（且已修 A6） | 纯测试侧 + 收窄 | BASELINE §3、§C2 |
| ledger 每轮 tokens 记账、累计 ≥2 亿 | 无法自测精确值；subagent 精确+主线估算 | 如实记录、不为凑数注水（DECISIONS D5） | PLAN 末节 |
