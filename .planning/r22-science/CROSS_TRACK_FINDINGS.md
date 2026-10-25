# R22-01 science 跨轨发现

## 1. src/app 分类窗口的朴素累加暴露 #1684 同类风险（建议 04/05 轨或 app owner 处理）
`src/app/classification/qgsclassificationmainwindow.cpp:3335` 每类每波段以朴素
`bandSums/bandSumSq`（double）累加后交给 `computeStatsFromAccumulators`。公式端
（本轨已修）在 Sterbenz 条件下精确，但调用方累加仍带 ~√N·ε·sumSq 噪声；对
DN~1e4、σ<1、类内像元数 ~1e6 的教学数据，stddev 特征残余误差可达 ~0.2%。
本轨无 src/app 写权限；建议该调用方改用偏移平移或 Kahan（参考
`rs_segment_features.cpp` 的实现，数学见 `math_utils.h` 契约注释）。严重度 P3。

## 2. `AccumulatorStats` 契约扩展的备选（若未来需要消除调用方下限）
如需彻底消除 ~√N·ε 下限，可给 `AccumulatorStats` 增加 `offset`/`shiftedSumSq`
字段（追加字段保持聚合初始化兼容）。本轨选择不改结构（qgsclassificationmainwindow
在范围外），改为文档化 + 范围内调用方 Kahan。后续轨如改，请同步更新
`math_utils.h` 契约注释。

## 3. #1706 FeatherAccumulator（P1，本轨未实施）
`src/operators/rs/rs_cartographic_operators.cpp` 的多槽位共享 cv::Mat 缓冲 +
权重重复累加问题在允许写范围内，但本轮容量用于 A1-A3 的四项 P1/P2 修复与
双轮评审，未及实施。留给 science 下一里程碑（RED 思路：多类别羽化时断言各类
缓冲互不串扰、权重只累加一次）。

## 4. #1687（P3）部分处理说明
- `bothNormalizable` 的 `-999` 魔数：#1676 已抽出函数但语义未变。改用波段声明
  NoData 需要改 `normalizedDifference` 签名并波及全部调用方（超出本轮最小增量），
  记录于此，建议与 #1676 合并时一并考虑。
- `computeStatsFromAccumulators` 的 validCount 语义：本轨已在 `math_utils.h`
  契约注释中明确（accumulators 路径 validCount==count，NaN 过滤发生在累加方）。

## 5. 构建/环境（供其他轨复用）
- R22 各新轨如遇 qgis_core 链接 `wrapper2_*`/`libcblas *_64_` 未定义与
  libexpat/libsqlite3 非 PIC 静态库问题：解法 = 强制系统共享库
  (SQLite3_LIBRARY/EXPAT_LIBRARY_RELEASE/ZSTD_LIBRARY/ZLIB_LIBRARY=/usr/lib/*.so)
  + `CMAKE_EXE_LINKER_FLAGS="-L/tmp/r21a-deb -Wl,-rpath,/tmp/r21a-deb
  -Wl,--allow-shlib-undefined"` + `CMAKE_BUILD_RPATH=/usr/lib:/tmp/r21a-libs`
  （全部承 R21-A 既有宿主 shim，非产品提交物）。
- GDAL/PROJ/GEOS/GSL/NETCDF 需显式 -D 路径（paleo vendor prefix），CMakeLists
  顺序解析在同机不同 build 目录间不稳定。
