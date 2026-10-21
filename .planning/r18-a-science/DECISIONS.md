# R18-A DECISIONS — 全自动模式下的权衡与默认选择

按任务书 §0：不向用户反复申请常规设计决定，全部记录于此。

| # | 决定 | 理由 |
|---|---|---|
| D-01 | `/goal-loop` 技能未安装（`.agents/skills`、`~/.zcode/skills` 均查证为无），不从外部 GitHub 仓库安装未审计技能 | 任务书允许降级为 ZCode 原生协议 + Oracle 清单；避免引入未审计的外部执行器。不得伪造斜杠命令运行成功。 |
| D-02 | 构建并发取 `-j2`（非硬上限 -j4） | 仓库 CLAUDE.md/docs 明示 -j1 优选 -j2 最大；五轨并行 RAM 纪律；62GiB 宿主上取保守值。不修改资源保护脚本。 |
| D-03 | 构建类型 Release + `ENABLE_TESTS=ON`（镜像 ci-fast preset），独立 `build-r18-a` 目录 | 融合/算法性能测量需要 Release；preset 的 binaryDir 固定为 build-ci-fast 等名字，独立目录避免与其它轨道混淆且不依赖 preset 路径。 |
| D-04 | WP1 = 整体恢复 #1514 被回滚的 5 个文件内容（`git checkout c6b96a5e2 -- <files>` 后补充提交说明），而非重写 | 证据：`git log a8d5cd510..HEAD -- <5 files>` 为空（回滚后无他人改动）；恢复精确内容避免二次引入偏差。恢复的代码/tests 属于本轨独占写区。 |
| D-05 | Inf 处理策略：与 band_math/atmospheric streaming 的现行惯例对齐（`!std::isfinite` → NaN 洗涤于读边界），不改变有限值语义 | 仓库已有先例（band_math.cpp:181-188、atm streaming:653/693）；Oracle-D 禁止以改语义换行为变化，Inf→NaN 是既定家族契约。 |
| D-06 | 每个缺陷先写 RED 测试（在未修复代码上证明失败），再修复，GREEN×2 | Oracle-A；#1514 误报教训：先证伪。若无法复现 → 标记 FALSE POSITIVE 不写 patch。 |
| D-07 | 子代理上限 3（已用于 Recon 只读审计）；编译/高 RSS 测试仅由持锁主执行者运行 | 任务书 §0 约束。 |
| D-08 | 独立 Review：R1 由未参与实现的 reviewer context 对实际 diff 逆向审核；R2 由另一未写 patch 的 context 再审 | Oracle-F；不把单一 Agent 连续自评冒称独立。 |
| D-09 | 共享 `tests/CMakeLists.txt` 仅在新增本轨测试 target 注册必需的最小块时修改，提交前复查 #1513 写集交集并在 PR 标出 | 任务书 §1 共享文件纪律。恢复 #1514 测试不需要新注册（target 已存在：`test_image_fusion`、`test_spectral_classification`、`test_obia_segmentation`）。新增 `test_r18_a_*` 系列时才触碰。 |
| D-10 | 不等待线上 CI（#1491 显示 master CI 本就无完成结果）；交付以本地 Oracle 为准 | 任务书 §0/§8。 |
| D-11 | #1467 剩余两项（spatial_split CMake 归属、bufferedBlockFolds）不接管，仅记录 CROSS_TRACK_FINDINGS | 前人已尝试并撤销（Windows 链接失败证据在 issue 评论）；设计裁决非本轨所有权。 |
| D-12 | 性能改动仅限保持输出 parity（bit-exact 或文档化容差）的优化（SoA 布局、分配提升、循环合并）；禁止为 benchmark 改数值语义 | Oracle-C/任务书 §7。 |
| D-13 | worktree 内 `.planning/r18-a-science/` 只写本轨进度；不向仓库根共享 `.goal-loop-ledger.md` 写全局轨道进度 | 任务书 §1.6。 |
