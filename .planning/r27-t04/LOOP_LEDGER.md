# R27-T04 Loop Ledger (goal-loop)

Format: `第N轮 | 改动 | 验证结果 | 通过/未通过 | 下一步`

- 第0轮 | Preflight：fetch（BASE_SHA=10652fd0）、14 issue 实时复核、无开放 PR、worktree 创建、13 处涉案源码证据确认（#1561 判定 FIXED_IN_MASTER）；规划文档落盘 | git/gh 输出与任务参考一致 | 通过 | Phase A 实施
- 第1轮 | Phase A+C+D 生产修复全部落地（#1745/#1746/#1747/#1788/#1789/#1790/#1791/#1792/#1793/#1794/#1795/#1765）；11 个 test_r27_t04_* 测试文件 + 既有 test_roi_tool_magicwand 泵化适配；发现 master 基线 tests/CMakeLists.txt 解析损坏（17426 交织碎片），cherry-pick r27-t01 修复 d66962c615 后追加本轨块 | cmake 配置 exit 0（修复前 pristine 也失败 → 记录为基线事实）；构建进行中 | 未通过（构建未完） | 等构建 → 跑测试（先 RED 复核再 GREEN）
- 第2轮 | （进行中） | - | - | -
- 第2轮 | 全部修复+测试落地；构建（修 5 轮编译/链接问题：QStatusBar/QtTest/QStringLiteral/链接 TU/ld segfault 重试）；12 套件单跑全绿；回归面 8 套件全绿（stac/mission/v3/r26/classify×2/spectral/roi/interactive）；全套 12×2 双跑全绿；bench 三路径双跑稳定并落盘 BENCH_BASELINE（slot p95 ≤1.21ms vs 同步下限 18–362ms） | 全部 exit 0 | 通过 | R1 独立评审
- 第3轮 | （进行中）R1 | - | - | -
