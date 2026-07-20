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
