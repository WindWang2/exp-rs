# REVIEW_LOG — review 记录与 disposition

| 阶段 | Reviewer | 范围 | Findings | Disposition | 修复 commit |
|---|---|---|---|---|---|
| Phase 7（计划） | 主 agent 全 diff 自审 | origin/master...HEAD 全 diff | （回填） | （回填） | |
| Phase 7（计划） | subagent #2 只读对抗 review | 同上 | （回填） | （回填） | |

严重级定义：P0 = 科学错误/数据破坏/fail-open；P1 = 契约违背/资源/取消/原子性缺陷；
P2 = 性能/一致性/可读性；P3 = 记录即可。P0/P1 必须修复后才能进入 Phase 8。

## Phase 7 — 独立对抗 review（subagent #2，2026-09-16）

范围：`git diff origin/master...HEAD` 全 diff（76 文件，+8717/−213）。

**结论：无 P0。** 数学内核（符号约定/Cholesky/闭合恒等式）、近侧交叉校验界、
fail-closed 声明、provider 清理、向后兼容、越界修复最小性全部验证通过。

| # | 级别 | 位置 | 问题 | 处置 |
|---|---|---|---|---|
| 1 | P1 | sar_coregistration.h:31 + docs §12 | warp 符号在文档中写反（实现与函数契约均为 +dx/+dy） | 已修复：两处改为 src(x+dx, y+dy) 并注明取反应用 |
| 2 | P2 | rs_sar_pair_network_operator.cpp | Windows 上 QFile::rename 不覆盖已有文件 → 二次发布失败 | 已修复：remove-then-rename |
| 3 | P2 | capability_catalog/harness_error | GRID_CRS_MISSING、DEM_CRS_MISMATCH 未入封闭词表 | 已修复：两表追加 |
| 4 | P2 | rs_sar_network_inversion_operator | "masters must precede slaves" 与实际契约（master index > slave index）相反且未说明与 pair_network 输出的反向关系 | 已修复：消息更正 + 说明 index-reversed |
| 5 | P2 | rs_sar_remove_topographic_phase | topoPhaseOutput 在 NaN 像元写陈旧值 | 已修复：NaN 分支显式置 NaN |
| 6 | P2 | coregister_local/unwrap 预算算术 | 48/28 B/px 实际 vs 声明 40/20 | 已修复：预算上调并更正注释 |
| 7 | P3 | topo/inversion 像元循环 | 取消粒度为 tile 级 | 已修复：加行级 throwIfCancelled |
| 8 | P3 | topo DEM 边界半格 | 严格边界覆盖产生 NaN 条带（topoNaNPixels 计数诚实） | 接受（披露行为；严格内缩会拒绝合法 AOI） |
| 9 | P3 | 近侧校验 | 验证为正确（含偏心轨道/高 DEM） | 无需处置 |
| 10 | P3 | offset-field 栅格 GT | 节点分辨率产品盖 master GT | 已修复：节点中心 GT |
| 11 | P3 | 死代码/陈旧注释/文档 typo 若干 | pairs JSON、prevPhase、GLOBAL dy 注释、"39 baseline"注释、screeningBaseline 速度别名、docs 重复行、outputFile schema 类型 | 已修复/清理 |
| 12 | P3 | screening B⊥ 相位近似 | 已在 header/算子/docs 披露为图级指标 | 接受 |
| 13 | P3 | provider stdout 缓冲/QCoreApplication 构造 | 无实际影响（进程先杀、guard 存在） | 接受 |
| 14 | P3 | COREGISTRATION_FAILED 分类 validation | 可辩护 | 接受 |

修复后重跑：全量受影响 target 重建 + 全套 InSAR 测试 + gates（见 EVIDENCE Phase 8 双验证）。
