# R16-A Risk Register

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R1 | 与并行 R16 其它 Prompt 抢 CPU/内存(共享 40 核/62GB) | 高 | 共享构建租约 flock(/home/kevin/project/exp-rs-build.lock)序列化全量 C++ 编译;每 worktree 独立 build-dev;-j2 上限;RSS>70% 降 -j1 |
| R2 | 旧 Issue 盲修造成重复代码(#1488/#1464 均疑似已修) | 高 | 阶段 0 已建立四态分类;每个候选缺陷先写复现用例(RED)再改;已修复的只写实证否定报告 |
| R3 | tests/CMakeLists.txt 与 PR #1515 冲突 | 中 | 只做最小增量追加注册;不改写既有行;PR 前对比 #1515 变更区域 |
| R4 | 全量构建耗时(QGIS 主体巨大) | 高 | 只构建叶子测试目标(test_verifier_*_14、test_grader_*、test_faultlab*);不构建主程序 |
| R5 | 两子代理意外越界写文件 | 中 | OWNERSHIP.md 明确清单;集成时用 git diff --name-only 校验每轨提交的文件集合,越界提交退回 |
| R6 | 伪造测试结果/伪 GREEN | 高 | 集成 Agent 复跑全部关键测试两遍;Review 检查测试非空转(断言真实行为);RED 证据必须先于修复存在 |
| R7 | cherry-pick 冲突处理不当 | 中 | 逐提交 cherry-pick 保留来源;冲突人工语义解决;集成后跑双轨全部测试 |
| R8 | master 在执行期间前进 | 低 | PR 前重新 fetch;若前进则 rebase 集成分支并复跑回归 |
| R9 | 破坏与 grader↔verification 的 canonical digest parity | 中 | 轨道内改动不得改变既有 canonical 字节;如有语义变化必须先在 LEDGER 记录并由集成 Agent 否决或建合同测试 |
| R10 | 内存压力(5 Prompt × 多 worktree 并行) | 中 | 动态降档;构建租约串行;ctest -j1 |

## 降档规则(执行时如实记录)

- 默认 -j2(CLAUDE.md 规约,严于任务书 -j4 上限)。
- `free` 可用内存 < 30% 或 load > 1.5× 核数时降至 -j1,并在 LEDGER 记录降档事件。
