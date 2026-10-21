# R18-A FINAL_REPORT

- **PR**: https://github.com/WindWang2/exp-rs/pull/1663（13 commits，基点 2d2881631）
- worktree：`/home/kevin/project/exp-rs-r18-a-science`，分支 `agent/r18-a-science-numerics-perf`
- 交付时 master：e12a9afbc（+145，needs-rebase 仅 .gitignore 一处琐碎冲突，试验合并实测）

## 交付内容（按 Oracle）

| Oracle | 状态 | 证据 |
|---|---|---|
| A 重复性 | ✅ | 每缺陷 RED→GREEN×2；RED 全记录于 TEST_RESULTS.md；无 FALSE POSITIVE patch |
| B 本地验证 | ✅ | 11 套件 ×2 全绿（最终树）；NOT_EXECUTED 项有补偿说明 |
| C 性能 | ✅ | 分割器 SoA：RSS −10.9%/−18.8%（512²/1024²，各 3 次），时延 −7~15%，输出逐位一致 |
| D 回归契约 | ✅ | band=0/99 可辨识 InvalidParameter；PCA 零有效带 errorMessage；chunked 拒绝退化尺寸；无伪 Success |
| E 所有权 | ✅ | overlap_scan=0；29 文件全部在写区或书面说明（.gitignore 3 行白名单，PR 标出） |
| F Review | ✅ | R1（P0×1+P1×1+P2×3，F4 证伪成功）→ 529481e72 修复 → R2 独立复审判定可合并 |
| G 交付 | ✅ | 13 原子 commits、diff --check 0、review_pack 完整、push 成功、PR URL 真实 |

## 头条发现：#1514 被 #1516 合并静默回滚（6 文件 678 行，含生产防护与全部测试）
恢复为逐字节还原（`git diff c6b96a5e2 41fd31b57 -- <files>` = 空）；机制性风险记录 CF-1。

## 修复清单
1. WP1 恢复 #1514（非对称地理参考防护、QString::arg 修复、精确哨兵匹配、O2 注记、645 行测试）
2. WP2 融合读边界 ±Inf 洗涤（brovey/hpf 邻居毒化等）
3. WP3 内存版 dos1/dos2/dos1Histogram/quac ±Inf 契约（原 -Inf → 全带 +Inf）
4. WP4 raster-spatial 7 算子 band 1-based 范围契约（原 band=0 静默读 band1）
5. WP5 linearWeighted 防护（SIGSEGV UB）+ panWeight 双路由钳制
6. WP6 mosaic 内核+算子 isfinite、ChunkedProcessor 退化尺寸拒绝、PCA 零有效 errorMessage
7. WP9 分割器 SoA（RSS −18.8%@1024²，bit-exact）
8. WP10 quac 单缓冲化
9. R1 修复（P0 编译断链 + P1 生产路线 + P2×3 + 4 新测试）

## 开放事项
- needs-rebase（.gitignore 一行冲突；mosaic 自动合并）——未做全量重测的 rebase，交维护者
- 缓办清单 5 项（TASKS.md，含证据）：band_math regBuffer、ISODATA 扫描、PCA-diff 策略、
  SAR 地形（CF-4）、kmeans RNG（已被 master 6942ed100 处理）
- CF-1 合并取侧未审的机制性建议（给 infra）；CF-6 .gitignore 通用规则丢失
