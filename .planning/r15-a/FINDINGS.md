# R15-A FINDINGS — A01–A16 工作包审计发现记录

> 状态图例：✅ 已闭合/无缺陷 · ⚠️ 潜在缺陷（有 RED 驱动或死代码）· ❌ 存活缺陷 · 📋 已记录待后续方向
> 每条发现附证据（文件:行 + 测试/构建引用）。仅静态审计结论标注「静态」；经测试证明的标注测试名。

## A02 NoData 方言

| # | 发现 | 状态 | 证据 |
|---|---|---|---|
| A02-1 | 共享契约存在且语义明确：`sicnu::rs::isNoDataValue` / `bandNoDataSentinel`（nodata_utils.h，#A1 防漂移），精确相等 + 非有限即无效 | ✅ | src/processing/algorithms/nodata_utils.h:28-38 |
| A02-2 | **未声明波段的双方言**：NaN-only 族（rs_sam_classify、rs_rx_anomaly、rs_terrain_* 文档明示）vs -9999 回退族（rs_spectral_similarity:127、rs_register_images:183）。两侧均有文档化理由 | ⚠️ 已钉死 | O2 双锁测试（tests/test_spectral_classification.cpp，待提交）；sam metadata() limitations 已记录交叉引用 |
| A02-3 | 手写哨兵比较（`v == nodata \|\| isnan(v)` 形态）约 40 处未路由共享契约，但语义等价 | 📋 风格漂移 | image_fusion.cpp:46-618、image_enhancement*.cpp 等；仅约 12 文件用 nodata_utils（SAR 新族） |
| A02-4 | #612 家族（阈值路径误把哨兵当数值）：change streaming 已闭合（rs_change_streaming.cpp:188-204，声明 NoData→255）；RX anomaly 统计排除声明 NoData（rs_rx_anomaly_operator.cpp:114-129） | ✅ | 静态 |

## A03 融合边界

| # | 发现 | 状态 | 证据 |
|---|---|---|---|
| A03-1 | ~~O1 缺陷：GDAL bilinear 重采样不识别 NoData~~ **O1 = FALSE POSITIVE（diag1–diag4 实证）**：① RED 模式系 fixture 缺陷——VRT 缺 `<GeoTransform>` 时 GDAL 填默认单位变换 [0,1,0,0,0,1]，融合走同分辨率钳位路径（readBandWindow 钳位 + 声明哨兵填充），wash 后呈 `{NaN,6.667,NaN,NaN,6.667,6.667,NaN×10}` 假"掺混"图案；② 带 GT+SRS 的正确 fixture 下，本环境 GDAL GRIORA_Bilinear 对声明 NoData 波段**是** NoData 感知的（排除空值 tap + 权重重归一化），BASE 输出 `{NaN,6.667×15}`/`{NaN,3.333×15}`——void 正确 void、邻域干净精确值，与 resampler.cpp 家族惯例一致；③ 空值仅在主导 tap 处原样透传，被 readMsWindow 精确匹配 wash 捕获。原 RED 测试已重写为 GREEN 回归锁（6d5672419） | ✅ FALSE POSITIVE | 6d5672419；diag 证据链见 LEDGER CP2；test_image_fusion 36 用例/1611 断言全绿 |
| A03-6 | **A03-6（提交信息记作 A14，新发现，勿与工作包 A14=性能混淆）**：单侧缺 GeoTransform 时 compareGrids 返回空报告（`||` 条件把"单侧"混入"双侧均无"回退），融合按单位变换静默走同分辨率钳位路径，产出大面积 void 且无报错。修复：processNativeFusion 增加 hasGeoTransform 对称性阻塞；both-unreferenced 合成回退保留并测试锁定 | ✅ 已修复 | bf5a7d27a；RED：pan GT+CRS + MS VRT 有 SRS 无 GT → BASE "成功"且 12/16 像素 void 无报错；GREEN×2 后 36 用例/1611 断言 |
| A03-2 | 参考实现存在：`rs::algorithms::Resampler` 用权重重归一化 + 50% 阈值，语义与 #700 any-touch 惯例不同但各自文档化且被测试钉死 | ✅ 不同策略 | resampler.cpp:75-77；tests/test_resampler.cpp |
| A03-3 | `PanSharpening::runBrovey` 无任何 NoData 处理（原记"与 O1 同族"——O1 误报后此关联撤销；但"无处理"本身仍是死代码路径的观测）；`upsampleBilinear` 边缘复制无 NoData | ⚠️ 死代码 | pansharpening.cpp:23-40,127-141；`sharpen()` 全仓零调用（src+tests） |
| A03-4 | `evaluateFusionQuality`（Wald 协议入口）全仓零调用，仅注释引用 | ⚠️ 死代码 | image_fusion.cpp:1783 注释 |
| A03-5 | pansharpening.cpp 无栅格 I/O（纯内存核）；spectral_resampling.cpp 为波长域 1-D 重采样——均不属 O1 空间重采样家族 | ✅ | 静态 |

