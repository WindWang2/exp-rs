# RISK_REGISTER — R23-E

| # | 风险 | 缓解 | 状态 |
|---|---|---|---|
| R1 | 全量基线构建时长未知（QGIS vendored 大树，GCC -j2） | 后台 detached 构建 + 期间并行推进 WP3/WP5-7 静态/脚本侧；构建失败即记录 KNOWN_BASELINE_FAIL 证据并降级 | OPEN |
| R2 | WSL RAM 15.5GB 上限，重 TU 链接峰值 | -j2（默认纪律）；监控 free；RSS 紧张降 -j1 | OPEN |
| R3 | MSVC/Windows 与 macOS 编译验证不可得（本轨改动跨平台面广） | 改动保持 C++20 可移植写法（与既有代码同风格：无平台专属 API 新增）；标记 NOT_EXECUTED(msvc)/NOT_EXECUTED(macos)；CI YAML 语义离线检查 | OPEN |
| R4 | tests/CMakeLists.txt 共享合并面冲突（#1714→#1715 事故复发） | 仅尾部 append-only 块；push 前重 fetch + 注册并集脚本验证 + ctest -N | OPEN |
| R5 | 与 C 轨在 src/python/scripts/worker_daemon.py 的历史交集（#1714 曾改） | 改前 diff 复核 #1714 语义不回滚；新增测试驱动而非改协议 | OPEN |
| R6 | #1708 修复被本轨无意回滚（RPATH/worker install/bundle guard） | WP4 以 #1708 PR 正文为核对清单逐项复测；diff 审查含这些文件零触碰声明 | OPEN |
| R7 | 独立 Review 上下文受限（单机 subagent） | R1/R2 用独立 fresh subagent，仅给 diff+契约+复现步骤，不给开发者自评；不可得时诚实记 INDEPENDENT_REVIEW_UNAVAILABLE | OPEN |
| R8 | gh push 网络失败 | 重试指令+已 push 证据入 FINAL_REPORT；不谎报 | OPEN |
