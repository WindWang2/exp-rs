# R9 Risk Register

| # | 风险 | 影响 | 缓解 |
|---|---|---|---|
| 1 | MSVC+Ninja 全量构建耗时长（14.8k 文件），-j2 下更甚 | 轮次变慢 | 只编定向 target；首波先 configure 再按 target 增量 |
| 2 | WP-A 改锁序可能破坏 #1449 hello-first 顺序契约 | 回归 | 保序由序号/两阶段回放保证；现有顺序测试必须绿 |
| 3 | WP-B PATHEXT 展开改变安全边界（.BAT/.CMD 可发现） | 安全语义变化 | 仅限已有安全审计边界内（manifest 允许的 program 名）；probe 与 spawn 同步扩展；PR 注明 |
| 4 | WP-D 引入等待者后失败/取消路径复杂化 | 新死锁/悬挂 | in-flight 表 + 条件变量；等待者带超时；失败广播测试覆盖 |
| 5 | WP-E 涉及 A 方向 DataManager 边界 | 越界 | 只改 task_center 侧持有方式（QPointer/weak），不动 DataManager |
| 6 | WP-F Qt 事件循环重入语义敏感 | 行为回归 | 最小改动 + 现有 worker 测试全绿；不理想则降级方案 |
| 7 | tests/CMakeLists.txt 与 A/C 并行冲突 | PR 冲突 | 最小 append；必要时在 PR 描述标注串行集成点 |
| 8 | 内存压力（62GB 主机，多 worktree 并行编译） | OOM | 监控 RSS；>70% 降 -j1；不同时多路构建 |
| 9 | PR#1469 thread WRz（src/core/radiometric_state）属 A | 范围外 | 转交记录，不越界修 |
