# R25-A CROSS_TRACK_FINDINGS

## 本轨审计处置（不修的留痕）

| 发现 | 处置 | 理由 |
|---|---|---|
| `src/processing/algorithms/change_detector.cpp:108-121` computeLogRatio 把非有限输入折叠为中性 0.0（兄弟内核 logRatio/change_streaming 均返回 NaN） | **登记移交，不修** | grep 全仓无生产调用者（仅本 TU 引用）；非「生产路径重现」缺陷，不计入本轨 oracle。B/D 轨接线该 seam 时应镜像 NaN 契约 |
| `src/processing/algorithms/sar/sar_metadata.cpp` writeSarDatasetMetadata 全仓零调用者（死代码） | 留痕 | 其 kIncidenceKey>0/kHeadingKey!=0 守卫与 FEAT-1 的「默认不戳记」纪律同向；本轨在算子侧用 GdalStreamingOutput::setMetadataItem 直写（贴合既有风格），未启用该助手 |
| json_params_converter.h 重复前向声明（#1646-4 残留） | 登记移交 | src/processing/framework/ 非 A 轨写域 |
| Explore 审计确认面（免复修清单） | 已核对 | SAM/FCLS/VCA/RX/MNF/CEM/OSP/TCIMF/稀疏解混/PCA/变化检测核心/分类评估/指数族/重采样内核/连续统去除——25+ 内核文件深读无缺陷；#1550/#1631 修复完好 |

## 移交 C 轨（继承 R24-A，仍有效）
- CT-R24A-5 temporal:anomaly_alert 零方差 z=0（src/agent/spatial_tools/）
- CT-R24A-6 viewshed agent 工具地理 CRS 无提示（src/agent/spatial_tools/）

## 移交 B 轨（继承 R24-A，状态待复核）
- CT-R24A-1/2 框架 GDALAllRegister/锁纪律（master 注释显示已处理，B 轨确认）

## 本轨对兄弟轨的依赖与互斥
- 共享构建锁 `~/.cache/exp-rs-build.lock`：与 R25-C 轨实测互斥排队（非冲突，纪律生效）
- R25-C 在 `../exp-rs-r25-C-20261028T140339Z` 构建 sicnu_geo_rs/tests；无文件写交集（其轨在 agent/app 面）
