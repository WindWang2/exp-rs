# 性能优化设计方案

**日期**: 2026-06-20
**状态**: 已批准
**方案**: 混合方案（渐进式 + 流式处理关键路径）

---

## 1. 目标

- 支持中等规模（10000x10000, ~1GB）和大规模（50000x50000, ~10GB+）栅格处理
- 多核并行加速（Qt 线程池）
- 减少 I/O 等待时间
- 应用启动时间优化（混合策略：核心快速启动，高级功能延迟加载）

---

## 2. 分块并行处理

### 2.1 分块策略

- **块大小**: 256 行（可根据可用内存动态调整）
- **并行度**: `QThreadPool::globalInstance()->maxThreadCount()`
- **数据流**: 读取 → 处理 → 写入，流水线式

### 2.2 适用算法

| 算法 | 分块方式 | 注意事项 |
|------|----------|----------|
| 斑点滤波 (Lee/Frost/Kuan/Gamma) | 行分块 + 边界重叠 | 需要 kernelSize/2 的边界重叠 |
| 空间滤波 (Mean/Gaussian/Median/Sobel) | 行分块 + 边界重叠 | 同上 |
| 对比度拉伸 | 行分块 | 统计量需先全局计算 |
| 地形分析 (Slope/Aspect/Hillshade) | 行分块 + 边界重叠 | 3x3 窗口需要 1 行重叠 |

### 2.3 不适用算法

- PCA（需要全局协方差矩阵）
- 分类（需要全局训练数据）
- 镶嵌（需要多图层协调）
- 直方图均衡化（需要全局直方图）

### 2.4 实现接口

```cpp
// 新增：分块处理接口
class ChunkedProcessor {
public:
    struct Chunk {
        int startRow;
        int endRow;
        int overlapTop;    // 上边界重叠行数
        int overlapBottom; // 下边界重叠行数
    };

    // 处理单个块
    virtual void processChunk(const Chunk &chunk,
                              const float *input, float *output,
                              int width, int height) = 0;

    // 获取边界重叠行数
    virtual int overlapSize() const = 0;
};
```

---

## 3. GDAL 异步 I/O

### 3.1 双缓冲策略

- 处理块 A 时，预读块 B 到缓冲区
- 处理块 B 时，预读块 C 到缓冲区
- 使用 `QFutureWatcher` 监控预读完成

### 3.2 批量读取

- 同一算法的多次读取合并为一次 `GDALDataset::RasterIO`
- 使用 band map 数组一次读取多个波段
- 利用 GDAL 块缓存（默认 40MB）

### 3.3 写入优化

- 使用 GDAL 缓冲写入
- 大文件使用分块写入（避免一次性写入整行）

---

## 4. 模块延迟加载

### 4.1 延迟加载模块

| 模块 | 触发条件 | 初始化内容 |
|------|----------|------------|
| Python 控制台 | 首次打开 | 初始化 Python 解释器 |
| Georeferencer | 首次打开 | 加载 GCP 工具 |
| Classification | 首次打开 | 加载 OpenCV |
| OBIA | 首次打开 | 加载分割算法 |
| 处理历史 | 首次打开 | 查询数据库 |

### 4.2 实现方式

```cpp
// 使用 QScopedPointer + 懒初始化
QScopedPointer<SicnuPythonConsole> m_pythonConsole;

void QgisDesktopWindow::openPythonConsole()
{
    if (!m_pythonConsole) {
        statusBar()->showMessage(tr("Initializing Python..."));
        m_pythonConsole.reset(new SicnuPythonConsole(this));
        statusBar()->showMessage(tr("Python ready"), 3000);
    }
    m_pythonConsole->show();
}
```

### 4.3 预加载核心

- 项目管理（QgsProject）
- 图层树（QgsLayerTree）
- 地图画布（QgsMapCanvas）
- 处理框架（QgsProcessingRegistry）

---

## 5. 缓存增强

### 5.1 缓存层级

1. **算法结果缓存**（已有）：相同参数+输入 → 直接返回结果
2. **块级缓存**：分块处理的中间结果可复用
3. **统计数据缓存**：直方图、统计值等预计算结果

### 5.2 缓存策略

- LRU 淘汰策略（已有 maxSizeBytes）
- 缓存键：算法ID + 参数SHA256 + 输入文件修改时间
- 缓存失效：输入文件变化时自动失效

---

## 6. 实施顺序

1. **分块并行处理**（最高优先级）
   - 实现 ChunkedProcessor 接口
   - 优化斑点滤波（最常用的 SAR 处理）
   - 优化空间滤波

2. **GDAL 异步 I/O**
   - 实现双缓冲读取
   - 优化批量读取

3. **模块延迟加载**
   - Python 控制台延迟初始化
   - 其他模块按需加载

4. **缓存增强**
   - 块级缓存
   - 统计数据缓存

---

## 7. 预期收益

| 优化项 | 预期收益 |
|--------|----------|
| 分块并行处理 | 2-8x 多核加速 + 内存从 O(整图) 降到 O(分块) |
| GDAL 异步 I/O | I/O 等待减少 60% |
| 模块延迟加载 | 启动时间减少 40% |
| 缓存增强 | 重复操作秒级完成 |
