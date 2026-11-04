# R25-A TASKS

## 已完成
- [x] W0 实时侦察（BASELINE/ISSUE_TRIAGE/PR_COLLISION_MATRIX 骨架）
- [x] W0 R24-A 留痕候选复核：terrain_solar 40°N / D8 float32 / SAR 元数据均 LIVE
- [x] DEF-1 fix+test（commit d4ce002bed）：terrain_solar 纬度三级解析
- [x] DEF-2 fix+test（commit 269b14426d）：uint64 精确累积 + Float64 产物
- [x] DEF-4 fix+test（commit c1ce60c289）：derivative 单位归一化
- [x] DEF-5 fix+test（同上）：SID 不相交 +inf
- [x] P3：hybrid similarity meanScore 非有限过滤（同上）
- [x] FEAT-1 feat+test（commit 5a99f7117e）：SAR 几何回退链
- [x] DEF-6 fix+test（commit 3c3455aee0）：apply_mask 保真 WAVELENGTH_UNITS
- [x] DEF-7 fix+test（同上）：resample 源轴单位归一化
- [x] W3 光谱/分类/镶嵌家族深审（Explore 代理 + 主代理实读复核；25+ 内核文件）

## 进行中
- [ ] 构建 + 全部 RED/GREEN×2 实测（排队于共享构建锁后）
- [ ] W1 光学预处理链复核（qa_mask→apply_mask→定标→大气→指数数据流）

## 待办
- [ ] W7 E2E 链（光学 QA→校正→指数→变化；DEM→水文；时序）
- [ ] 性能基线 ≥3 套（terrain/光谱/变化或镶嵌；5 次中位 + 峰值 RSS）
- [ ] SCIENTIFIC_MATRIX.md 全域覆盖表
- [ ] R1 独立评审 → 修复 → R2 → 修复
- [ ] merge-integrity 检查 + push + PR
