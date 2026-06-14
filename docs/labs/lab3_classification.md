# 实验3：遥感影像分类

## 实验目的
1. 理解监督分类和非监督分类的原理
2. 掌握训练样本的选取方法
3. 学会使用不同分类器进行影像分类
4. 掌握分类精度评价方法

## 实验数据
- `samples_data/landsat_sample.tif` (7波段Landsat-like影像)
- `samples_data/training_samples.shp` (训练样本ROI)

## 实验步骤

### 3.1 加载数据
1. 启动 SICNU GEO RS
2. 菜单: File > Add Raster Layer... → 选择 `landsat_sample.tif`
3. 菜单: File > Add Vector Layer... → 选择 `training_samples.shp`

### 3.2 查看训练样本
1. 在图层面板中选择 `training_samples` 图层
2. 右键选择 "Open Attribute Table"
3. 查看样本类别:
   - Water (水体)
   - Vegetation (植被)
   - Urban (城市)
   - Bare Soil (裸土)
   - Forest (森林)

### 3.3 监督分类 - 最大似然法
1. 菜单: Raster > Classification...
2. 选择输入影像: `landsat_sample.tif`
3. 选择训练样本: `training_samples.shp`
4. 选择分类器: **NormalBayes (最大似然)**
5. 设置输出文件路径
6. 点击 "Run"
7. 查看分类结果

### 3.4 监督分类 - SVM
1. 重复上述步骤
2. 选择分类器: **SVM (支持向量机)**
3. 观察SVM分类结果与最大似然法的差异

### 3.5 非监督分类 - K-Means
1. 菜单: Raster > Classification...
2. 选择输入影像: `landsat_sample.tif`
3. 选择分类器: **K-Means**
4. 设置分类数量: 5 (对应5种地物)
5. 设置输出文件路径
6. 点击 "Run"
7. 查看分类结果

### 3.6 精度评价
1. 在分类窗口中点击 "Accuracy Assessment"
2. 查看混淆矩阵:
   - 总体精度 (Overall Accuracy)
   - Kappa系数
   - 各类别的生产者精度和用户精度
3. 导出精度报告 (CSV格式)

### 3.7 交叉验证
1. 在分类窗口中点击 "Cross Validation"
2. 系统自动进行5折交叉验证
3. 查看平均精度和标准差

### 3.8 结果分析
1. 比较不同分类器的结果
2. 分析混淆矩阵，找出混淆最多的类别
3. 讨论训练样本质量对分类精度的影响

## 思考题
1. 监督分类和非监督分类各有什么优缺点？
2. 如何选择合适的训练样本？
3. Kappa系数与总体精度相比有什么优势？
4. 如何提高分类精度？

## 预期结果
- 最大似然法: 适合正态分布数据，精度较高
- SVM: 适合高维数据，泛化能力强
- K-Means: 非监督方法，结果需要后处理
- 混淆矩阵可以揭示分类错误的模式
