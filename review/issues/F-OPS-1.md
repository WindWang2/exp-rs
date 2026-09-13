# [IO/Operators] io:reproject 的 srcCrsOverride 是死参数——无 CRS 输入经"唯一获准兜底"重投影后静默错配地理参考

P1
Affected Location: src/operators/io/io_operators.cpp:301-323（读而不传；schema :278-279 文档化为"the only sanctioned fallback"）；src/geospatial/convert/raster_convert.h:50-62（WarpOptions 无源 CRS 字段）；src/geospatial/convert/raster_convert.cpp:202-204（warp 只建 -t_srs）
Root Cause & Impact: run() 校验 CRS-less 输入必须声明 srcCrsOverride，但 WarpOptions 根本没有源 CRS 字段可承接，warpRaster 从不写 -s_srs。GDALWarp 对无 SRS 源按"源 CRS==目标 CRS"处理：像素零变换，输出栅格被打上 targetCrs 标签——像素坐标空间的数据被标注成例如 EPSG:4326，下游叠加/量测/切片全部静默错位。声明参数全程无效，正是 #646 "declared knob does nothing" 类。subagent V 补强：同文件 io:clip 对同名参数做了功能性消费（io_operators.cpp:383-386，折入 targetCrs），证明这是漂移而非设计。
Reproduction: review/tests/F-OPS-4.cpp——4×4 无 SRS GTiff，io:reproject 带 srcCrsOverride=EPSG:4326、targetCrs=EPSG:32633；断言输出中心像元坐标等于 4326→32633 变换结果（现实现为未变换的像素网格）。
Recommended Fix: WarpOptions 增加 sourceCrsOverride，warpRaster 非空时 emplace -s_srs；IoReprojectOperator 传入参数。
