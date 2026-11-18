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

**CTest（build-dev, Debug, -j1，双 resource_guard）**

- 新增 4 target 18/18 case 全过：`r33_t05_paging::` 5、`r33_t05_journal::` 6、`r33_t05_sidecar::` 4、`r33_t05_redact::` 3（含 R1 修复后的耗尽态往返复跑）。
- **终轮合并运行 93/93 全过（100%，0 Not Run）**，覆盖：受影响既有套件（agent_loop_core journal 11、journal_durability 4、r30_t05_journal_read_bound 3、agent_loop_resume 4、mission_scale_benchmark 6）、批次2 构建的 `test_agent_ops_core`（含 "secrets redacted; fault markers preserved; trace budget"、delivery 成功路径 0.9 置信、unified report 门等合同 case）、`test_mission_context` / `test_mission_runtime_store` / `test_mission_stage` / `test_mission_surface_parity`、以及新增 4 target 全部 case。

**独立小闭包运行（g++ 直链，R1 审查者亦自行复跑）**

- `test_r33_t05_mission_paging` 5/2616、`test_r33_t05_journal_bounds` 6/59（含 R1 修复后的耗尽态往返）、`test_r33_t05_redaction_delivery` 3/40、`test_r33_t05_sidecar_recovery` 4/65、`test_mission_scale_benchmark` 6/33054、`test_agent_loop_core` 17/216、`test_agent_loop_journal_durability` 4/71、`test_agent_loop_resume` 6/88。

**独立 review**：R1（独立子 agent，全量 diff+调用方走查+动态执行）= APPROVE_WITH_FIXES（无 P0/P1）；P3-3/4/5 已修并复测，P2-1/P2-2 如实记录。R2 复审绑定最终 HEAD（回填结论）。

## 限制

- Qt 6.12.0 本机环境下官方 `QAbstractItemModelTester` 被版本门编译出局（该版本 tester 的 changeInFlight 状态机与其自身 rowsInserted 内 fetchMore 探测冲突，最小契约正确探针可复现）；行契约由手写 RowContractWatcher 逐信号钉住。Qt<6.12（如 CI 的 Qt 6.8）下 tester 启用腿：NOT_EXECUTED（本机无该版本；不等待线上 CI）。
- Windows/UTF-16 腿、可见桌面（非 offscreen）腿：NOT_EXECUTED（本机 Linux；#2139 已覆盖 Windows UTF 路径基线）。
- GUI 生产路径的任务增长当前仍走 setTimeline 全量 reset（main_window_workbench 的 incremental 判定要求任务数不变）；appendNewTasks 修复为模型 API 契约+防御纵深（R1 P2-2 记录）。
- 同 missionId 同任务数的"替换"文档检测留待 WP03。

base `1dfb04382e` ← head（见 PR）
