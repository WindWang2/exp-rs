# R22-05 RISK_REGISTER

| 风险 | 缓解 |
|---|---|
| 与 #1677 文件冲突 | 冲突矩阵 + push 前复查；本轨不写 #1677 文件集 |
| 共享构建机过载 | /tmp/exp-rs-r21-build.lock（七轨共用）+ CMAKE_BUILD_PARALLEL_LEVEL=4 上限、-j4 硬顶，内存紧张降 -j2/-j1 |
| vendor/Qt/PROJ 环境伪红 | 先跑基线分类 KNOWN_BASELINE_FAIL；与 R21-D 记录对照（PROJ_DATA、task_center flaky） |
| #1695 建议删除死代码但测试直编 | 不删类：修复生命周期缺陷（测试仍直编 .cpp，不破坏 tests/CMakeLists 既有行） |
| CP11 渲染门挂死 | 不设 SICNU_CP11_RENDER=1 跑渲染用例（门注释记载本机 legend paint 挂死）；按 R21-D 分类记录 |
| 账本/上下文压缩 | LOOP_LEDGER.md 每轮追加；恢复仅从 ledger+git |
