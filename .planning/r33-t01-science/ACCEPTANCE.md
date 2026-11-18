# R33-T01 ACCEPTANCE

Base: origin/master 4e1d664d47 (rebase 后；初始基线 56f0bd2a0a)
Head: 见最终 commit（R2 后）

## 每工作包状态

| WP | Issue | 状态 | Oracle 满足 |
|---|---|---|---|
| WP01 PolSAR helix | #2127 | DONE | 双旋 PSD u=(√.2,±j√.2,−√.2) trace3=0.6；混合目标精确恢复；clamp 边界有限；registry E2E 5-band 重开 |
| WP02 beam-away ENU | #2126 | DONE | i=97° 斜视几何 θe=7.4557°，dzdN≥0.8 Shadow vs 旧 Normal；独立 ENU oracle；localIncidence/RTC 一致 |
| WP03 velocity anchor | #2128 | DONE | u=[0,1,0,1]→0.2mm/yr（手算 OLS）；2-epoch 有限；时间原点平移不变；raster E2E |
| WP04 finite sentinels | #2102 | DONE | 逐 pair 哨兵 intersect/perpixel；displacement NaN+计数+Itoh 排除；DEM cm 哨兵缩放前 NaN、洞像元 NaN、24 valid |
| WP05 speckle 契约 | #2129 | DONE | 逐 scene 哨兵（异值）、ref 哨兵≠companion 数据方向 pin、grid/波段拒绝、count==0→NaN |
| WP06 landform 证据 | #2091 | DONE | 孤立像元/单轴缺邻→255；均匀平面仍 plains；倾斜平面仍分类；无邻居 TPI=nodata |
| WP07 hillshade 保真 | #2094 | DONE | -99999.9f 精确 sentinel；逐方向 oracle 均值；无证据方向→NaN；NaN 输入 |
| WP08 resolveFlats | #2095 | DONE | 全缺测 filled=nodata（非零缓冲）；脏复用无残留；4000m epsilon 台阶 pin |
| WP09 大核 median | #2088 | DONE | OpenCV 5.0.0 头文件核验 CV_32F 仅 3\|5；k=7 手算多重集=12；300×300 seam 逐位；偶数计数 mid-1/mid；k≤5 cv parity；1e30 保数值 |

WP10–WP20：本 PR 范围外（后续检查点），见 PLAN.md。

## 验证证据（真实命令与结果）

- 构建：`cmake --preset dev-default -DOpenCV_DIR=.../opencv5` + `cmake --build build-dev --config Debug --parallel 4 --target <11 targets>` exit 0（双锁 -j4）。
- CTest 注册核对：`ctest -N` 1408 tests；新用例 #159–180 全部按名注册。
- 新用例：`ctest -C Debug -j1 -R "(#2088|#2091|#2094|#2095|#2102|#2126|#2127|#2128|#2129|helicity|left-handed mixed|clamps non-physical|left-helix|anchored fit)"` → **100% tests passed out of 22**（rebase 后复跑仍 22/22）。
- 受影响旧套件直跑二进制全绿：test_sar_polsar 8c/147a；test_sar_platform10 11c/2766a；test_sar_platform11 6c/357a；test_sar_geocoding 8c/5769a；test_sar_network_inversion 4c/35a；test_terrain_landform 6c/202a；test_terrain_hydrology 12c/1023a；test_speckle_filter 26c/15869a；test_opencv_operators 21c/1457a。
- 白空格：`git diff --check origin/master..HEAD` 干净。

## Review

- R1（独立子 agent，全 diff + 独立数学复推导）：APPROVE_WITH_FIXES，无 P0/P1；2×P2+4×P3 处置见 DECISIONS D-006/D-007 与 commit 2ffa4e3f49。报告 REVIEW_R1.md。
- R2（同 reviewer 复查修复与最终 HEAD）：见 REVIEW_R2.md（待完成）。
