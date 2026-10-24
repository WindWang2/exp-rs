# RED_GREEN — r21-A

| ID | RED（失败证据） | 修复 | GREEN×2 | 备注 |
|---|---|---|---|---|
| #1591 | 潜在竞态（issue 实读：mutable 缓存在 const 方法无同步写入） | QReadWriteLock 双检 + 显式拷贝/移动 + int64 计数 | test_obia_segmentation 863/15 ×2 PASS | 新增线程 hammer 回归 |
| AG2-F2 | 构造输入：零反射率谱 [0,0,0] → out=1.0（假"在包络上"数据） | 退化分支发 nodata | test_spectral_classification 205/32 ×2 PASS | 生产 rs:continuum_removal |
| AG2-F3 | 端元[1,1] 像元[-2,-2] → OLS 输出 {0.0} sum=0（违反 sum-to-one） | 零和发 NaN + 头文件契约 | test_spectral_unmixing 72/11 ×2 PASS | 生产 rs:spectral_unmixing 默认路径 |
| AG5-1 | scale=false 与 true 输出逐字节相同（代码实读：flag 读于 fit 后） | 先读 flag 门控 fit | test_rs_operators 9102/83 ×2 PASS | 生产 rs:kmeans |
| AG5-2 | calibration={1,2,4} + RF 训练 {1,2,3}：旧门 size 比通过 → 概率/不确定性全静默错 | 比 backend classOrder 身份 | test_classification_intelligence_e2e 6208/6 ×2 PASS | 生产 |
| AG5-3 | k=5 训练类{1,2} → 簇 4/5 输出 raw id 4/5（超标签空间） | value(raw,0) 镜像管线 | obia 族两遍 PASS | 生产 rs:obia_classify |
| AG1-1 | airmass=0 → transmittance=0 → 全像素 NaN + success + surface_reflectance 章 | operator typed refusal + 2 处内核门 | test_atmospheric 37540/47 ×2 PASS | 生产 rs:atmospheric_dos2 |
| AG1-2 | Rayleigh P(90°)=0.7306 vs 标准 0.7694（-5%） | 1+δ → 1+3δ | test_fast_6s_atmospheric 1215/9 ×2 PASS | 无生产调用方 |
| AG1-3 | sunElevationDeg=NaN → 返回 true 全 NaN（违反 fail-closed 契约）；esun=+Inf → 全 0 场景 | 全非有限标量拒绝 | test_radiometric_calibration 195713/34 ×2 + 6S 1215/9 ×2 PASS | seam 级 |
| AG1-4 | bands=[2.5] 静默丢弃 → 7 波段全定标 + bandCount=7 | 非整数项 InvalidParameter | test_rs_operators ×2 PASS | 生产 |
| AG3-1 | DEM=3000 feet → geocode 高度错 2085.6m → 零多普勒斜距错 ~2083 列 | demUnitScaleToMetres + 3 算子接入 | test_sar_kernels 139/14 ×2 PASS（含新单测） | 生产 3 算子 |
| AG3-2 | θ0=35°,θi=15°: 生产 0.219 vs 头文件/内核/exposed 0.435（同 token gamma0 三处矛盾） | 全部对齐 sinθL/sinθ0 + 测试 oracle sin15/sin35=0.451237 | test_sar_foundation5 PASS | 契约对齐 |
| AG3-4 | look_azimuth=NaN → gamma=NaN 但 mask=1"valid" | 别名分支 isfinite refusal | sar 族两遍 PASS | 生产 rs:sar_terrain_flatten |
| AG3-3 | 2 周期 wrap 斜坡 → fit slope≈0 成功返回且不移除 | 圆合成长度诊断 + warning + 元数据 | sar 族两遍 PASS | 诊断性改进 |
| AG4-1 | t=16,r=16, obs{0,8,16,24,32} vals{1,1,1,5,5} → 实测 3.0/2obs（期望 2.6/5obs） | 窗口边界由天数轴推导 | test_temporal_calendar 92/11 ×2 PASS（首遍暴露起点 off-by-one，修复后绿） | 生产 rs:temporal_regularize |
| AG4-2 | 恒定像素 drift=0.5×60 景 → S=−30 假 CUSUM 警报 | sd==0 跳过 step | temporal 族两遍 PASS | 生产 rs:temporal_monitor |
| AG4-3 | DOY=366（闰年 12/31）硬失败 | 接受 1..366 + Spencer clamp | test_terrain_solar 69/5 ×2 PASS（首遍测试自身断言反向，修正后绿） | 生产 rs:terrain_solar |
| AG2-D1 | band1=[1,2,3,ND] band2=[1,2,ND,4] → cov12=1.3333 vs 0.5 | 联合有效集一遍统计 | test_image_fusion 1710/47 ×2 PASS（变形 oracle） | seam 级 |
| AG2-D2 | brovey bands={50,50,+Inf} pan=300 → {0,0,NaN} 发明黑色数据 | 全门 !isfinite | 同上 ×2 PASS | seam 级 |
| AG2-F6 | 4 像素 tile histogramMatch 尺度差 6.7% | 样本标准差 N-1 | 同上 ×2 PASS | seam 级 |
| AG2-F1 | PCA/MNF 内存核均值逐 band vs 协方差 listwise | 联合 finite 集 + isfinite | test_pca 287/8 ×2 PASS（变形 oracle） | seam 级 |
| AG2-F5 | linearStretch +Inf → 255 假数据 | !isfinite | test_image_enhancement PASS | seam 级 |
| AG2-F4 | normalized=[0.2,0.1,0.15] → 半深交叉外推到 65/200nm（谱外） | 截断翼 clamp 到 run 边界 | test_continuum_removal 69/7 ×2 PASS | 测试 seam |
| AG5-5 | 注释承诺的 getVotes row-0 id 校验从未实现 | 实现逐列 id 校验 | test_classifier_random_forest PASS | 潜在 guard |
| AG5-4 | （主代理复核驳回：perBand 本就是累计映射，门已查累计） | 无代码变更 | — | 误报，已记录 |
