# 遥感导论实验指南 — SICNU GEO RS

本系统为遥感导论本科生实验提供完整的遥感影像处理工作流。

## 实验数据

所有实验数据位于 `data/samples/` 目录：

| 文件 | 说明 | 用途 |
|------|------|------|
| `landsat_sample.tif` | 7波段类Landsat影像 (256×256) | 光谱分析、分类、指数计算 |
| `dem_sample.tif` | 数字高程模型 (256×256) | 地形分析 |
| `change_before.tif` | 变化前影像 | 变化检测 |
| `change_after.tif` | 变化后影像 | 变化检测 |
| `training_samples.shp` | 训练样本ROI | 监督分类 |

## 实验列表

### 实验1：影像增强与空间滤波
- 对比度拉伸（线性、百分比截断、标准差）
- 直方图均衡化
- 空间滤波（均值、高斯、中值、Sobel、Laplacian）
- **菜单**: Raster > Enhancement

### 实验2：光谱指数与波段运算
- NDVI植被指数计算
- 自定义波段运算（Band Math）
- 光谱曲线分析
- **菜单**: Raster > Vegetation Index, Raster > Band Math

### 实验3：遥感影像分类
- 监督分类（最大似然、SVM）
- 非监督分类（K-Means）
- 精度评价（混淆矩阵、Kappa）
- **菜单**: Raster > Classification

### 实验4：变化检测
- 影像差值法
- 归一化差异
- 变化掩膜生成
- **菜单**: Raster > Change Detection

### 实验5：地形分析
- 坡度计算
- 坡向计算
- 山体阴影
- **菜单**: Raster > Terrain Analysis

### 实验6：影像配准（几何校正）
- GCP选点
- 多项式变换
- 影像重采样
- **菜单**: Raster > Georeferencer

### 实验7：影像融合
- Brovey融合
- PCA融合
- IHS融合
- **菜单**: Raster > Image Fusion

## 实验列表（能力扩展：D3 轨道新增）

实验 8–11 把平台时序 / SAR / 高光谱 / 制图能力引入课堂。每个实验除 GUI 步骤外，
还提供 **headless 可复现管道**（`data/labs/pipelines/*.pipeline.json`），通过
`sicnu_geo_rs_cli --pipeline` 离屏运行；数据规格（供 D1 生成教学数据）与判分意图
（判分器由 D4 实现）分别在 `data/labs/data-specs/` 与 `data/labs/grading/`。

### 实验8：NDVI 时序分析——趋势、物候与异常检测
- NDVI 时序栈（12 期，获取日期元数据硬依赖）
- 线性趋势（斜率单位 NDVI/天）
- 物候参数（SOS/POS/EOS/生长季长度，阈值交叉法）
- 基线 z-score 异常检测（扰动时相 + 扰动前对照）
- **算子**: `rs:temporal_summary` / `rs:temporal_index_series` / `rs:temporal_trend` / `rs:temporal_phenology` / `rs:temporal_anomaly`
- **文档**: [lab8_temporal_analysis.md](lab8_temporal_analysis.md)

### 实验9：SAR 相干斑抑制与变化检测
- DN → σ0 辐射定标（同校准常数配对）
- Lee 滤波与等效视数（ENL）评价
- dB 对数比 + Otsu 阈值变化检测（检出率/虚警率）
- #785 视角几何与 #803 多波段 NoData 两个已修缺陷的回归性教学验证
- **算子**: `rs:sar_calibrate` / `rs:sar_speckle` / `rs:sar_change` / `rs:sar_terrain_correction`
- **文档**: [lab9_sar_processing.md](lab9_sar_processing.md)

### 实验10：高光谱分析——MNF、PPI、SAM/SID 与线性解混
- MNF 降维（SNR 有序分量与有效维数）
- PPI 端元提取（与光谱库对照）
- SAM 与 SID 光谱匹配分类（精度对比）
- 线性解混（丰度 + 重构误差）
- **算子**: `rs:mnf` / `rs:endmember_extraction` / `rs:sam_classify` / `rs:spectral_unmixing`
- **文档**: [lab10_hyperspectral_analysis.md](lab10_hyperspectral_analysis.md)

### 实验11：制图出图——专题数据生产与合规地图排版
- 年内均值合成 + Otsu 专题分级（可复现数据链）
- MapSpec 声明式排版（spec_version 5）
- 合规五要素：标题/图例/比例尺/指北针/来源注记（preflight 规则）
- 交付治理：output 只声明、导出必须显式（cartography:export）
- **算子/工具**: `rs:temporal_composite` / `rs:threshold_raster` + `cartography:compose` / `cartography:export`
- **文档**: [lab11_cartographic_mapping.md](lab11_cartographic_mapping.md)

## 数据说明

### landsat_sample.tif 波段说明
| 波段 | 名称 | 波长范围(μm) | 用途 |
|------|------|-------------|------|
| 1 | Coastal/Aerosol | 0.43-0.45 | 气溶胶检测 |
| 2 | Blue | 0.45-0.51 | 水体、植被 |
| 3 | Green | 0.53-0.59 | 植被健康 |
| 4 | Red | 0.64-0.67 | 植被、土壤 |
| 5 | NIR | 0.85-0.88 | 植被、水体 |
| 6 | SWIR1 | 1.57-1.65 | 土壤、水分 |
| 7 | SWIR2 | 2.11-2.29 | 岩石、土壤 |

### 地物类型
| 类别 | 光谱特征 |
|------|----------|
| 水体 | 各波段反射率低，NIR最低 |
| 植被 | 红光低、NIR高（红边效应） |
| 城市 | 各波段中等反射率 |
| 裸土 | 随波长增加反射率上升 |
| 森林 | 类似植被但NIR更高 |
| 阴影 | 各波段极低反射率 |

## Headless 验证（实验 8–11）

```bash
# 1) 生成本地临时数据（gitignored，实验环境由 D1 提供等价数据）
python3 scripts/gen_lab_fixtures.py temporal      --out data/labs/_tmp
python3 scripts/gen_lab_fixtures.py sar           --out data/labs/_tmp
python3 scripts/gen_lab_fixtures.py hyperspectral --out data/labs/_tmp

# 2) 运行四条管道（离屏；先构建 sicnu_geo_rs_cli）
QT_QPA_PLATFORM=offscreen python3 scripts/run_lab_pipelines.py

# 3) 对照判分意图校验产物
python3 scripts/verify_lab_outputs.py
```

LabSpec 规格：`data/labs/*.labspec.json`（`data/labs/labspec.schema.json` 校验）；
实验 11 的排版出图另见 `data/labs/mapspecs/` 与 `scripts/export_lab_map_mcp.py`。
