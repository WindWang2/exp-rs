# R19-02 TASKS — 证据驱动工单（全部锚定源码符号与可执行验证）

基线 `e12a9afbc`。每项含：path → upstream PR → blob SHA（提交时更新为 HEAD SHA）→ reproducer → owner。
状态机：`VERIFIED-BROKEN`(RED 已复现) → `FIXED`(GREEN×2) → `REVIEW-ONLY` / `DEBT`(记录不动) / `ALREADY-FIXED`(当前 master 验证)。

## A. 已验证 ALREADY-FIXED（当前 master 复核，无需改动；防止回归由既有测试守护）
| issue | 符号/位置 | 验证方式 |
|---|---|---|
| #1549 | science_context/asset_state_provider.cpp resolve (strip 移至出站投影) | 源码复核 :235-258 |
| #1551 | plugins/host/plugin_host_worker_main.cpp services isObject→E1002 | 源码复核 :1086-1096 |
| #1552 | agent_loop/session_journal.cpp stackLimit=128+catch | 源码复核 :320-335 |
| #1588 | preflight/runtime_adapter.cpp 多集合截断计数 | 源码复核 :105-185 |
| #1589 | agent/data_platform_tools.cpp notifyProjectSwitch 接线 :1462 | 源码复核 |
| #1592 | plugins worker params 类型前置检查 | grep 无裸 get().asString() |
| #1594 | workflow/placeholder_grammar.cpp 单遍不重扫 | 源码复核 :232-252 |
| #1606 | contracts fromCanonicalJson outputs 走 paramFromJson | 源码复核 :261-266 |
| #1616 | explain numberToString isfinite+范围门 | 源码复核 :38-48 |
| #1570 | spectral_spatial_tools.cpp 移除 required layer_id | 源码复核 :169 |
| #1571 | commands/workspace_commands.cpp 回调出锁+部分回滚不入 redo | 源码复核 :85-111 |
| #1572 | llm_streaming_client 错误态先取再 deleteLater | 源码复核 :437-446 |
| #1573 | llm_streaming_client onReplyError 代际护栏 | 源码复核 :493+ |
| #1574 | agent_ops/ops_driver.cpp ec 重载 | 源码复核 :79-85,261 |
| #1628-1 | mcp_server StdinReader 干净 EOF(gcount)区分超长 | 源码复核 :422-468 |
| #1628-2 | mcp_server validateWorkspacePaths 自由文本槽豁免 | 源码复核 :164-203 |
| #1628-5 | data_platform_tools using 收敛为 1 份 | grep -c |
| #1629-2 | agent_ops actions 入公布表 | session_surface.cpp:52 |
| #1630-1 | agentbench validateReplay 越权步先计 usage 再 continue | 源码复核 :576-598 |
| #1634(全部5) | contract_graph 词表/census matchIsQuoted/graph_assembly notes/注释去重/error_code_scanner insideLiteralOrComment | 源码复核 |
| #1643 | planner capability_live.h 单一声明 | grep -c =1 |
| #1644 | plugins mLastKnownGroup 代替 mProcessGroupId 竞态读 | 源码复核 :695-730 |
| #1645 | preflight rules.cpp daysInMonth 门 | 源码复核 :250 |
| #1649 | science_context recipe_router 空 needle 守卫 | 源码复核 :288-298 |
| #1650-1/2 | sdk pipe fd 清理 / ipc cancelPredicate catch+erase | 源码复核 |
| #1654(部分) | workflow_runtime runStep 开头消费陈旧取消 :235-238 | 源码复核（残余竞态见 T1） |
| r18-c 残留: pi rpc-id 跨代重用 | pi/mcp_bridge.ts nextId 单调不重置,实例跨代存活 | 源码复核 :80,:432 — 非问题 |

## B. 本轨工单

### T0 ★核心★ [P1][B04/B13] 恢复 5e14bd1d7b 静默回滚的 #1518/#1505-P1 修复（merge_loss_audit 发现）
- path: `src/science_context/{broker.h,broker.cpp,agent_adapter.cpp}` + `tests/test_science_context_{broker,concurrency,live_authorities}.cpp`
- upstream: #1518 (a3465fc53) 被 merge 5e14bd1d7b 回滚（2026-10-18，r16-c 轨合并事故）；丢失后零提交，恢复无碰撞；owner: 本轨
- 机理: `capabilityFacts()` 交出 `&*mCapFacts`（optional 存储裸指针）；并发 `setCapabilityFacts()` move-assign 后调用方（agent_adapter scientificCapabilities，跨锁持有）读错 authority/悬空。修复=shared_ptr 快照所有权。
- reproducer: 恢复 #1518 测试（已入工作树，253 行增量）→ 在未修 src 上运行 `ctest -R 'test_science_context_(broker|concurrency|live_authorities)::'` → snapshot 测试行为 RED；随后 verbatim 恢复 src（git checkout 9c0dd4cb9 -- …）→ GREEN×2。

