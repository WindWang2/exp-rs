# CAPABILITY_MATRIX — before/after（对照 origin/master@a5b11b7f10）

| 能力 | before | after（本 PR） | 状态 |
|---|---|---|---|
| first/last-valid 流式镶嵌 | rs:mosaic | 不变（兼容） | implemented(before) |
| scene/grid plan + overlap inventory | 无 | rs:quality_mosaic plan 阶段 + mosaic_plan 模块（含混合 CRS 足迹统一诊断） | implemented |
| 镶嵌相对匀色（robust gain/bias） | 无 | mosaic_balancing：overlap 统计→稳健回归→BFS 链式传播→限幅/异常拒绝 | implemented |
| seamline（DP 最小成本路径） | 无 | mosaic_seamline：|Δ|+梯度+云+边缘距离成本、确定性 tie-break | implemented |
| feather / 多尺度融合 | 无 | mosaic_blend：feather 默认 + 窗口化 Laplacian，权重恒一 | implemented |
| quality composite + provenance | 无 | mosaic_quality：cloud/quality/time/view 加权 + UInt32 provenance 波段 | implemented |
| 融合 GS/HPF 方法（算子面） | 仅内核有 | rs:image_fusion 增加 gram_schmidt/hpf 方法 | implemented |
| fusion quality report + 失真防护 | 仅内存指标 | fusion_quality_report：Q/RASE/ERGAS/CC/SSIM + JSON artifact + gate | implemented |
| overview/COG-friendly 发布 | 无 | 输出 creation options + BuildOverviews（失败降级 warning） | implemented (degradable) |
| 大图流式 + cancel + 原子输出 | rs:mosaic 有 | rs:quality_mosaic 同语义 + sidecar 原子写 | implemented |
| 跨 CRS 内联重投影 | 无 | 无（fail-closed + plan 指引 gdal:reproject，D-004） | not-supported (by design) |
| GUI 工作台接入新算子 | 无 | 无（D-011 follow-up） | not-supported (follow-up) |
