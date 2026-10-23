# R20-02 CROSS_TRACK_FINDINGS — 跨轨移交（本轨只读发现，不越界修）

| ID | 发现 | 归属轨 | 证据 |
|---|---|---|---|
| X1 | #1597 残余：`pipeline run` / `workflow list-runs` / `data catalog search` / `catalog export` 四个 CLI 入口的未知 flag 静默忽略（parseOptions 主体已修，这四段无 leftover 检查）；`--max-items` 的 toInt ok 位未检（#1595 残余） | CLI/agent 面（03/05 轨候选） | cli_commands.cpp:549-638 / 643-822 / 2128-2680 / 2702-2905、:2400（探索子代理 2026-10-23 实读） |
| X2 | #1647-1：SicnuPythonApi 整类（288 行）从未注册进解释器，qgis_python.cpp:202-204 还向用户广告不存在的 sicnu.project_path() | Python/agent 面（03 轨候选） | sicnu_python_api.cpp:24-288；src/ 内注册点仅 qgis_python.cpp:67/:80 |
| X3 | #1537：STAC searchNext 续翻旧查询分页并冒新代数 | src/app 面板（04 轨） | issue #1537（open） |
| X4 | #1544 涉及 operators/rs 的裸 GDALDatasetH RAII——master 已修（DatasetFileGuard 在位），issue 仍 open，建议随 01 轨或维护者关闭 | 01 轨域 | rs_continuum_removal_operator.cpp 现码实读 |
| X5 | 本轨域 24 条 swarm P1 + ~15 P2 + ~10 P3 已全部修复但 issue 全部 open（ISSUE_TRIAGE.md 逐条证据）——建议维护者批量关闭 | 维护者 | ISSUE_TRIAGE.md |
| X6 | 观测（非缺陷）：2026-10-23 04:3x，r20-03/r20-05 存在未持 `exp-rs-zcode-global-build.lock` 的并发编译（~227 编译进程）；本轨未受影响但共享宿主内存压力大 | 各轨协调 | ps 实测（本账本时间戳） |
