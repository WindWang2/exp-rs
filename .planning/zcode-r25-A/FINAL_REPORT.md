# R25-A FINAL REPORT

## 状态
- 基线 `origin/master = 2610cb4741e69a89124921e44c03e340e2522ee2`；分支 `agent/r25-A-science-processing-20261028T140339Z`；24 commits。
- **全部 Oracle 完成**；push/PR 待网络恢复（执行时 git/gh 均 TLS EOF，见 §网络阻断）。

## 交付
**6 项新缺陷修复**（master-RED 实测 → HEAD-GREEN×2，429 assertions）：
DEF-1 terrain_solar 纬度 CRS 推导（无 CRS fail-closed）；DEF-2 D8 uint64 精确累积 + Float64 产物；DEF-4/7 波长单位归一化（derivative/resample）；DEF-5 SID 不相交 +inf；DEF-6 apply_mask 单位保真。

**3 项算子深化**：FEAT-1 SAR 几何 param>metadata>default 回退链+provenance；FEAT-2 qa_mask cloud_confidence + SCL 直方图；FEAT-3 post-classification 类别面积表。

**W0 台账**：24 个算子族覆盖（SCIENTIFIC_MATRIX.md），R24-A 因限流未做的光谱/分类/镶嵌家族深审已完成（25+ 内核文件）。

**评审**：R1（REQUEST-CHANGES P0×1/P1×7/P2×1/P3×7 全闭环）+ R2（REQUEST-CHANGES P0×1/P1×1/P3×5 全闭环），均独立子代理。

**性能**：3 套 report-only 基线（0.56/1.26/9.07 Mpx/s + VmHWM）。

## 任务书 Oracle 逐项
1. SCIENTIFIC_MATRIX ✅ 24 族按证据标色
2. ≥4 新缺陷 ✅ 6 项（master-RED 内核实测 + 测试判别）
3. ≥3 深化 ✅ 3 项（schema+实现+错误门+文档+测试）
4. 四类数值 oracle ✅（手算/独立算法/metamorphic/生产 E2E）
5. ≥3 性能基线 ✅（5 次中位 + 峰值 RSS）
6. 产物完整性 ✅（NoData/CRS/schema 一致；失败 abandon；17868 断言 surface 门绿）
7. 跨轨边界 ✅（仅 A 轨封闭域；CROSS_TRACK_FINDINGS 登记）

## 网络阻断（push/PR）
执行至 2026-10-29 22:5x，`git fetch/push` 与 `gh api` 均返回 `TLS connect error: unexpected eof while reading`（连续 5+ 次重试、60s 退避无效）。本地可恢复：分支与全部提交在 worktree `../exp-rs-r25-A-20261028T140339Z` 完好；网络恢复后：
```
git push -u origin agent/r25-A-science-processing-20261028T140339Z
gh pr create --repo WindWang2/exp-rs --base master --head <branch> --body-file .planning/zcode-r25-A/PR_BODY.md
```

## 剩余问题（诚实记录）
- R8 GDAL 轴序策略依赖（RISK_REGISTER）
- R9 cellSizesMetres 恒等 gt 守卫（病态输入）
- sar_terrain_masks 的无条件 LOOK_AZIMUTH 戳记（CROSS_TRACK 移交）
- surface_diff_snapshot.json 与 R25-C 潜在同文件冲突（push 后需按 union 复核）
- 未执行：GPU/TensorRT/在线 STAC/macOS/Windows
