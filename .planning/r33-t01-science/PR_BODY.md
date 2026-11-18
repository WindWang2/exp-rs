# R33-T01: 科学数值、缺测语义与有界算法（PolSAR 物理功率 / InSAR 哨兵 / 地形证据 / 精确大核中值）

## 原问题与影响

九个遥感科学算法缺陷（每个都有 issue 报告的静态/轻量数学证据，本 PR 全部转为真实回归）：

1. **#2127 Yamaguchi 左旋功率膨胀**：helix 去污染用 signed `fh/4`，左旋目标的体积/残差被错误方向扣除——典型左旋 fixture 上 SPAN 报 2.2 而 trace-3 是 0.6；且旧测试用非物理负对角协方差锚定。分类产品中左旋螺旋（城市各向异性散射）的体散射/功率分量全部失真。
2. **#2126 geocodeGroundCell beam-away 方向缺 `shz·nz`**：北分量点积不完整，低纬度把沿波束坡度方向归零/偏转，layover/shadow 判错（InSAR 形变链的质量掩膜错误）。
3. **#2128 网络反演速度回归漏参考历元锚点**：两历元像元速度 NaN；非线性真值有偏（u=[0,1,0,1] 回归出 0 而非 0.2 mm/yr）。
4. **#2102 三处有限 NoData 哨兵当信号**：network_inversion 把 -9999 m 当位移进最小二乘；displacement 把 -9999 rad 转成巨大形变并污染 Itoh 统计；remove_topographic_phase 把 -9999（cm）单位缩放成 -99.99 m 假地形。
5. **#2129 多时相 speckle 契约**：全部 companion 共用 ref 的单一哨兵；只比尺寸不比 CRS/transform；band 不存在不拒绝；无有效观测像元回写有限 ref 当"有效值"。
6. **#2091 地貌分类无证据判 plains**：孤立有效像元/单轴无梯度证据（slope kernel 输出 nodata）被分类为平原；无邻居 TPI 留 0 与合法 0 不可区分。
7. **#2094 多方向 hillshade 缺测漂移**：`nodata/8` 累加 8 次，float32 无法精确表示 -99999.9/8，缺测像元漂移成巨大"有效"亮度；中心有效但某方向无梯度证据的像元同样被污染。
8. **#2095 resolveFlats 全缺测不写输出**：成功返回但 caller 缓冲未初始化，D8/D∞ 把 0 当真实高程。
9. **#2088 Float32 median 大核崩溃**：operator 校验放行 7..101 奇数，但锁定版 OpenCV 5.0.0 的 `medianBlur` 在 CV_32F 仅支持 ksize 3|5——大核直接命中 CV_8U-only 断言。

## 已合并基线

基于最新 master `4e1d664d47`（#2134–#2137、#2139–#2141 已合入）。研究快照中 #2135 的科学排他路径已随其合并解除，本 PR 不重复其修复。

## 本次增量

- `sar_polsar.{h,cpp}`：`|fh|/4` 统一扣除（左旋物理 PSD 模型），header/docs 合同同步；旧负对角 fixture 修正为 `conj(u)·conj(u)^H` 并补全功率守恒断言。
- `sar_geocoding.cpp`：beam-away 两分量完整 ENU 点积。
- `sar_network_inversion.cpp`：OLS 锚定 (t₀,0)；operator 逐 pair 读取声明哨兵→丢行（`noDataSentinelSamples` 计数）。
- `rs_sar_displacement_operator.cpp` / `rs_sar_remove_topographic_phase_operator.cpp`：raw 值哨兵相等判定在数值转换/单位缩放之前；Itoh 统计排除；计数上报。
- `sar_speckle.cpp`：per-scene 哨兵 + `compareGrids` 网格校验 + band 存在性 + `count==0 → NaN`；operator 拒绝消息点名契约。
- `terrain_landform.{h,cpp}`：TPI 无邻居→`nodata`；landformClasses 证据门（slope nodata / TPI nodata → class 255）。
- `terrain_analysis.cpp`：hillshadeMultidirectional 逐方向有效性门 + 精确 sentinel 写出。
- `terrain_hydrology.cpp`：全缺测早退路径按契约初始化 `filled`。
- `opencv_filter_operators.cpp`：k>5 精确 float32 选择中值（replicate 边界、NaN 排除、偶数计数取 mid-1/mid 双中位均值、double 求和防溢出），`neighborhoodRadius=k/2` 走流式（300×300 tile seam 逐位验证）；k≤5 保持 OpenCV 路径（parity 锚定）。
- 9 个新测试目标 `tests/test_r33_t01_*.cpp`（核级手算 oracle + registry E2E + 独立 ENU/排序/OLS oracle），`tests/CMakeLists.txt` 仅追加自己的注册块。

