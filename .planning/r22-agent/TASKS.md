# R22-04 (r22-agent) TASKS / ORACLES / RISK

## TASKS（每个工作包 = 一个 15 轮 goal-loop 循环）

- [ ] M1-D0a #1704：classifyReference 识别 GDAL/OGR 驱动前缀连接串；文件承载驱动解析内嵌路径后递归 containment；网络驱动归 remote 策略；未知驱动默认拒绝。测试：CSV:/GPKG:/NETCDF:/WMS:/PG:/WFS:/PG:host=/嵌套连接串/Windows 盘符豁免。
- [ ] M1-D0b #1690：env key 结构校验（空/含'='/含NUL → E5007）+ PluginManifest 解析期 key 字符集门。测试：`LD_PRELOAD=/x.so `、`A=B`、空 key、大小写变体。
- [ ] M1-D1a #1691：initialize 失败路径撤销 py: 注册（bridge 兜底 + 失败路径显式撤销）；集成测试"注册后超时"无幽灵目录。
- [ ] M1-D1b #1692：IPC oversized 行提取 id 立即回错 + dropInFlight；daemon 发送侧 1MiB 上限 typed error。测试：1.5MiB 响应立即以错误回调（不挂 300s）。
- [ ] M1-D2a #1705：JobEngine 日志上限（行数 + 单行字节）+ droppedLogLines + delta cursor 调整；测试：>上限行数时 logLines 有界、dropped 计数正确、delta 通知不重不漏（可承受丢最旧）。
- [ ] M1-D2b #1693：① StdinReader 死代码分支删除（保留/重构真实边界语义并有测试）；② 终态后日志回调早退。
- [ ] M2：R1 独立 review → 修复 → R2 独立 review → 修复。
- [ ] M3：交付文档 + push + PR。

## ORACLES（最终验收，可客观检查）

1. `pathOutsideWorkspace("CSV:"+工作区外文件) / ("GPKG:...") / ("WMS:http://169.254.169.254/...") / ("PG:host=...")` 全部拒绝；`CSV:`+工作区内文件按 containment 判定；`C:/x`（盘符）与 `C:` 不被误伤（走 LocalPath 原逻辑）。
2. ExternalProcess environment key `"LD_PRELOAD=/tmp/x.so "` → E5007 拒绝且子进程未启动；`"A=B"`、`""`、`"ld_preload"` 同拒；合法 key（PATH 等）不受影响；manifest env key 非法字符在解析期被拒。
3. PythonPluginAdapter 加载超时（插件先注册后 sleep>5s）后 `py:` 算法不在 AlgorithmEngine/目录中；正常 unload 路径回归不破坏（#755 用例）。
4. >1MiB 单行响应：pending 回调立即以 isError+明确 message 返回、takeInFlightRequests 不再含该 id；后续短行正常。daemon 侧超限结果回 typed error（-32001 类）。
5. JobEngine：N=50000 行 logInfo 后 snapshot()->logLines.size() ≤ 上限且 droppedLogLines = N+2-上限；delta 通知序列 (offset,size) 单调且不重复转发；正常短任务日志完整。
6. 上述全部测试双跑（同退出码/同断言结果）；master 基线红（RED）先于修复被记录。

## RISK_REGISTER

| 风险 | 缓解 |
|---|---|
| #1704 修复过宽误伤合法相对路径（含 `:` 的文件名，如 `a:b.tif`） | 驱动前缀判定限定 `^[A-Za-z][A-Za-z0-9_+.-]*:` 且排除单字母盘符；`a:b.tif`（相对名含冒号）在 POSIX 是合法文件名——需要明确策略：仅当冒号前部分匹配已知驱动名（含大小写）或形态为多字母前缀+绝对路径/`/`开头时才视作连接串。测试覆盖 `data:a.tif` 相对文件名场景的行为保持。 |
| #1705 截断导致 TaskCenter 前向计数错乱（m_forwardedLogCounts） | delta offset 语义保持 1-based；丢弃后 cursor=max(0,cursor-dropped)；累积路径 size<forwarded 时 TaskCenter 天然跳过；补专门测试。 |
| #1691 bridge 兜底析构撤销的线程/顺序风险 | 主修失败路径显式撤销（同步、确定性）；dtor 兜底仅撤销注册表项（幂等）。 |
| #1692 oversized 行 id 提取失败（id 不在前缀） | 扫描行首有限窗口；失败则保持丢弃（daemon 侧修复保证自家协议不再产生该形态）；日志说明。 |
| 大构建时间 / 内存 | 只构建需要的 test 目标；flock 串行；-j4 上限；内存 46G 可用，机器紧张时降 -j2。 |
| vendor GDAL/Qt 伪失败 | 环境/基线/本轨回归分开记录（KNOWN_BASELINE_FAIL 替代验证），不提交 link-shim。 |
