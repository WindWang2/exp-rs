# FINAL_REPORT — R27-T2 数据资产、遥感 I/O 与分块/模型执行运行时：可靠性和可恢复性

- **BASE_SHA**: `10652fd003c66ae78598d2b878eed9beb50029fa`（=origin/master，2026-11-02T13:23Z fetch；PR 前复 fetch 确认未漂移）
- **HEAD**: 见 PR（分支 `agent/r27-t02-data-io-runtime-reliability-20261102T132440Z`）
- **Worktree**: `../exp-rs-r27-t02-20261102T132440Z`（本轨唯一；master 零直接提交）

## 交付总览（10 commits）

1. `cc7aaf69e0` **修复 master 阻塞**：tests/CMakeLists.txt 自 #1727 起 5 次冲突解决交错破坏（configure 必败）。以 #1726 为基座按 PR 序重放五轨 authored 块；1102 测试目标集合与 master 完全一致；找回被吞的 36 行 authored 内容。
2. `f50ca34527` **#1743 (P1)** addJoin 重复输入 fail-closed + RIO-3 空_join 运行时拒绝 + 8 用例反例矩阵（**RED 已证明**：master 4/4 失败 → fix 8/8 过）。
3. `800ae9f3db` **#1766 (P2)** ResumableTileRun 状态路径单拥有者（Qt-free OS 锁 + ChunkRunLockHeld fail-closed + 真实子进程持锁崩溃 → 确定性恢复）。
4. `0133646520` **#1767 (P2)** JobEngine 有界 shutdown（detach + immortal 单例 + deadline 等待 + 放弃路径）+ 可选 per-job timeoutMs watchdog + RIO-9/RIO-10。
5. `6a11371df1` **#1764 (P2)** 租约表有界（4 个释放点 + trackedCount/trackedLeases 可观测 + 500 代反例 + 真实 pool oracle）。
6. `d7e838dc44` **#1765 (P2)** safeRemoteRasterPath 安全打开权威（scheme→offline→私网→缓存安装→**受控姿态**五重 fail-closed 链）+ RIO-6 acquire 接线。GUI 侧按条款归 T4。
7. `6482857b16` **#1803 P3** RIO-3/6/7/11/12/13/14（WHERE 守卫、audit 单权威+retention、CRS no-net/no-file、mirror 双信号防降级、页重复即停）。
8. `41c55afad9` **D** ImportSession 四状态 + `sicnu.import.session/1` 机器可读摘要（复用 ImportPreflight/DataManager，无第二身份系统）；**E** 尾批像元守恒/注入故障/取消恢复 oracle。
9. `a5d60ee6c5` **R1 闭环**（11 findings：含锁父目录生产缺陷、mirror chunkKey 逐条目信号补强）。
10. `43696f6ae1` **R2 闭环**（13 findings：GDAL 3.8 lane 守卫、GuardUnavailable 姿态门、r12b 回归修复、v1 兼容/retention/拒绝消息 oracle、chunked_run 成功路径防误伤、契约措辞诚实化）。

## Oracle 达成（对照 ORACLES.md）

| Oracle | 状态 | 证据 |
|---|---|---|
| O1 addJoin typed 拒绝 | ✅ | RED 证明 + 4 用例 |
| O2 合法 join/取消/背压 | ✅ | 4 用例 + 既有 19 断言 |
| O3 双执行者 fail-closed + 崩溃恢复 | ✅ | 双线程 + 真实子进程 _Exit 用例 |
| O4 shutdown 有界 | ✅ | stuck worker <5s（250ms 注入 deadline）+ 恢复后可复用 |
| O5 晚到取消/终态进度 | ✅ | Succeeded+结果+日志注明 / RIO-9 门 |
| O6 租约有界 | ✅ | tracker 500 代 + 真实 pool 4 代 + shutdown 后 ==0 |
| O7 SSRF/离线/私网/姿态 | ✅ | 11 私网拼写 + offline + 无缓存 + 无姿态 typed 拒绝；合法 https 映射受控路径 |
| O8 acquire 受控 | ✅ | 私网/元数据/无缓存拒绝 |
| O9 P3 各项 | ✅ | WHERE 拒绝集 + audit 单权威/retention + CRS 消息级 oracle + mirror 双向（防降级与 v1 兼容）+ RIO-14 判定 |
| O10 四状态导入 | ✅ | check→proceed→cancel→resume 幂等（journal path-keyed + catalog 重验） |
| O11 尾批/故障注入 | ✅ | 像元守恒+字节级；恰 1 块重算；取消恢复零重算 |
| O12 双跑 | ✅ | 39/39 ×2 + 全回归 ×2 |

## 评审

R1（并发/资源生命周期）11 findings 全修；R2（地理空间/安全 I/O/兼容性）13 findings 全修；两轮均独立 subagent、逐行覆盖声明落盘 REVIEW_R1/R2.md。

## 局限与如实声明

- **未测平台**：Windows（LockFileEx/GDAL lane）仅编译目标存在，未运行（NOT_EXECUTED）。
- **RIO-12 残余**：~13 处跨轨 SetFromUserInput 站点未收敛（白名单外），helper 已公开供各轨采用。
- **#1765 GUI 侧**：dialog 改线专归 T4（跨轨条款）；本轨交付其后端权威 API 与 acquire 接线，并如实披露 GUI main 未 engage 策略的现状（T4/T5 决策面）。
- **性能矩阵**：256/512/1024 吞吐、GPU fallback 峰值未做（非本轨 Oracle；BENCH_BASELINE.md 声明）。
- 全部测试本地执行；未等待远程 CI。
