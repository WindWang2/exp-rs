# REVIEW_LOG — Track 8 R4 独立对抗性评审

评审方式：1 个独立 subagent（只读、带证据挑错），对 `git diff origin/master...HEAD` 全量审查。两个轴：Standards（C++20/Qt6、异常安全、正确性）与 Spec（是否真修、是否同义反复、白名单、交付足量性）。评审报告 18 项发现（8×P1、10×P2），全部处置如下。

## P1 处置（8/8 已修复并验证）

| # | 发现 | 位置 | 修复 | 提交 |
|---|---|---|---|---|
| 1 | `plan.inputs` 非字符串 `name` 使 `validateAgentPlan` 抛 `Json::LogicError`（复现：reader 接受 → validate 抛；工具面 harness:plan 抛出） | agent_plan.cpp:224 | 非字符串 name → 类型化 issue；非对象 input → 类型化 issue | 审查修复提交 |
| 2 | `pins.datasets` slot 成员判断 lambda 对非字符串 name 调 `asString()` 抛异常 | agent_plan.cpp:388 | lambda 加 `isString()` 守卫 | 同上 |
| 3 | `planFingerprint`/`planSummary` 对非 object step 调 `get()/isMember()` 抛异常；**harness:plan 在校验前调 planSummary**，工具面可被 `{"steps":["x"]}` 打抛 | agent_plan.cpp:508,584 | 两个循环加 `isObject()` 跳过；新增 3 个对抗用例 | 同上 |
| 4 | SSE 网络路径 `m_buffer` 无上限（8 MiB 检查在 `parseSseLine`，即**已缓冲之后**；实测 400 MiB 无换行行全量缓冲） | llm_streaming_client.cpp:140 | `onReadyRead` 在拼行前对未闭合尾部设限 → 类型化拒绝 + `cancel()` | 同上 |
| 5 | `m_toolCalls` index 映射无上限（100k 个 index → +22 MB 线性增长） | llm_streaming_client.cpp:233 | `kMaxToolCallIndices=64`，超出发 `too_many_tool_calls` 拒绝 | 同上 |
| 6 | "oversized arguments" 用例的 fixture 把含 `"` 的 chunk 直接插进 JSON 字符串字面量 → data: 行非法 JSON，从不累积，**该用例是红的** | test_llm_streaming_client.cpp:392 | fragment 改为正确转义的合法 JSON（`\\"pad\\":\\"…\\"`） | 同上 |
| 7 | `adversarialCorpusPath()` 把 `/../data` 拼在 **`__FILE__`（含文件名）** 之后 → POSIX 无法解析 → **语料从未被任何目标加载过，"双目标复用"实际是死的** | adversarial_corpus.h:121 | 从头部所在目录解析；语料样本的 partial-JSON 行转义层数也不对（线本身非法 JSON），改为构造式生成并验证 round-trip | 语料修复提交 |
| 8 | `errorEnvelope` 形状断言错误：实现返回 `{success:false, error:{…}}`，测试读顶层 `envelope["code"]` → 该 case 是红的 | test_harness_boundaries_r4.cpp:149 | 断言嵌套形状 `envelope["error"][…]`（与 harness_error.h:139-142 文档一致） | 审查修复提交 |

## P2 处置

| # | 发现 | 处置 |
|---|---|---|
| 9 | `expectedExtent` 非数值成员未防（`asDouble()` 抛）——WP-B 规格的"类型错位"只做了一半 | **已修**：四成员 `isNumeric()` 守卫 → 结构化失败 check + 新增用例（含数值对照） |
| 10 | `malformedToolCall` 无生产消费者（只有测试连接） | 记录为 backlog：信号面已就绪，agent_copilot_dock_widget 的消费属交互行为改动，超出"对抗加固"边界，PR 正文列明 |
| 11 | `REQUIRE` 在 `try{}catch(...)` 内被吞、误报为 throw | **已修**：断言移出 try（3 处） |
| 12 | 两个 parse-seam 用例只钉平台行为、不触生产码 | 接受并如实记录：它们是 WP-B"任何输入必须终止"的**平台前提**钉测（QJson 与 jsoncpp 对 100k 深的实测合同不同，都已钉住）；实现面由 WP-A SSE 用例覆盖 |
| 13 | "bounded work" 断言 60s 太松 | 接受：VRT 逻辑栅格用例的核心断言是"完成 + 有界采样网格"，时间上限只防挂起；已在用例注释说明 |
| 14 | `test_output_verifier` 的 WHOLE_ARCHIVE 修复未提交 | **已修**：随审查修复提交 |
| 15 | 非数值 class 域成员静默不匹配（把过错记在栅格上） | 部分接受：抛异常已消除（P0 级问题）；"域声明侧拒绝"需改 plan_tools 派生期合同，记入 backlog |
| 16 | PLAN.md WP-F 的"文档 ≤4MiB"只实现了一半 | **已修**：`kMaxPlanDocumentBytes=4MiB` 在 reader 内对序列化文档强制，新增用例 |
| 17 | 步数上限不在所有路径（`lowerIrToAgentPlan` 绕过 readAgentPlan） | 记录：该路径受 IR 自身 `kMaxNodes=64` 约束，非本轨道接缝；PR 正文 backlog |
| 18 | dispatcher 两新用例污染进程级注册表 | **已修**：末尾 `reset()` 收尾 |

## 白名单核查（评审独立确认）

`grounding_probes.cpp`、`lab_copilot.cpp`、`mcp_server.cpp`、`src/agent/CMakeLists.txt`、`src/workflow/`、`context_checkpoint.cpp` 均**未**被本分支触碰（`git diff origin/master...HEAD` 实证）。规避决策（BASELINE §3）成立。

## 失败矩阵 8 类可复现性（评审独立确认）

8 类全部有独立可复现样本与独立检测锚点：幻觉引用（matrix:100）、格式错误（:149）、拒答/空（:167 + streaming:204）、超长截断（streaming:296）、部分 JSON（streaming:331）、缺参（dispatcher:1358）、工具幻觉（dispatcher:1329）、自相矛盾（matrix:288 + 对照 :319）。语料 9 样本、被 2 个目标真实加载（修复 #7 后实测通过）。

## 复审后终检

- 审查修复后 5 个受影响目标全绿（adversarial_matrix 88/16、verifier_robustness 41/10、streaming 55/15、boundaries 210/4、dispatcher 226/27）。
- 车道 ctest 双跑一致：**98%（123/126）**；3 个失败 = 1 个纯 verifier 库目标未构建（与车道无关）+ 2 个教师令牌门禁（#1335 在修，`constantTimeEquals` 的 `return diff` 倒置缺陷已实证并定位到 #1335 的逐字修复）。
- 剩余已知项全部转 PR backlog 或 EVIDENCE，无未决 P1。
