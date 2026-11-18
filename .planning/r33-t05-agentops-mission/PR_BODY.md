# R33-T05: mission paging contract, journal seq/compaction evidence, bounded sidecar reads, fault redaction, honest delivery claims

## 原问题与影响（用户可观察）

1. **Mission 时间线分页下的行通知违反 Qt 模型契约**（`mission_timeline_model.cpp`）：分页可见 100/总数 1000 时追加一条任务，`beginInsertRows(1000,1000)` 宣告 1 行，`rowCount()` 却从 100 跳到 1001——视图看到的行数增长与宣告不符，且隐藏行 100–999 无通知变可见；`setPageSize` 提升后首次 `applyEvents` 也会无声扩大可见行数；`applyEvents` 收到收缩/异 mission 时间线时静默部分采纳（陈旧 id→行映射可错指任务）。
2. **Journal seq 空间边界 UB 与 next_seq 语义缺失**（`session_journal.cpp`）：`fromJson` 派生 `mNextSeq=previousSeq+1`（INT64_MAX 溢出 UB；完全忽略持久化 `next_seq`，逐出历史（maxEntries=0）后派生值回退→seq 复用）；`append()` 在 seq 耗尽时回绕为负。
3. **Journal 压缩丢失终态原因**：`compactProjection` 把 terminal 条目的 string stop reason 一并替换为 `{compacted,...}`——压缩保存→重开→replay 后"取消/中止"失去 typed 原因。
4. **Mission sidecar 读取无预算 + last-good 验证/复制两次打开**：authority/last-good/legacy timeline 三条读路径都是无界 `readAll()`；`rotateLastGood` 验证一次读、复制另一次开文件——验证的字节不一定是保存的字节（TOCTOU）。
5. **Trace 脱敏的 fault 旁路**：`secret_redactor` 对 `fault` 子树整棵 verbatim 复制——`fault.api_key` 等秘密槽与超长字符串绕过脱敏与预算。
6. **交付 claim 的信任门**：已提供但未知 schema 的 verifier report 被当作"缺席"，loop PASS 仍给 0.9 高置信 claim；外部自述 claims 原样透传，与平台验证 claim 无法区分。

## 已合并基线

master `1dfb04382e`（含 #2134/#2135/#2136/#2137/#2139）。

## 本次增量

- WP02 `MissionTimelineModel`：分页保持的增长语义（全可见→按 append 揭示并宣告精确范围；分页→新行落到可见前缀外经 `fetchMore` 可达；空模型首灌取首页）；可见前缀增长一律带 `begin/endInsertRows`；shrink/异 missionId → `setTimeline` 安全重建（含 id 索引重建）。
- WP05/06 `SessionJournal`：`append()` seq 耗尽拒绝（不回绕）；`fromJson` 采用并校验持久化 `next_seq`（必须严格大于最后 entry、非 INT64 顶、整数；极值文档 typed 拒绝）；`compactProjection` 保留 terminal string stop reason。
- WP08/09 Mission sidecar：新增共享有界读 `readMissionSidecarBytes`（stat-先行 + 64KiB 分块、上限=sidecar 写权威 `kDefaultMaxSidecarBytes` 32MiB；增长中文件也止于上限），三条读路径全部切换；`rotateLastGood` 与验证绑定同一读取字节（消除 A→B 竞争），毒化状态拒绝保存保持 last-good 原样。
- WP11 `secret_redactor`：仅"string 且非 secret 形状且 ≤预算"的 fault marker 精确保留（评分合同：evaluator 只认 string faultKind）；object/array/异常 string fault 递归走脱敏+预算。
- WP13 `delivery_assembler`：未知 schema 的已提供 report → `unified.verdict=FAIL` + `reason=unsupported_report_schema` + confidence 0.0（空/null 仍为"未提供"，legacy 门不变）；外部 claims 标 `source:"external"`（自带 source 不覆写）、平台 claim 标 `source:"platform"`。

## 边界与接口兼容

- 不改 `operations_coordinator.cpp`/`decision_record.cpp`（#2134 领域）；TS 未触碰（无新增用户可见文案）。
- `readMissionSidecarBytes` 为新增导出（mission_context_store.h）；既有错误消息形状保持（"sidecar missing/cannot open/JSON parse error"）。
- Journal 文档合同收紧：手工构造 `next_seq<=lastSeq` 或非整数的文档现在被拒绝（`toJson` 产物天然满足）；R30-T05 read_bound fixture 已同步修正为合法形状。

## 验证（命令与真实计数）

（CTest 数字待构建完成后回填；下为独立闭包运行动态证据）

- `test_r33_t05_mission_paging`：5 cases / **2616 assertions 全过**（含逐信号行契约监视器；Qt 6.12.0 的 QAbstractItemModelTester 与 fetchMore-插行模型不兼容——tester 在 rowsInserted 派发内同步探测 fetchMore 且 changeInFlight 拒绝嵌套 begin，已用最小契约正确探针复现，故 tester 以 QT_VERSION<6.12.0 门控附加）。
- `test_r33_t05_journal_bounds`：6 cases / **51 assertions 全过**（含磁盘 save(>1MiB)→压缩→load→replay 的 stopReason 保留）。
- `test_r33_t05_redaction_delivery`：3 cases / **40 assertions 全过**。
- 既有套件独立运行：`test_mission_scale_benchmark` 6/33054、`test_r30_t05_journal_read_bound` 3/13、`test_agent_loop_core` 17/216、`test_agent_loop_journal_durability` 4/71、`test_agent_loop_resume` 6/88 全过。
- （回填区：CTest test_r33_t05_sidecar_recovery / test_mission_runtime_store / test_mission_context / test_agent_ops_core / …）

## 限制

- WP08/09 的 sidecar 测试经 CTest 全量构建验证（进行中）。
- Windows/UTF-16 腿、可见桌面（非 offscreen）腿：NOT_EXECUTED（本机 Linux；#2139 已覆盖 Windows UTF 路径基线）。
- 同 missionId 同任务数的"替换"文档检测留待 WP03（权威 append-only 下不可达；shell 层 revision 仲裁待做）。

base `1dfb04382e` ← head（见 PR）
