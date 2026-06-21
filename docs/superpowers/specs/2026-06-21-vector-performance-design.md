# 矢量性能优化设计方案

**日期**: 2026-06-21
**状态**: 已批准
**方案**: 混合方案（空间索引 + 批量处理 + 几何简化 + 并行查询）

---

## 1. 目标

- 支持大量简单要素（10万+）的高效处理
- 支持复杂几何要素（多拐点面）的高效处理
- 优化几何运算、空间查询、属性查询、渲染显示

---

## 2. 空间索引优化

### 2.1 预构建空间索引

```cpp
// 打开图层时自动构建索引
QgsSpatialIndex index;
QgsFeatureIterator iter = layer->getFeatures();
QgsFeature feat;
while (iter.nextFeature(feat)) {
    index.insertFeature(feat);
}
```

### 2.2 索引查询

```cpp
// 空间查询使用索引
QgsRectangle searchRect(x - radius, y - radius, x + radius, y + radius);
QgsFeatureIds candidates = index.intersects(searchRect);

// 只对候选要素做精确几何判断
for (QgsFeatureId id : candidates) {
    QgsFeature feat = layer->getFeature(id);
    if (feat.geometry().intersects(queryGeom)) {
        // 精确匹配
    }
}
```

### 2.3 缓存策略

- 索引构建结果缓存（图层不变则不重建）
- 空间查询结果缓存（相同查询返回缓存）

---

## 3. 批量要素处理

### 3.1 分批读取

```cpp
QgsFeatureRequest request;
request.setLimit(1000); // 每次读取 1000 个要素

QgsFeatureIterator iter = layer->getFeatures(request);
QgsFeature feat;
while (iter.nextFeature(feat)) {
    // 处理要素
}
```

### 3.2 批量几何运算

```cpp
// 一次调用处理多个要素
QgsGeometry unionGeom = geometries[0];
for (int i = 1; i < geometries.size(); i++) {
    unionGeom = unionGeom.combine(geometries[i]);
}
```

### 3.3 内存优化

- 流式处理大矢量（不一次性加载所有要素）
- 使用 `QgsFeatureIterator` 而非 `getFeatures()` 返回列表
- 使用 `QgsFeatureRequest::setFlags(QgsFeatureRequest::NoGeometry)` 获取纯属性

---

## 4. 几何简化预处理

### 4.1 简化策略

```cpp
// 对多拐点几何先做简化
QgsGeometry simplified = geometry.simplify(tolerance);

// 简化后的几何用于空间查询
if (simplified.intersects(queryGeom)) {
    // 精确几何用于最终结果
    if (geometry.intersects(queryGeom)) {
        // 精确匹配
    }
}
```

### 4.2 容差策略

| 场景 | 容差 | 说明 |
|------|------|------|
| 显示用 | 较大容差 | 快速渲染 |
| 分析用 | 较小容差 | 精确计算 |
| 默认 | 自动选择 | 根据 CRS 和比例尺 |

### 4.3 适用操作

- `buffer()` — 简化后 buffer 再恢复精度
- `intersection()` — 简化后求交再精确计算
- `contains()` — 简化后判断再精确验证

---

## 5. 并行属性查询

### 5.1 并行统计

```cpp
// 并行计算属性统计
QFuture<double> sumFuture = QtConcurrent::run([layer, fieldName]() -> double {
    double sum = 0;
    QgsFeatureIterator iter = layer->getFeatures();
    QgsFeature feat;
    while (iter.nextFeature(feat)) {
        sum += feat.attribute(fieldName).toDouble();
    }
    return sum;
});
```

### 5.2 并行筛选

```cpp
// 并行筛选要素
QFuture<QgsFeatureIds> filterFuture = QtConcurrent::run([layer, filter]() -> QgsFeatureIds {
    QgsFeatureIds result;
    QgsFeatureIterator iter = layer->getFeatures();
    QgsFeature feat;
    while (iter.nextFeature(feat)) {
        if (filter(feat)) {
            result.insert(feat.id());
        }
    }
    return result;
});
```

### 5.3 线程安全

- 只读操作天然线程安全
- 写操作需要同步（使用 QMutex）
- QgsFeatureIterator 不是线程安全的，每个线程需要独立的迭代器

---

## 6. 实施顺序

1. **空间索引优化**（最高优先级）
   - 自动构建空间索引
   - 空间查询使用索引

2. **批量要素处理**
   - 分批读取要素
   - 批量几何运算

3. **几何简化预处理**
   - 复杂几何简化
   - 简化后精确验证

4. **并行属性查询**
   - 并行统计
   - 并行筛选

---

## 7. 预期收益

| 优化项 | 预期收益 |
|--------|----------|
| 空间索引优化 | 空间查询 10-100x 加速 |
| 批量要素处理 | 迭代器开销减少 50% |
| 几何简化预处理 | 几何运算 2-5x 加速 |
| 并行属性查询 | 多核加速 |
