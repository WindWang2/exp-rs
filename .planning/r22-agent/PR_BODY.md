# R22-04 (r22-agent): Pi 空间 Agent/MCP 沙箱逃逸、env 注入、IPC/插件生命周期与 Job 日志有界化 — 双轮独立评审闭环

**Base**: `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（push 前经 `gh api repos/WindWang2/exp-rs/commits/master` 复核远端未前进）
**Head**: `agent/r22-agent-20261025T164309Z`（独立 worktree，13 commits）
**Fixes**: #1704 (P1) · #1690 (P1) · #1691 (P2) · #1692 (P2) · #1705 (P2) · #1693 (P3)

## 改动清单（src 12 文件 + tests 6 文件 + .planning 证据）

| Issue | 缺陷 → 修复 |
|---|---|
| #1704 | GDAL 驱动前缀连接串（CSV:/GPKG:/WMS:/PG:/OCI:/TileDB:s3://…）曾被当作工作区相对路径放行，GDAL 按前缀打开真实工作区外目标（沙箱逃逸 + 远程默认拒绕过）→ classifyReference 新增驱动前缀分类：文件驱动提取内嵌路径（引号/盘符/layer 分隔/嵌套 scheme）按同一策略递归 containment；网络驱动（含真实短名 OCI/EEDA/AmigoCloud/GeoRaster，R2-F1）走 SICNU_MCP_ALLOW_REMOTE 默认拒；未知驱动危险 payload 拒绝；嵌套前缀递归 + 8 层深度上限（R2-F2 防栈溢出） |
| #1690 | env key `"LD_PRELOAD=/x.so "` 绕过整 key 精确比较，envp 拼接后 ld.so 按首个 '=' 切分实现子进程代码注入（issue 内实锤 PWNED 复现）→ run 前（POSIX/Win 两平台）拒绝空/含'='/NUL key（E5007）+ manifest 解析期 env key 字符集门 |
| #1691 | 插件加载超时失败路径不撤销已注册 py: 算法（幽灵目录条目）→ 失败路径显式撤销（对齐 #755）+ ~AppInterfaceBridge 幂等兜底 |
| #1692 | PythonIpcServer 静默丢弃 >1MiB 响应行 → 调用方挂满 300s；>4MiB 触发崩溃重放 → 宿主按响应形状锚定（R1-F4）抢救 id 立即回 -32001 typed error + dropInFlight；daemon 发送侧 >1MiB 回 typed error（阈值两侧一致无错窗） |
| #1705 | JobEngine logLines 无界增长（issue 复现 50000 行全保留）+ snapshot/终态持锁全量复制 → 单行 4KiB UTF-8 截断、>10k 行块驱逐至 5k（droppedLogLines 精确记账）、delta 通知改绝对索引空间（消费端 exactly-once 跨驱逐成立）、终态通知走同一 delta 流（终行不再因窗口收缩丢失）；进度通知剥离 logLines（R2，消除 TaskCenter 双索引写 hazard） |
| #1693 | ①StdinReader exact-boundary 死代码分支删除（标准库语义论证 + R1 用 g++ 三态实测，EOF-at-boundary 由既有分支交付，零行为变化）②终态后迟到日志回调早退（终态记录不可变） |

## RED→GREEN 证据（evidence/ + TEST_RESULTS.md）

- **RED（master src，全部 exit 42）**：containment 25/50 断言失败（CSV:/WMS:/PG: 放行实锤）；env_keys splice key 子进程照常启动；job_logs 25002 行无界 + 1MiB 单行原样；ipc >1MiB 5s 无回调（=300s 挂起复现）；ghost 超时加载后 py: 残留（真实 worker daemon）。
- **GREEN×2 双跑（两遍退出码/断言数一致）**：containment 78、env_keys 46、job_logs 24031、ipc 17、ghost 7（评审修复后最终态）。
- **回归族**：mcp_server 9686/31、job_engine 446/34、task_center 533/34（见 flake 备注）、r14_C_env 23/2、exprs_external 83/13、runtime_python_channel_r4 126/12。

## 双轮独立评审

- **R1**（独立 subagent）：APPROVE；F1(P2 scheme:// payload 旁路)+F2-F7(P3) 全部闭环（4 项代码修复+回归用例，F5 转跨轨，F6 注释）。亮点：getline 边界 g++ 实测反例。
- **R2**（另一 subagent，不采信 R1）：初判 REJECT——F1(P1 驱动表 "oracle"≠短名 "OCI" 绕远程默认拒) + F2(P2 嵌套递归栈溢出，PoC 实测 exit 139) 均已修复并加 8 个回归用例；P3 一并闭环。R2 另独立验证 IPC 正则对 daemon 全部 26 个 sendall 点键序、envp 双门、bridge 析构幂等。修复后最终双跑全绿。

## 基线分离（非本轨问题，均已实证）

- `test_exprs_plugin_loader` 16 例败：**A/B**（回退 master 的 plugin_manifest.cpp 同败）→ 本机信任链环境 KNOWN_BASELINE_FAIL。
- `test_task_center` "#1454 cancel-window" 1 例：同刻**纯 master 二进制（R21-C 车道）同断言失败** → 环境态敏感既有 flaky；其余 33 例绿。
- `test_python_plugin_host/manager/plugin_host` 族：SICNU_EMBED_PYTHON=OFF 车道门控 NOT_EXECUTED；替代覆盖=ghost_algo 真 daemon 全链路（#1691），#755 成功路径代码未触碰。

## 性能

#1705 有量化 oracle：单行 ≤4KiB、保留窗口 ∈[5k,10k]（最坏 ~40MiB/作业，此前无界）；逐行 delta O(新增)；snapshot/终态复制 O(窗口)；驱逐块摊销 O(1)/行。其余为安全/正确性修复，无热路径变更（PERF_BASELINE.md）。

## 与 open PR 的冲突排查（PR_COLLISION_MATRIX.md）

- #1675 (R21-C)：mcp_server.cpp 改动区不同 hunk（其 dispatch 区 vs 本轨 StdinReader 区），语义无交集；pi/ 无本轨改动。
- #1673/#1674 + R22 兄弟轨 #1708-1713：仅 #1709 与本轨共享 tests/CMakeLists.txt（双方 append-only 注册块，文件尾隔离）；src 零交集。
- base 经 API 复核未前进，无需 rebase。

## 遗留/跨轨（CROSS_TRACK_FINDINGS.md）

- TaskCenter `m_forwardedLogCounts` 绝对/本地双语义统一（processing 轨，字段已就绪；R1-F5/R2，当前无重复投递，本轨已消除最高频混合写源）。
- worker_daemon.py 位于 src/python/scripts（白名单为 src/python/isolated）——其对端协议修复已在 OWNERSHIP 备案，宿主侧修复独立成立。
- 不等待线上 CI；本地证据为准。daemon 大结果文件/shm 引用通道为后续可选增强（错误消息已指路）。
