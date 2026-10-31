# LOOP_LEDGER — R26-science

| 轮 | 改动 | 验证 | 结果 | 下一步 |
|----|------|------|------|--------|
| 1 | 只读侦察（PR/issues/兄弟轨道/3×Explore 深扫） | gh/git 实时查询 | 12 候选缺陷+防撞矩阵 | 建 worktree |
| 2 | worktree + planning 骨架 + configure(-j4/flock/ccache) | cmake 生成成功 | OK | 写 RED |
| 3 | test_r26_science_oracles.cpp 12 用例 + CMake 注册 | 编译+逐用例运行 | 11 RED / S11 未复现 | 探针复核 S11 |
| 4 | OpenCV copyTo 探针 | /tmp/cv_probe | S11 判 UNREPRODUCED | 实施修复 |
| 5 | 11 项修复（S1-S10,S12） | 全套件 | GREEN 190/12 | GREEN×2 |
| 6 | 第 2 轮 GREEN + 13 个既有回归二进制 | 逐个执行 | 全 PASS 但 d16 tau 钉死值冲突 | 修 S4 语义 |
| 7 | S4 修订：tau-a 契约保留，仅 varS 补时间并列项；头文件公式扩展 | d16+oracle 双过 | OK | E2E |
| 8 | 注册表 E2E（rs:change_detection 真实链路 152 断言） | ctest Passed | OK | perf |
| 9 | 性能基线 3 规格×5 进程 + run2 复现（CROSS_TRACK 记录） | 见 PERF_BASELINE | OK | R1 评审 |
| 10 | R1 独立评审（后台）+ 修复 | 进行中 | — | R2 |
| 11 | R1 评审 approve-with-fixes：P1-1 capped 内存收紧 + 8×P2 修复 | 复验 4 套件全绿 | OK | R2 |
| 12 | R2 独立复审 approve：6×P2 修复（含 S8 自接触行为固化、scaler isDouble） | 16 套件×2 轮全绿 + 注册完整性 | OK | 提交+PR |