### T1 [P2][B06/B09] workflow 取消窗口：requestCancel 与 runStep 消费无全序
- path: `src/workflow/workflow_runtime.cpp`（runStep :235-238 consume / requestCancel :800-805 store(true) 锁外）
- upstream: #1654 剩余"理论竞态窗"（任务书点名种子）；owner: 本轨
- 机理：`requestCancel` 的 `flag->store(true)` 与 runStep 入口的 `staleFlag->store(false)` 均在 `m_mutex` 外，二者无全序。交错 A（丢失取消）：store(true) 落在 store(false) 之前完成→新 step 吞掉取消，且此时 m_activeTaskIds 尚无新 task→TaskCenter cancel 也落空；交错 B（跨步误伤）：store(true) 落后于 consume→瞄准上一步的取消命中新 step。
- reproducer: 新增确定性测试（fence/fatch，不用 sleep）：test_r19_02_workflow_cancel_order.cpp — (a) 取消先于 runStep 完成被消费；(b) step 执行中取消必达（watcher/同步路径）；(c) 全序性质：并发 requestCancel+runStep 终态二择一且不"丢失已发出取消"（用门/gate operator 强制交错点）。
- fix: requestCancel 的 store(true) 移入 `m_mutex`，runStep 的 consume 移入 `m_mutex`（同把锁建立全序；cancel 在 step 开始前→消费语义，之后→由 cancelWatcher/同步轮询兑现）。

### T2 [P2][B10] explain 死成员 guidance_ 清理（任务书点名）
- path: `src/explain/explanation_validator.{h,cpp}`（h:54 声明、cpp:47 仅初始化零读取）、调用方 `src/agent/spatial_tools/explain_step_tool.cpp:371`
- upstream: #1636（任务书明示"未彻底修"）；owner: 本轨
- reproducer: `grep -n 'guidance_' src/explain/*` 仅命中声明+初始化（静态 RED）；移除后编译+既有 explain 测试仍绿。
- fix: 删除成员与构造参数，更新唯一调用方；不引入新抽象。

### T3 [P1][B13] merge-loss 不变量检测器（29596c913 类回滚探测）
- path: 新增 `scripts/dev/merge_loss_audit.py`（append-only 新文件，不动共享文件）+ `tests/test_r19_02_merge_loss_audit.py`（或并入既有测试机制——按 tests/ 现有 python 测试惯例）
- upstream: r18-c FINAL_REPORT 维护建议 + 任务书 B13；owner: 本轨
- 机理: 29596c913 型事故=合并提交对某文件的解等于 parent-1 blob 而该文件仅 parent-2 侧改过（或反之）→静默回滚。检测器遍历 `git log --merges`，对每个合并的冲突文件集比对 blob 归属，输出"独占修改侧的内容在合并结果中消失"的文件清单。
- reproducer: 构造合成 repo fixture（临时目录内 init/commit/branch/merge -X ours），断言检测器报告已知回滚；对真实仓库 29596c913 运行应报告 6 个文件（r18-C 已证 blob 考古结论）。RED=脚本未存在；GREEN=fixture+真实历史两方向断言通过×2。

### T4 [P3][B03] workspace_snapshot dataMin/dataMax 死分支 — REVIEW-ONLY（不改）
- 注释已诚实（"No producer..."）；分支保留是可接受的自描述防御式渲染。避免无收益 GUI 邻接 churn。DEBT 记录。

### T6 [P2][B03/B11] pi knowledge temporal 页超预算（真实 RED 已实测）
- path: `data/processing/algorithm_meta/capability/rs-temporal-*.json`（16 文件 24 行，转义原位替换）+ `pi/knowledge/capability-temporal.md`（gen-pages 重生成）
- RED 实测：`node --test pi/test/` → 39 中 1 败（capability-temporal.md 33384 > 32768）。
- fix: 两轮文案精简 −947B decoded（保留全部独有语义；去除与摘要逐字重复的 limitation 条目）；`capability_knowledge_tool gen-pages` 重生成 → 预算门 GREEN + committed==derived 漂移门 GREEN。

### T5 [P2][B03] tool surface 快照一致性验证（只读检查）
- 运行既有 `pi/test/surface_snapshot.test.mjs`、`no_drift.test.mjs`、`tests/test_surface_parity*` 并记录；只有发现真实漂移才开新工单。

## C. DEBT（记录，不修——避免投机性 churn）
- #1628-3 temporal phenology 工具未接线（D-160-1 声明的后续集成，接面属扩展非修复）
- #1629-1 run/resume 启动重复（Shotgun Surgery 重构风险>收益）
- #1629-3 checkpoint sink 注释依据（需核对当前注释是否已改写；若仍矛盾做纯注释修正）
- #1630-2 agentbench EnumTable 四份拷贝（纯重构）
- RateLimiter 反向抑制（无反向进度生产者；行为记录于 RISK_REGISTER）
- get_tool_schema 单工具 schema 投影无缓存（单调用 ~几十节点；无实测瓶颈，B11 记账）

## D. 基线义务
- 全量构建（进行中）→ 本轨相关测试族 GREEN 基线 ×2（mcp/workflow/agent/agent_ops/agent_loop/sdk/plugins/preflight/science_context/contracts/explain/agentbench/surface）+ pi Node 测试套 ×2
- TEST_RESULTS.md 记录环境/退出码/NOT_EXECUTED 项