## A13 跨入口等价

| # | 发现 | 状态 | 证据 |
|---|---|---|---|
| A13-1 | 三条 brovey 路径 NoData 策略各异：① image_fusion 静态/流式 = any-void→nodata + #611 负和守卫；② PanSharpening::runBrovey = 无处理（死）；③ 无第三活路径 | ⚠️ 见 A03-3 | image_fusion.cpp:139-156；pansharpening.cpp:127 |
| A13-2 | register_images 入口：Resampler 重归一化 + -9999 默认哨兵（未声明源）——与 image_fusion 的 any-touch→void 策略分歧，但均有测试锁定 | 📋 策略分歧记录 | rs_register_images_operator.cpp:177-183；rs_register_images_operator.cpp 用 resampler.h |
| A13-3 | #1465 3-arg std::equal 全仓 8 处清扫完毕，本分支无存活缺陷（详见 LEDGER L2） | ✅ | asset_state_json ×4（FIXED_ON_MASTER）、temporal_change:218、study_spatial:169 |

## 其余工作包

- A01 算法普查：进行中（所有权内 src/processing/algorithms ~90 cpp + src/operators/rs 128 cpp + src/analysis 6 子域）。
- A04 辐射链 / A05 SAR / A07 光谱 / A08 分类 / A11 地形水文 / A14 性能 / A15 确定性 / A16 回归门禁：待 L4/L5 构建循环间隙继续。

## M2 补充审计（A06/A09/A10）

| # | 发现 | 状态 | 证据 |
|---|---|---|---|
| A06-1 | 时序数值域探针仅采正量程（#856）：未声明 -9999 到达探针不会翻转域判定；TemporalTileReader 仅屏蔽已声明 NoData（文档化） | ✅ | rs_temporal_index_operator.cpp:296-304 |
| A06-2 | #1465 家族 temporal_change.cpp:218 有尺寸相等前置守卫 | ✅ | LEDGER L2 |
| A06-3 | 确定性：bootstrap/分类种子均有固定默认值（bootstrapSeed=20260915、seed=42） | ✅ 静态 | rs_temporal_seasonal_breaks_operator.cpp:255、rs_supervised_classification_operator.cpp:238 |
| A09-1 | multires 核心像素有效性谓词用 epsilon 匹配（abs(val-nodata)<1e-6），违反 nodata_utils.h 精确相等契约；仅内存重载 + 小哨兵可复现（生产路径洗 NaN；float32 无法表示 -9999+5e-7） | ✅ 已修复（待 GREEN 验证） | rs_multires_segmenter.cpp:357 → `sicnu::rs::isNoDataValue(val, nodata)`；RED 测试 9187cdc79 |
| A09-2 | rs_simple_segmenter NoData 完备（重归一化平滑 + 任意波段空值屏蔽 + 量化排除空值） | ✅ | rs_simple_segmenter.cpp:218-330 |
| A09-3 | multires 文件入口按波段洗哨兵成 NaN（#1461 闭合），路由 isNoDataValue | ✅ | rs_multires_segmenter.cpp:702-723 |
| A10-1 | gcp_manager 退化防护完备：重复点坍缩（1e-9）、monotone chain 退化点丢弃、shoelace 面积、Clark-Evans <2 点返回 0 | ✅ | gcp_manager.cpp:29-56,292-316,318-340 |

## 测试目标存在性（ninja -t targets，build-r15-a）

9 个优先目标全部存在：test_image_fusion、test_spectral_classification、test_radiometric_calibration、test_radiometric_qa、test_change_detection、test_obia_segmentation、test_gcp_transformer、test_terrain_hydrology、test_temporal_change。A05/A06/A07 另有 100+ 相关目标（SAR 20+、时序 20+、光谱 40+）。
