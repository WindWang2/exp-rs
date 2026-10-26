# R22-04 (r22-agent) FINAL_REPORT

## 概要

轨道 r22-agent（R22-04：基于 Pi 的空间 Agent/MCP 工具安全、流程一致性与可恢复交互闭环）第一批工作包交付：6 个 issue 的修复（#1704 P1、#1690 P1、#1691 P2、#1692 P2、#1705 P2、#1693 P3），全部经 master-RED → GREEN → GREEN×2 双跑 + R1/R2 双独立评审闭环。

- 基线：origin/master = 911e0b17a3533ca0d79c1b68b000d6052c4c3c61
- 分支：agent/r22-agent-20261025T164309Z（worktree /home/kevin/project/exp-rs-r22-agent-20261025T164309Z，本轨唯一）
- 提交：9 个（5 修复 + 测试注册 + 3 文档/评审），每工作包可独立回滚

## 修复清单（详细反例链见各 commit message 与 REVIEW_R1/R2）

| Issue | 缺陷 | 修复 |
|---|---|---|
| #1704 P1 | GDAL 驱动前缀连接串（CSV:/GPKG:/WMS:/PG:）落入 LocalPath 拼接工作区后放行，GDAL 按前缀打开真实工作区外目标（沙箱逃逸 + 远程默认拒绕过） | classifyReference 新增驱动前缀识别：文件驱动提取内嵌路径（引号/盘符/layer 分隔/嵌套 scheme//vsi）后按同一策略递归 containment；网络驱动走 SICNU_MCP_ALLOW_REMOTE 默认拒；未知驱动危险 payload（绝对/引号/xml/kv/../scheme://）拒绝；嵌套驱动前缀递归分类（R1-F2）；任意 scheme:// payload 走 URL 策略（R1-F1） |
| #1690 P1 | env key 含 '='（"LD_PRELOAD=/x.so "）绕过整 key 精确比较拒绝表，envp 拼接后 ld.so 按首个 '=' 切分实现代码注入 | run 前（POSIX/Windows 两平台）拒绝空/含'='/含 NUL 的 key（E5007）；PluginManifest 解析期即校验 env key 字符集 |
| #1691 P2 | 插件加载超时失败路径不撤销已注册 py: 算法 → 幽灵目录条目（恒"IPC Server not available"） | initialize 失败路径显式 unregisterRegisteredAlgorithms（对齐 #755 成功路径）+ ~AppInterfaceBridge 幂等兜底 |
| #1692 P2 | PythonIpcServer 静默丢弃 >1MiB 响应行 → 调用方挂满 300s；>4MiB 触发 worker 崩溃重放 | 宿主从 256B 前缀按响应形状锚定抢救 id，立即以 -32001 typed error 回调并 dropInFlight（R1-F4 加固：锚定 "jsonrpc" 键序 + 溢出安全解析）；daemon 发送侧 >1MiB 回 typed error，两侧阈值一致 |
| #1705 P2 | JobEngine logLines 无界增长 + snapshot/终态持锁全量复制 | 单行 4KiB UTF-8 安全截断；>10k 行块驱逐至 5k（droppedLogLines 精确记账）；delta 通知改绝对索引空间（驱逐不重编号、消费端 exactly-once）；终态通知在有 cursor 时走同一 delta 流（终行不再可能因窗口收缩而丢失） |
| #1693 P3 | ①StdinReader exact-boundary 死代码分支（标准库语义论证 + R1 g++ 实测三态验证）②终态后迟到日志回调改写终态记录并泄漏 delta cursor | ①删除（零行为变化，EOF-at-boundary 由既有 eof 分支交付——实测）②终态不可变早退 |

## 验证链

- RED（master src）：5 个新测试二进制全部 exit 42，反例断言逐一失败（evidence/RED_*.log，含 issue 级复现：CSV: 放行 / splice 注入 started=1 / 25002 行无界 / >1MiB 挂起 / ghost 残留）
- GREEN×2：containment 70、env_keys 46、job_logs 24031、ipc 17、ghost 7 断言，双遍完全一致
- 评审修复后再双跑：同上全绿
- 回归族：mcp_server 9686/31×2（±1 为既有非确定条件断言）、job_engine 446/34、task_center 533/34、r14_C_env 23/2、exprs_external 83/13、channel_r4 126/12
- KNOWN_BASELINE_FAIL：exprs_plugin_loader 16 例（A/B：master 同文件同样 16 败——本机信任链环境）
- NOT_EXECUTED：test_python_plugin_host/manager/plugin_host（SICNU_EMBED_PYTHON=OFF 车道门控；替代覆盖=ghost_algo 真 daemon 全链路）

## 评审

- R1（独立 subagent）：APPROVE；F1(P2)+F2-F7(P3) 全部闭环（F1/F2/F4/F7 代码修复+回归用例；F5 转跨轨；F6 注释说明）
- R2（另一独立 subagent，不采信 R1）：初判 REJECT——R2-F1（P1：网络驱动表 "oracle" 应为连接串短名 "OCI"，EEDA/AmigoCloud/GeoRaster 同漏，OCI:usr/pwd@db 可绕远程默认拒）与 R2-F2（P2：嵌套驱动递归无深度上限，"KML:"×100000 PoC 实测 SIGSEGV）。两项已修（表名更正+8 层深度上限 fail-closed）并加 8 个回归用例；P3 项（进度通知剥离 logLines 消除 TaskCenter 双索引 hazard、CMake 注释、INFO 次序、TileDB 真名、kvKeys 补 path=/driver=）一并闭环。修复后最终双跑全绿（containment 78×2 / env 46×2 / job_logs 24031×2 / ipc 17×2 / ghost 7×2；job_engine 446/34、mcp_server 9686/31）。R2 还独立验证了 IPC 抢救正则对 daemon 全部 26 个 sendall 点的键序匹配、envp 双门、bridge 析构幂等、#1705 单线程 exactly-once 代数。→ 两轮独立评审全部闭环。

## 遗留与跨轨

- CROSS_TRACK_FINDINGS.md：TaskCenter m_forwardedLogCounts 双语义统一（processing 轨，R1-F5）；worker_daemon.py 白名单归属备案
- 后续可选项（未做，非本批 oracle）：daemon 侧大结果文件/共享内存引用通道（#1692 修复已在其错误消息中指路）；PR_COLLISION_MATRIX 需在 #1675 等合并后重跑受影响族
