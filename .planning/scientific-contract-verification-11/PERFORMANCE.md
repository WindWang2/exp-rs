# PERFORMANCE — scientific-contract-verification-11

资源模型与证据（本 track 不以 wall-clock 为 correctness gate）。

## 构建资源

- Host：Windows 10.0.26200 x64，MSVC 2022，Ninja，`-j2` 硬上限（GOAL 约束）。
- load average 在 Git Bash/Windows 不可测 → not-executed（GOAL 允许，记录一次）；CPU/RSS 每 60s PowerShell Get-Process 抽样（build-dev/monitor.log）。
- 配置：Debug + ENABLE_TESTS + vendored qgis_core（构建时长主导项）。
- configure ≈ 16.4 min（988s + generating 76s），build 全库栈 + 7 个 gate 测试可执行文件 ≈ 60–120 min（-j2, 3307 targets）。
- RSS 抽样：1.3–2.4 GB（20+ 进程），低于 70% 内存阈值，无需降 -j1。

## 逻辑规模（bounded scale 纪律）

| 测试 | 逻辑规模 | 上界 |
|---|---|---|
| census 扫描 | src/ 全树 2 遍（registration + class slices），文本预过滤 | ≤ ~4k 文件/遍；纯本地 IO |
| replay corpus | 10 recipes × 2 runs，16×16×2 Float32 | 秒级 |
| metamorphic | 6 relations，≤24×24 fixtures，seeded mt19937 | 秒级 |
| numeric reference | 5 closed-form 组，≤16×16 | 秒级 |
| mutation kill | 10 mutants + 1 threshold 掩码组 | 秒级 |
| failure lane | 5 注入类，8×8 fixtures | 秒级 |
| cross-surface | help/agent JSON 全量解析 + registry diff | 静态数据级 |

## 内存/队列上界主张

- 所有 11 系列测试使用固定小 fixture（≤24×24），无 OOM 面、无并发队列；wall-clock 不作为断言出现。
- ladder 每项硬 timeout（已按 suite 声明），hang 不能吃掉 host。

（构建后回填实测值。）
