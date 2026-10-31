# R25-A GOAL

## 目标
遥感科学算法与处理链正确性、性能及现有算法能力深化（WindWang2/exp-rs，A 轨封闭域）。

## Oracle（可客观验收）
1. SCIENTIFIC_MATRIX.md 覆盖全部实际编入的 A 域算子族，按证据标色
2. ≥4 条生产路径新缺陷：master-RED + HEAD-GREEN×2（不足则经批准以可验证功能深化补足）
3. ≥3 项已存在算子功能深化：schema+数值实现+错误门+文档+测试
4. 四类科学数值 oracle：手算小格网 / 独立慢算法 / metamorphic / 生产 E2E
5. ≥3 套性能基线（吞吐中位数×5 + 峰值 RSS）
6. 产物完整性（NoData/CRS/extent/schema/metadata）+ 失败/取消无半成品
7. R1/R2 独立评审闭环 → push → PR（不等待 CI、不自动合并）

## 确认的缺陷/深化候选（W0 复核）
- **DEF-1** rs:terrain_solar 纬度静默默认 40°N（R24-A CROSS_TRACK 留痕，master 仍 LIVE）→ 从 CRS 推导或 fail-closed
- **DEF-2** D8 flowAccumulation float32 累加 >2^24 格精度损失（R24-A 留痕）→ 精确累加 + Float64 产物
- **FEAT-1** SAR 地形算子（correction/flatten）忽略已声明 SICNU_SAR_INCIDENCE_DEG/HEADING_DEG/LOOK_AZIMUTH 元数据 → param > metadata > default+warning 回退链
- **DEF-4** 待 W1/W3 深审发现（R24-A 未完成的光谱/分类/镶嵌家族扫描是首选猎场）

## 边界
A 轨可写：src/analysis/{classification,atmospheric,georeferencing,hyperspectral,segmentation}/、src/operators/rs/ 科学核、src/processing/{algorithms,gdal}/、tests/test_r25_a_*、docs/processing/、docs/scientific-state/、.planning/zcode-r25-A/。tests/CMakeLists.txt 仅末尾追加 `# BEGIN R25-A` 块。
