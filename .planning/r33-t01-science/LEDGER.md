# R33-T01 LEDGER

| 轮 | 改动 | 验证 | 结果 | 下一步 |
|---|---|---|---|---|
| 1 | 刷新状态：#2134–#2137 已合并入 master 56f0bd2a0a；建 worktree zcode/r33-t01-science-20261117T150207Z | git/API 查询 | ✅ | WP01 |
| 2 | WP01 #2127：sar_polsar.cpp yamaguchi4 改 \|fh\|/4 统一扣除；.h 与 sar-domain.md 同步；test_sar_polsar.cpp 左旋 fixture 改物理 PSD+守恒断言；新增 tests/test_r33_t01_polsar_helix.cpp（双旋 PSD oracle/混合/clamp/registry E2E）+CMake | 代码级推导（fh=−0.8: total 2.2→0.6=trace） | 待构建 | 等锁构建 |
| 3 | WP02 #2126：sar_geocoding.cpp gy 补 shz·nz（gx 同步完整点积）；新增 tests/test_r33_t01_geocode_beam.cpp（i=97° 斜视几何，dzdN≥0.8 阴影 vs legacy Normal；独立 ENU oracle）+CMake | Python 探针数值验证（θe=7.4557°，唯一零多普勒穿越 t=822.14s，legacy/fixed 分类差异确认） | 待构建 | 等锁构建 |
| 4 | configure 两次被拒（host lock 由 r33-t06 持有，pid 8） | resource_guard exit 75 | 按协议做不占锁工作 | 重试构建 |
| 5 | WP04 #2102：network_inversion/displacement/remove_topographic_phase 三处 finite sentinel→missing（读取声明 NoData、等值→NaN/丢行、计数进 result JSON）；DEM sentinel 在单位缩放前 NaN 化 | 代码级（沿用 polsar R4 模式） | 待构建 | 等待构建 |
| 6 | WP05 #2129：speckleRaster multitemporal per-scene sentinel + compareGrids 网格校验 + band 存在性校验 + count==0→NaN | 代码级 | 待构建 | 等待构建 |
| 7 | WP06 #2091：tpiMultiscale 无邻居→nodata（区分合法0）；landformClasses 证据门（slope nodata/TPI nodata→class 255） | 代码级 | 待构建 | 等待构建 |
| 8 | WP07 #2094：hillshadeMultidirectional 逐像元有效 mask，无效→精确 sentinel，不参与 /8 累加 | 代码级 | 待构建 | 等待构建 |
| 9 | 构建中断后重启（CMake 自动重配置纳入 5 个新测试目标）；OpenCV_DIR=main/.scratch/ui-build-deps（宿主共享依赖，DECISIONS D-005） | 后台运行中 | 进行中 | 等待完成跑 CTest |
| 10 | WP09 #2088：median ksize>5 精确 float32 选择中值（replicate 边界、NaN 排除、偶数计数取两中位均值）；修正实现偶数中位对 bug（mid-1/mid，手算 oracle 暴露）；k≤5 保持 OpenCV 路径 | 同上 | 待构建→已过 | 已过 |
| 11 | 构建全部 11 个测试目标成功；CTest：22/22 新用例过；受影响旧套件全绿：platform10 11c/2766a、platform11 6c/357a、geocoding 8c/5769a、network_inversion 4c/35a、terrain_landform 6c/202a、speckle_filter 26c/15869a、terrain_hydrology 12c/1023a、opencv_operators 21c/1457a | ctest + 直跑二进制 | ✅ 全绿 | 提交 + R1 |

## 修复过程记录（测试驱动发现）
- median 偶数计数 bug：手算 oracle（{10×9,20×9}→15）暴露实现取错中位对（mid 与 mid+1 之后 min，应为 mid-1 与 mid）；实现与测试 oracle 同步修正。
- hillshade 语义深化：中心有效但某方向无梯度证据（PROC-A2-4）时该方向输出 nodata——多维均值对该像元无定义→nodata（逐方向有效性门）。
- landform/resolveflats/topo 测试的若干手算期望修正（replicate 权重、排水方向不抬升、DEM gt 必需）。


## 未解决差距
- WP01/WP02 尚未编译+CTest（host lock 占用）。
- WP03–WP20 未开始。

## 活动进程与锁
- host lock: ~/.cache/exp-rs-agent/host-build ← r33-t06 的 configure（本轨等待，不删除）。
- 本轨 build-dev 锁未持有。
