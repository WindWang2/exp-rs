# r23-A ORACLES

## O1 (#1687 归一化差数值契约)
- 输入对 (a,b):
  - 非有限(NaN/±Inf 任一)→ 输出 NaN
  - a+b == 0 → 输出 NaN(safeDiv 契约,含 (0,0) 与 (x,-x))
  - 其余全部计算 (a-b)/(a+b),**含负反射率**:
    - a=b=-0.01 → -0.0 ≈ 0(现 master 输出 NaN = RED)
    - a=-1500,b=-500 → 0.5(现 master NaN = RED,因 ≤-999 魔数)
    - a=-0.02,b=-0.01 → 1/3(现 master 已正确,回归 pin)
- 依据:归一化差在 a+b<0 时数学上良定义;大气校正后地表反射率可为轻微负值(水体/暗目标),同值负对 NDVI=0 是"无植被信号"的正确语义;NoData 判定权威在波段声明(全部生产调用方已预转 NaN),内核不得用魔数猜测(#1687-1)。
- RED 验证:tests/test_math_utils.cpp 新 SECTION(修复前 NaN≠0 失败);GREEN×2。
- 变异反例:回退 bothNormalizable 旧实现 → 测试必须变 RED。

## O2 (待审计补充)

## O3 (F1 viewshed 亚米格网)
- 平面 DEM 全 100m、观测者中央、0.3m 格网、全幅半径 → visibleFraction 必须 = 1.0(master 上大片假 0)。
- 数学依据:环 = ceil(d/min(csx,csy)) 时,朝向观测者的 8 邻步使 d 至少降 min(csx,csy)(u 为单位径向,步投影 = csx|ux|+csy|uy| ≥ min(csx,csy)·(|ux|+|uy|) ≥ min(csx,csy)),故前驱环 ≤ 当前环−1,扫描永不断链;原实现按整地图单位量化,步长 <1 时同环邻居无合法前驱。
- RED:test_terrain_viewshed 新 TEST_CASE(0.3 与 0.25×0.8 两节)。

## O4 (F2 曲率混合导数)
- z=x·y 于 5×5 单元格中心:zx=zy=2、zxx=zyy=0、zxy=1 → profile=+1、plan=−1、total=0(master:profile=+2)。
- 各向异性 csx=0.5/csy=2:zx=4、zy=1、zxy=1 → profile=8/17(master:16/17)。
- 依据:∂²z/∂x∂y = [(i−c)−(g−a)]/(4csx·csy);代码用 (g+i−a−c)(x 翻转对称,不可能为混合偏导)。

## O5 (H1 ROI 波长单位)
- WAVELENGTH=0.49/0.665 + WAVELENGTH_UNITS=um → 输出 490/665 nm(header 已承诺 nm;master 输出 0.49/0.665)。
- 依据:SpectralWavelength::normalizeToNm 为全仓唯一权威;其余 4 个读取方均已归一。

## O6 (F3/F4/T1a/H3/H4/H5)
- F3:流式 stretch tile 输出与全帧逐元素相等(含 +Inf/−Inf/NaN→nodata)。master:tile 把 ±Inf 夹成 255/0。
- F4:lat=±90 sunPositionDeg 返回有限方位(180°/0°/270°,共享约定极区极限 az=acos(∓cosH))。master:NaN+true。
- T1a:harmonics=7 ≡ harmonics=6(逐系数相等、13 项);=0 ≡ =1(3 项)。master:=7 越界写 design[13..14];=0 读未初始化 design[1..2]。
- H3:reconstructionRmse(model,y,{})==0 且 ≡ {0,1,2,3}。master:返回全投影质量。
- H4:2e-7 尺度 2 波段单纯形 VCA k=2 提取成功。master:绝对 1e-12 门限拒绝。
- H5:宽度不一致 Table:digestHex 空、toJson 空。master:row[b] OOB 读(UB)。
