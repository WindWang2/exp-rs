# r25-A: 遥感科学算法与处理链正确性、性能及现有算法能力深化

- **BASE**: `2610cb4741e69a89124921e44c03e340e2522ee2` (origin/master)
- **HEAD**: `84a4ec95c70fcfd6248059a37a80564937a20b3e`（分支 `agent/r25-A-science-processing-20261028T140339Z`，24 commits，已 push）
- **Issue 归属**: 任务书候选 #1538/#1550/#1631/#1646/#1687 经实时复核全部 FIXED_IN_MASTER（#1718/#1722 已修，本 PR 只出确认报告，见 `.planning/zcode-r25-A/ISSUE_TRIAGE.md`）；本 PR 全部为新缺陷与新深化。

## 新缺陷（master-RED 实测 → HEAD-GREEN×2）

| ID | 缺陷 |
|---|---|
| DEF-1 | rs:terrain_solar 纬度静默默认 40°N（南半球夏季 DEM 被当作 40°N 冬季算阴影；day 355：9 vs 13 日光样本） |
| DEF-2 | D8 flowAccumulation float32 累积 >2²⁴ 格失真（4097² 漏斗出口 master 报 16785408，精确值 16785409） |
| DEF-4 | rs:spectral_derivative 忽略 WAVELENGTH_UNITS（μm 轴读作 nm：一阶导数 1000×、二阶 10⁶× 失真） |
| DEF-5 | SID 完全不相交支撑返回 0.0（"完美匹配"假象；sidClassify 按 lowest-index 误分类） |
| DEF-6 | rs:apply_mask 复制 WAVELENGTH 但丢 WAVELENGTH_UNITS（μm 轴下游被当 nm 读） |
| DEF-7 | rs:spectral_resample 源轴原始读取（μm 源 + nm 目标 → 全 NaN "成功"产物） |

## 算子功能深化（默认行为不变）

| ID | 深化 |
|---|---|
| FEAT-1 | SAR 地形算子（flatten/correction）几何回退链 param > SICNU_SAR_* 输入元数据 > 默认+警告；来源 provenance；输出再声明（默认猜测不戳记）；垃圾元数据带来源 fail-closed |
| FEAT-2 | rs:qa_mask `cloud_confidence`（standard 默认=历史行为 / relaxed 去 SCL-8 中概率云与 Landsat dilated cloud）+ SCL 类直方图质量摘要 |
| FEAT-3 | rs:post_classification_change 类别面积表（fromAreasKm2/toAreasKm2/changedAreaKm2 + cellAreaM2/基准命名，地理 CRS 走 #612 弧长纪律） |

## 测试（8 个新二进制 + 1 个重新生成）
- `tests/test_r25_a_{terrain_solar_latitude,terrain_flow_precision,spectral_guards,sar_geometry_fallback,qa_confidence,change_area_table,optical_chain_e2e,perf_baseline}.cpp`
- 数值 oracle：手算小格网（日长公式/线性坡/平地 θi=θ0）、独立慢算法（漏斗逐格组合计数）、metamorphic（≤2²⁴ float 契约、#783 哨兵、部分重叠 SID、显式参数优先）、生产 E2E（全经 RSOperatorRegistry）
- master-RED 内核实测（`git show origin/master` 独立编译）：SID 0.0 vs inf、label=0 vs 1、float32 16785408 vs 精确 16785409

## 验证（本地，未等待 CI）
```
cmake -S . -B build-r25-A -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON        # exit 0
flock ~/.cache/exp-rs-build.lock cmake --build build-r25-A --parallel 2 --target <targets>  # exit 0
# GREEN x2: 429 assertions across 7 correctness binaries (exit 0)
# 邻域: test_terrain_solar/hydrology/foundation5/viewshed/analytics_e2e = 3925 assertions 全绿
# test_surface_snapshot: 17868 assertions 全绿（快照已随 schema 变更重新生成）
# 性能基线（report-only，5 次中位 + VmHWM）: flow_accumulation 0.56 Mpx/s / 364.8 MiB；
#   spectral resample 1.26 Mpx/s / 597.0 MiB；post-classification change 9.07 Mpx/s / 597.4 MiB
# 合并完整性: git diff --check exit 0;
#   check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs → PASS
#   (1071 = 1063 + 8 targets, 0 lost, 0 duplicates, 0 markers)
```

## 评审
- R1（独立子代理，REQUEST-CHANGES P0×1/P1×7/P2×1/P3×7）→ 全部处置 → REVIEW_R1.md
- R2（新上下文子代理，REQUEST-CHANGES P0×1/P1×1/P3×5）→ 全部处置 → REVIEW_R2.md；test_surface_snapshot 17868 断言绿

## 跨轨风险与未执行项
- **共享文件合并要求**：`tests/surface_diff_snapshot.json` 同时被本轨与开放 PR #1727 (R25-C) 修改（双方均基于 master 各自重新生成）。本轨快照含本轨 4 处 schema 变更；合并双方后须用 `surface_snapshot_gen` 按 union 再生成一次，Git 不能文本自动合并该文件。`tests/CMakeLists.txt` 与 `.gitignore` 均为 append-only 块，union 安全。
- R8 GDAL 轴序策略依赖（已 ±90 门 + 注释）、R9 cellSizesMetres 恒等 gt 守卫（病态输入）登记 RISK_REGISTER 后续
- 未执行：GPU/TensorRT/在线 STAC/macOS/Windows NOT_EXECUTED（无资源）；性能为共享宿主 Debug 构建数据，无 master 对照硬门（repo 纪律）
