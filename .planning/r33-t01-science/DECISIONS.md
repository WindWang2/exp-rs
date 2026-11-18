# R33-T01 DECISIONS

## D-001｜#2135 排他路径解除方式
快照中 #2135 的整文件排他表在执行时已随该 PR 合入 master（56f0bd2a0a，2026-11-17T14:16Z）。决定：从最新 master 继承，不重复实现其修复；其文件重新可编辑，但本轨只做 #2135 未覆盖的真实增量。符合公共协议"若其已合并，只通过最新master继承、更新排他表并验证兼容"。

## D-002｜WP01 修复口径：|fh|/4 统一扣除
物理模型（header 契约）：左右旋 helix 协方差的对角贡献均为 |fh|/4、Re C13 均为 −|fh|/4；符号只在 Im C12/C23。决定在 yamaguchi4 中计算 helixQuarter=|fh|/4 后统一用于 fv/r11/r33/r13 扣除；fh 保留符号仅用于 helicity 语义与 helix 功率（|fh|·3/4 不变）。不引入新 API，不修改 freemanDurden（无 helix 项）。

## D-003｜左旋测试 fixture 修正为物理 PSD
tests/test_sar_polsar.cpp 原左旋 fixture 用负对角（非物理协方差）只断言 helix。决定原地修正为 u_left·u_left^H（rank-1 PSD）并补齐 surface/double/volume/SPAN 断言——该测试正是 #2127 指出的错误 fixture，属于缺陷修复而非格式化。新测试按规范放 tests/test_r33_t01_polsar_helix.cpp（核级 oracle + registry E2E），CMake 仅追加自己的最小注册块。

## D-004｜构建资源
用户上限 -j4；双锁（host=~/.cache/exp-rs-agent/host-build + 本轨 build-dev）。首configure因 R33-T06 轨持锁被拒（exit 75，等待45s后按协议放弃本轮）；先推进不占锁的分析工作，稍后重试。不删活锁、不杀别的进程。

## D-005｜OpenCV 依赖路径
宿主共享 OpenCV 5.0.0 位于 main/.scratch/ui-build-deps/opencv（本机唯一 CMake 化 OpenCV，T06 轨同一来源）；configure 显式 -DOpenCV_DIR。非"别人私有路径"。

## D-006｜WP09 大核中值实现口径
OpenCV 5.0.0 medianBlur 合同（imgproc 头文件）：ksize 3|5 支持 CV_32F，更大仅 CV_8U。决定实现精确 float32 选择中值（replicate 边界、NaN 排除、偶数计数取 mid-1/mid 两中位均值、double 求和防溢出），k>5 走 neighborhoodRadius=k/2 的流式路径（R1 建议，300×300 seam 验证逐位一致），k≤5 保持 OpenCV 全帧路径（NaN 排序 parity）。不缩放 UInt8。

## D-007｜R1 deferred 项（P3，不阻塞）
- remove_topographic_phase 的 noDataSentinelSamples 按 halo 重叠读取计数（同一 DEM 像元可计 2–4 次），语义为"采样数"而非"唯一像元数"——结果键名已如实反映。
- k>5 中值路径把 ±Inf tap 视为无效（与本仓 isMissing/!isfinite 惯例一致）；OpenCV k≤5 路径对 Inf 的排序行为由 OpenCV 决定——两路径在 k=5/7 边界的 Inf 策略不同已记录，不在本 PR 强行统一（会破坏 k≤5 的 cv parity 锚）。
- speckle 拒绝细节经 operator 消息文本传递（kernel API 保持 bool），未引入 QString* error 通道（最小改动）。
