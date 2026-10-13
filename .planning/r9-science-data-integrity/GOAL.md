# GOAL — r9-science-data-integrity（方向 A：遥感算法、科学数据与实验可复现性收敛）

- 分支：`hardening/r9-science-data-integrity` ｜ worktree：`../exp-rs-r9-science-data` ｜ base SHA：`8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
- 使命：在 A 方向拥有模块内实现数值/空间语义正确性、数据可追踪与故障不伪成功；复用既有单一权威（QgsDataSourceResolver / ResourceUri / radiometric vocabulary / canonicalizeJsonRfc8785 / durable_sidecar / stepFailed 契约），不发明新品类、不建第二套状态中心。
- 完成条件（Oracle，可客观验证）：
  1. 采纳的每个缺陷有 Bug→RootCause→Repro(red)→Fix→Regression(green) 证据，关键 gate 跑两次（命令+退出码+套件名+通过数）。
  2. 所有修复经本地定向编译（MSVC -j2 上限）+ 定向 CTest（-j1, offscreen）绿灯；无法本地验证的写 NOT_VERIFIED。
  3. 双轮独立 review（reviewer 未参与实现）；P0/P1 findings 全部闭环。
  4. 与 #1469/#1472/#1473/#1474 无重复实现；已修问题标 ALREADY_FIXED_IN_MASTER 不重做。
  5. 最新 master 重放回归后推送分支并创建 PR（base=master，不自动合并、不等线上 CI）。
- 本波上限：15 轮 goal-loop；subagents ≤3 且并发 ≤3；主 Agent 复核关键 diff 与运行结果。

## 本波采纳工作队列（去重后）

来自已合并 PR 的**未解决 review threads**（isResolved=false，全部经主 Agent 源码复核确认）：
- W1 radiometric_state.cpp `layerLocalFilePath`：非盘符冒号一律判 URI → POSIX 合法路径（`/data/acq:2026.tif`）跳过辐射元数据读写，reopen 后回落 DigitalNumber，可能放行已定标数据的二次定标（#1469 review；关联 #1467/#1450）。
- W2 model_catalog.cpp `ModelEoDomainContract::validate()` kStates 缺 sigma0/beta0 → SAR 模型 manifest 在注册期被拒，单权威词汇表对模型不可用（#1472 review；关联 #1450）。
- W3 qgsdatasourceresolver.cpp `localPath()`：`file://localhost/...` 时 `QUrl::toLocalFile()` 先返回非空 SMB 型路径使 localhost 分支不可达，本地存在性检查失败（#1472 review）；伴随 test_datasource_resolver.cpp percent-decode 断言需平台感知。
- W4 temporal_workspace.cpp 通用 datasource 候选循环未做 `localPath()` 规范化 → `file://` 出现在非 `scenes` 参数时报 "unidentifiable local input"（#1472 review）。
- W5 data_manager.cpp relocate 依赖 VRT 发布：`durable_sidecar::write` 返回 VerifyFailed/LastGoodFailed 时目标已发布新字节，但布尔判断吞掉 `assetChanged` → 显示停留在旧状态（#1473 review；关联 #1457）。
- W6 dataset_store_samples.cpp `removeSample` 两个 prepare-failure 分支在 rollback **之后**读 `stmt.error()` → ROLLBACK 成功重置错误串，诊断读 "not an error"（#1473 review；step 分支已修，两个 prepare 分支漏改）。
- W7 operators/framework rs_json_params.cpp `fileExists()`：`file://` 原始串直接喂 `QFile::exists` → 合法输入被 FileNotFound 拒绝（#1472 review；跨界最小变更：路由经 `QgsDataSourceResolver::localPath()`，PR 中标注供 B 串行集成）。

来自开放 Issue 复核（subagent 矩阵 + 主 Agent 抽查）的 STILL_OPEN 项：
- N1 #1456：fabric/query_planner.cpp:363 无条件覆盖 walk 的截断信号 → `catalogMatchesTruncated` 在 maxItems≤sceneBudget 时误报"未截断"。
- N2 #1450：suitability_report.cpp / suitability_goal.cpp 摘要仍用 QJsonDocument::toJson(Compact)，绕过 canonicalizeJsonRfc8785 单权威。
- N3 #1457：stepFailed 收口尾巴：dataset_store_facets.cpp:185/221/240、dataset_store_splits.cpp:184/303、workspace_catalog.cpp:42（CORRUPT/IOERR 折叠为"取尽"）。

明确不做（记录于矩阵）：#1449 asset_state_provider 无锁读写（science_context 归属需与 C 协调）；SHA-256 fork 合并与 spatial_split 分层债（结构性，单列技术债）；#1465 asset_state_json（C 已在 #1474 动过）；全部 ALREADY_FIXED_IN_MASTER 项。

## 轮次规划（每轮 red→fix→green→记账）

R1=W6（最小、有 fault-injection 传统）→ R2=W5 → R3=W1 → R4=W2 → R5=W3 → R6=W4+W7 → R7=N1 → R8=N2 → R9=N3 → 双轮 review → master 重放 → PR。
