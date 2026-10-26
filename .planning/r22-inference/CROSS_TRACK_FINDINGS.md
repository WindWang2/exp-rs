# R22-03 CROSS-TRACK FINDINGS

| # | 发现 | 影响轨道 | 证据 |
|---|------|---------|------|
| X1（承接 r21-B X1） | 共享环境断裂仍在：vendor libsqlite3.a non-PIC、/usr/lib/libcblas.so.3 缺 LAPACK 64 位后缀符号。新 worktree 需复制 r21-B 配方：系统共享 zlib/zstd/expat/sqlite3 + link-shim 目录 + `CMAKE_EXE_LINKER_FLAGS="-L<build>/link-shim -Wl,--allow-shlib-undefined"` | 所有需要新链接的轨道（r22 各轨） | 本轨 LOOP_LEDGER 轮3 + r21-B CROSS_TRACK X1 |
| X2 | 共享构建锁 `/tmp/exp-rs-r21-build.lock` 观察到**嵌套锁自阻塞**：一轨在外层锁内再取 `/home/kevin/project/exp-rs-build.lock`（内锁），持内锁构建 40+ 分钟，其余轨排队空转。建议各轨统一只用外层锁，或内层构建限时 | R21/R22 全轨 | 2026-10-25 23:30 `ps` 快照：PID 2178871（外锁等内锁）/2198570（持内锁 43min）/2200137（排队） |
| X3 | #1554 / #1553 已在 master 修复（`d9f1b886f1`，验真于 gpu_plane.cpp:44-64 / chunk_graph.cpp:92-104）但仍 OPEN——建议维护者核实后关闭（本轨不代关，issue 归属全员共享） | issue 卫生 | ISSUE_TRIAGE.md |
| X4 | derived finalize 的 uncertainty 分支为不可达死代码（engine :1056 守卫拒绝 derived+uncertainty；R2 确证若可达仍正确）——未来若放开该组合需先删守卫再接测试 | model-runtime 维护者 | REVIEW_R2.md |
| X5 | FeatherAccumulator 窗口内存随类别数翻倍（有界）；若大类别数+大 rasterW 场景吃紧，可考虑半精度权重账本 | 后续 C5 工作包 | PERF_BASELINE.md |
