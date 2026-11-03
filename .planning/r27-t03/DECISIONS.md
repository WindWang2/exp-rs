# R27-T3 DECISIONS（无人值守决策记录）

| 决策 | 选项 | 选择与理由 |
|---|---|---|
| D1 审批 mint 通道 | (a) MCP/CLI 保留 mint+HMAC (b) 仅 dock 本地 UI (c) CLI 命令行算"操作者通道" | **(b)**。目标文件 Oracle 明确"通过本地可信 UI 的审批才可成功"；CLI 二进制任何进程可执行，模型有 shell 时可自签。(a)/(c) 均留自签面。surface 保留 action 名 → typed 拒绝（非 UNKNOWN_ACTION），CLI/MCP 错误可读。 |
| D2 HMAC 实现 | (a) Qt QMessageAuthenticationCode (b) 本地纯 C++ HMAC-SHA256 | **(b)**。agent_ops 源文件事实上 Qt-free（CMake 虽链 QtCore），审批加密路径不加 Qt include；实现 ~90 行 FIPS 198-1，与 python3 hmac 独立交叉验证一致；repair_planner 是他轨叶子不动。 |
| D3 TTL 超限处理 | (a) clamp (b) 拒绝 | **(b)**。Oracle 明文"超大 TTL 拒绝"；clamp 会静默改写操作者意图。上限 30min（人工审批延迟上界），默认 5min。 |
| D4 consumed 环 | (a) 保持 16 条 FIFO (b) 过期逐出+满员 fail-closed | **(b)**。原环+无界 TTL=重放窗；现仅过期条目出环，未过期满员时 arm 拒绝（APPROVAL_REPLAY_RING_FULL）；consume 点（已获人工授权）溢出时才丢最老并注释说明。 |
| D5 dock 串行化 | (a) GUI 线程同步 join 旧任务 (b) 持久单 executor + 单深队列 | **(b)**。(a) 长任务阻塞 GUI 违反 Oracle"不阻塞 GUI"；(b) 结构上杜绝任务重叠（#1742 根因），mWorkerRunning 在锁内清零+notify 避免 joinWorker 丢唤醒死锁。 |
| D6 copilot 写审批 | (a) 每调用模态框 (b) 包装单步 plan 走既有审批卡 | **(b)**。复用已验证的人工 Confirm 卡与 role:"tool" 回路；门建于 src/agent/**（dispatcher 在 src/processing/** 非本轨白名单）。 |
| D7 #1798 定位 | 复核发现 base 的 validatePipelineJson 已挡 issue 三例（非 terminate）；真缺口=退出码 1→6 与零 catch | 加 invalidInput 位 + runFromJson try/catch 边界 + main_cli 退出码映射；实测以构建后 CLI 二进制定案 |
| D8 SPP-9/10/12/13 与 AS-016 | 本轨取舍 | SPP-10（签名体系）/SPP-12（params_file 语义）/SPP-13（runFile CRT）改的是跨进程契约或需独立设计，超出"现有闭环修复"最小面，记录不动；SPP-9 做 size 校验中的 header 一致性（若 C++ 侧在白名单内确认后补）；AS-016 symlink TOCTOU 需改各 surface 打开点（O_NOFOLLOW/openat），深改面大，记为已知残余并写入 RISK_REGISTER |
| D9 tests/CMakeLists 撕裂 | (a) 只报 blocker (b) 从各轨原始 commit 重建 | **(b)**。#1715 有先例；多重集证明 master 零丢失；不修则全轨无法构建。仅追加自己块，PR 说明中单列。 |
