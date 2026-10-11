# R8 Goal Loop Ledger — r8-agentops-workflow-execution

格式: 第N轮 | 改动 | 验证结果 | 通过/未通过 | 下一步

## S0 状态
- BASE_SHA=47c6371f3 (origin/master, 2026-10-11 fetch 实测)
- PR #1469 open (head c8c463d22, MERGEABLE, 单提交), 1 unresolved P1 review thread
  (mcp_server.cpp:2663 recording-after-dispatch), 5 P2 threads 均非 R8 文件。
- Issues #1447–#1468 open; R8 相关: #1451 #1452 #1454 #1459 #1463 #1448。
- #1469 已修（R8 域内）: mid-run cancel 传播、MULTI_STEP fail-closed、rubberBand
  deleteLater、llm 流式 re-entrancy、bounded checkpoint parse、terminal-save retention、
  armRepairApproval 真时钟、cancel-vs-submit race finalization。
- 本轨剩余 cluster: A(recording P1) B(#1454 P2×4) C(#1459 P2×5) D(#1463 P2×3)
  E(#1452 mcp_server P2×4)。
- Worktree ../exp-rs-r8-agentops branch hardening/r8-agentops-workflow-closure @47c6371f3。

## 轮次记录

第0轮 | S0 状态快照 + worktree 创建 + ORACLE/OWNERSHIP/LEDGER 建立 | git fetch/pr list/issue list
实测通过；worktree HEAD=47c6371f3 | 通过 | 配置构建 + Cluster A 审计

第1轮 | 构建: configure 成功(build-r8, Ninja/MSVC/Qt6.8, 153s); 基线 test_mcp_server
目标后台构建(3600 objects)。审计 Cluster A: master enable-before-dispatch(#1448 孤儿行)
vs #1469 enable-after-dispatch(review P1 未记录 workflow 继续跑) + recordSubmission 失败
同型洞 | 设计定型: prepare(无副作用验证)→dispatch→enable(commit)+recordSubmission,
任一失败→cancelRun 补偿+typed refusal | 通过(设计) | 实现 Cluster A

第2轮 | Cluster A 实现: WorkflowExperimentMonitor::prepare() 新增(空参/rebind/可写探测,
零副作用); mcp_server.cpp handleRunWorkflow 重构为握手三段+refuseRecordingAndRollback
补偿 lambda; 新 TEST_CASE 3 sections(拒绝不留痕/commit 失败回滚到 durable Canceled/
prepare 拒绝不 dispatch), GatedNoopOperator 门控算子 | 待构建验证 | 实现 Cluster E

第3轮 | Cluster E: insertBoundedKeepingNewest 模板(mcp_server.h)用于 cancel-map/progress-map
驱逐; EOF 分页测试钉住(master 实测语义已正确→该子项按 CLOSED_BY_EXISTING 处理待测试
证实); interaction_tool_registry list_layers O(N*M)→QSet 索引(mcp_server 的同名已修);
McpServer dtor terminate()→detach+setParent(nullptr) | 待构建验证 | 实现 Cluster B

第4轮 | Cluster B: output_committer copy-temp 清理移至注册成功后(#1454 契约); task_center
logBuffer 19 处 append 全部走 2000 行 ring(批量裁剪, elided 标记); local_worker_pool
teardown/retire 的 QProcess 亲和门(仅 owner 线程发 graceful shutdown, foreign 直接 kill
ladder); main_window dtor 先 setCatalog(nullptr)+setWorkspaceCatalog(nullptr) 再 reset
ProjectContext(B3 宿主路径; 残余= DataManager 所有权需跨轨改造, 记录为 documented
residual) | 待构建验证 | 实现 Cluster C/D

第5轮 | Cluster C: workflow_runtime PlaneSubmitRejected typed fallback(替代消息子串)+
optional nullopt 分支独立; run_coordinator ghost unlink 移出 m_mutex; scratch_registry
seal/verify 改 1MiB 流式 FNV(fnv1aContinue, 与整文件参考实现差分测试); checkpoint
archive 改 publish-then-swap(.old 备份/回滚)。Cluster D: journal 驱逐保护最新
goal_normalization(last-write-wins); bench containment 词法归一化(../、\\、Win大小写);
recipe scan 改 error_code increment 循环 | 待构建验证 | 写回归测试

第6轮 | 回归测试全部落位: test_mcp_server(+3 cases), test_agent_loop_core(journal pin),
test_agentbench_trace(containment 8 断言), test_fault_injection(archive swap),
test_external_memory_10(2MiB 流式差分), test_task_center(log ring)。修复 python 写入
造成的 3 处转义损伤(NUL/单反斜杠/字面换行) — 全文件 NUL 扫描清零 | 待构建+双跑 |
构建完成后跑受影响测试 ×2

第7轮 | 独立对抗 review(读-only subagent, 52 tool calls): 22 findings。MUST-BLOCK:
F1(P1) enable 后置丢失快速 pipeline 的终态转换→实验记录卡 Running(#1469 有 catch-up
而我漏了); F21 #1469 语义合并必须 prepare+compensation+catch-up 三合一。已修:
F1 terminal catch-up(recordAggregateState, 失败也走补偿); F2 补偿消息按 rolledBack
分支如实; F3 cancelRun false→TaskCenter::cancelPipeline 兜底; F17 archive .old 残留
加 history 扫除; F19(i) 退化 root(".") 走 legacy 前缀判定避免翻转; F14 注释纠偏
(原代码短路保护无 UB); F12 main_window 多窗口 caveat 注释。FP 确认: prepare 零副作用/
QSet 等价/detach 顺序/ring 无新锁/流式 digest 兼容/pin 终止性。 | 通过(修复完成) |
增量构建+测试双跑

## review 处置对照(第7轮)
- F1 P1 → 已修(catch-up + 补偿)
- F21 → 设计记录: 本轨=mcp_server recording 块的超集(= #1469 修复 + prepare + 补偿),
  PR body 明示合并语义; rebase 若 #1469 先进则取本轨块
- F2/F3/F17/F19i/F14/F12 P2 → 已修
- F5 EOF 测试定位为 contract pin(master 已正确), PR 措辞修正
- 其余 FP 确认无需改动

## 待验证清单(构建完成后)
1. test_mcp_server: 新增 3 cases + EOF + bounded-map + 全量回归
2. test_agent_loop_core / test_agentbench_trace / test_fault_injection /
   test_external_memory_10 / test_task_center / test_output_committer
3. 双跑关键(新)测试; 记录命令与 rc
## 已知残留
- B3: 并发 submit 已越过指针读取窗口的竞态需 DataManager 所有权(跨轨), 已加宿主 dtor
  顺序修复, 剩余窗口记录于 PR
- recipe scan race / committer 注册失败分支: 结构性修复, 无确定性公共 API 注入面,
  以代码审查证据支撑(测试文件注明)
- E2 offset==sizeBytes: master 语义疑似已正确(truncated=false 无 next_offset), 测试钉住
