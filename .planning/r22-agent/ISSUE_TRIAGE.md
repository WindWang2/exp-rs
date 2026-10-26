# R22-04 (r22-agent) ISSUE_TRIAGE

全部经 2026-10-26 实时 `gh issue view` + master@911e0b17 源码复核。

| Issue | 标题/级别 | 分级 | 证据 |
|---|---|---|---|
| #1704 | GDAL 驱动前缀连接串绕过 workspace 沙箱 (P1) | **OPEN_UNFIXED** | R17 verify 评论已实锤（header-only + GDALOpenEx 复现 CSV:/WMS: 放行）。本轨源码复核：`workspace_containment.h:197-248` classifyReference 仅识别 `://` scheme、`file:`、`/vsi*`、`http(s)://`；`CSV:/etc/passwd` 落入 LocalPath，`resolveAgainstWorkspace:169-177` 拼接后判在工作区内。master 无修复 commit。 |
| #1690 | ExternalProcess env key 含 '=' 绕过 LD_PRELOAD 拒绝表 (P1) | **OPEN_UNFIXED** | R17 verify 评论实锤（`LD_PRELOAD=/x.so ` 作 key → started=1 + constructor 写出 PWNED）。源码：`external_process.cpp:105-122` 仅整 key 大小写无关精确比较；`:973/:1006/:509` 直接 `key + "=" + value` 拼接。 |
| #1691 | PythonPluginAdapter::initialize 失败路径不撤销 py: 算法 (P2) | **OPEN_UNFIXED** | 代码确认：`python_plugin_adapter.cpp:171-204` 失败路径无 `unregisterRegisteredAlgorithms()`；成功 unload 路径 `:252-258` 有（#755）。 |
| #1692 | PythonIpcServer 静默丢弃 >1MiB 响应行 → 300s 挂起 (P2) | **OPEN_UNFIXED** | R17 verify 运行时实锤：`python_ipc_server.cpp:176-181` 丢弃后 callback 未触发、未 dropInFlight。daemon `worker_daemon.py:786-789` 发送无大小检查。 |
| #1705 | JobEngine logLines 无界增长 + 持锁全量复制 (P2) | **OPEN_UNFIXED** | R17 verify 运行时实锤：50000 行 logInfo → snapshot logLines.size()==50002。`job_engine.cpp:753-760` 仅 push_back。 |
| #1693 | agent lane 技术债 2 项 (P3) | **OPEN_UNFIXED** | ① `mcp_server.cpp:438-454` StdinReader exact-boundary 分支死代码；② `job_engine.cpp:924-956` 终态后日志回调仍 appendLog + 重插 m_deltaLogCursor（finishJobLocked:794 已 erase）。 |

排除项：其余 R17 issues 分属 science/data/ui/infra 轨，不在本轨白名单内。
FIXED-OPEN 复核：无（上述 6 项均未在任何 open PR 中修复——#1675 文件清单不含上述 5 个修复点所在文件；其 mcp_server.cpp 改动区为 939-1190 行 dispatch/cancel 映射，与 #1704 修复点 200-228/1693 修复点 438-454 不同 hunk）。