## 边界与接口兼容

- 全部改动限既有模块与既有用户流程；无新依赖、无新方向。
- `speckleRaster` 签名不变（哨兵读取内化）；`tpiMultiscale` 无邻居窗口从 0 改为 nodata（header 合同更新，合法 0 平面不受影响——旧行为没有任何测试锚定且 issue 明确定性为缺陷）。
- `resolveFlats` 全缺测输出从"未写"改为镜像输入（caller 契约修复）。
- k≤5 median 行为逐位不变（cv parity 测试）；k>5 从崩溃变为精确流式结果。
- 未触碰 #2134–#2137 曾占用/其他轨道文件；本轨 E2E-01/02/03/05 的相关链段在测试中覆盖（微型复数栅格→operator→重开断言）。

## 验证（命令/数量真实）

- 构建：`cmake --preset dev-default -DOpenCV_DIR=<host opencv5>` + `cmake --build build-dev --config Debug --parallel 4 --target <11 test targets>` — exit 0（双 resource_guard 锁，-j4）。
- 新回归：`ctest -C Debug -j1 -R "(#2088|#2091|#2094|#2095|#2102|#2126|#2127|#2128|#2129|helicity|left-handed mixed|clamps non-physical|left-helix|anchored fit)"` → **22/22 passed**（rebase 到 4e1d664d47 后复跑仍 22/22）。
- 受影响旧套件（直跑二进制）：test_sar_polsar 8c/147a；test_sar_platform10 11c/2766a；test_sar_platform11 6c/357a；test_sar_geocoding 8c/5769a；test_sar_network_inversion 4c/35a；test_terrain_landform 6c/202a；test_terrain_hydrology 12c/1023a；test_speckle_filter 26c/15869a；test_opencv_operators 21c/1457a — 全绿。
- `git diff --check` 干净。

## 独立 review

- **R1**（独立子 agent，读全 diff + 全文上下文 + 独立数学复推导）：**APPROVE_WITH_FIXES**，9 项修复全部确认正确，无 P0/P1；2×P2 已修复（median 流式化+double 求和、speckle 错误消息），4×P3 处置记录于 `.planning/r33-t01-science/DECISIONS.md`（D-006/D-007）。报告：`.planning/r33-t01-science/REVIEW_R1.md`。
- **R2**（同 reviewer 复查修复与最终 HEAD）：**APPROVE**，无 P0/P1/P2；median 流式逐位一致性经 ROI 语义（`opencv_operator_base.cpp:189-198` 排除 halo 边缘）与 BORDER_REPLICATE halo 填充（`gdal_block_stream.cpp:139-165`）双机制独立确认；异常顺序、#691 隔离、D-007 缓期项全部复核安全；reviewer 在 HEAD 上独立重建并复跑 4 个套件全过。报告：`.planning/r33-t01-science/REVIEW_R2.md`。

## 限制

- WP10–WP20（BandMath 展开、Landsat 尺度、PCA/MNF、MAD、VCA、光谱来源、apply_mask、统计语义、时序状态、全局 CC 有界化）为后续工作包，见 `.planning/r33-t01-science/PLAN.md`。
- remove_topographic_phase 的 `noDataSentinelSamples` 按 tile halo 读取计数（同一 DEM 像元在相邻 tile 可计多次），语义为采样数。
- 未跑：线上 CI（按本批约定不等 CI）、Windows 构建、Release 安装验收。

Refs #2127, Refs #2126, Refs #2128, Refs #2102, Refs #2129, Refs #2091, Refs #2094, Refs #2095, Refs #2088
