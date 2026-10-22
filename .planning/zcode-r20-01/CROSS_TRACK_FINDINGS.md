# CROSS_TRACK_FINDINGS — zcode-r20-01

本轨审查中发现、但属其他轨所有权范围的缺陷。本轨不修，登记移交。

## CT-R20-01-1 → 02 轨（数据/模型/运行时）

- **#1541 [P1]** `src/operators/runtime/detection_tile_engine.cpp`：**LIVE 复核（2026-10-22）确认已在 master 修复**——:331 `if (bandCount <= 0 || bandCount > CV_CN_MAX)` typed refusal 已存在且注释直接引用 #1541。属"已修未关"，建议 02 轨关闭；无需任何代码动作。

## CT-R20-01-2 → 02/03 轨（处理框架/IO）

- **#1642-1** operators/framework rs_operator_registry.cpp:86-98 listSchemas() 持非递归互斥量调工厂（自死锁风险）
- **#1642-2** operators/runtime opencv_dnn_runtime.cpp:215 const_cast 剥 const 加锁（UB 风险，应 mutable）
- **#1642-3** operators/io 15 处数组参数 schema 声明为 "string"（机器契约与运行时不符）
- **#1646-1/2/3/4/5** processing/framework GDALAllRegister 绕过 ensureGdalInit、local_worker_pool 无锁读、processing/gdal 移动构造丢 lastError、json_params_converter 重复前向声明、algorithm_help_catalog 14 死键
- **#1467 主体** src/core temporal_cube.cpp:369 静默吞读失败→NaN 合成、radiometric_state GDALOpenEx provider URI、spatial_split/temporal_cube 错误归属

## CT-R20-01-3 → 02 轨（提案）

- `rs_spatial_cross_validation.h`（analysis）与 `src/core/spatial_split.cpp` 的 buffered block folds 双实现（#1467）：建议 02 轨收敛到单一实现，analysis 侧跟随。

## CT-R20-01-4 → 04 轨（Qt/UI，观察项）

- 无新发现（本轮未触 UI）。

## 本轨收到他轨影响

- 无（R19-02 CT-2 所列 rs_multires_segmenter/rs_sam_classify_operator/image_fusion 恢复项经代理实读确认在 master 正常存在，未再丢失）。
