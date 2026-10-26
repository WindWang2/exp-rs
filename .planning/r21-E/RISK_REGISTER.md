# R21-E RISK_REGISTER

| # | 风险 | 影响 | 缓解 |
|---|---|---|---|
| R1 | git 传输不通（fetch/push 走 socks5 代理与直连均失败；gh API 可用） | 无法 fetch 最新 master / push 分支 | 用 `gh api` 替代一切远程读；push 时若失败改用 `gh api` 建 ref（GET /git/refs → POST /git/refs 需先逐对象上传，不可行则如实记 NOT_EXECUTED 并保存本地提交）。提交前仍以 gh API 查 master SHA 漂移 |
| R2 | 共享构建机 16C/62G，兄弟轨 A/B/C 在场 | 编译资源争抢 | 默认 -j2 + flock /tmp/exp-rs-r21-build.lock；RSS>70% 降 -j1；ctest -j1 |
| R3 | **src/cli 不在本轨白名单**，而批量评分入口（lab --grade/--batch）在 src/cli/lab_batch_runner.cpp | W1 verifier 接线无法直接改 CLI 文件 | 把 verifier 运行+投影实现为 src/grader 或 src/verify_adapters 内的库函数（在白名单），CLI 通过既有公开头调用；若 CLI 一行不改无法接线，则把该工作降级为「库能力 + 测试证据」，并在 PR 说明 CLI 接线留待 CLI 轨（CROSS_TRACK_FINDINGS 记录） |
| R4 | 依赖本地 vendor prefix（paleo-singlefactor-closure） | 换机不可复现 | 构建命令与 -D 全量记录于 TEST_RESULTS.md；依赖路径为环境事实非本轨产物 |
| R5 | tests/CMakeLists.txt 五轨争抢 | 合并冲突/相互覆盖 | append-only 尾部追加；提交前 diff 复核 |
| R6 | #1601 接线改变评分报告形状 | 老实验包/既有测试兼容回归 | 投影为**增量字段**（evidence 附加项），不改动既有 verdict 语义；全量跑 teaching/grader/verifier 族测试 |
| R7 | 长任务上下文压缩 | 进度丢失 | 每轮记账 LOOP_LEDGER.md；TASKS.md 保持可恢复工作单元粒度 |
