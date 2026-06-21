# 系统稳健性提升设计方案

**日期**: 2026-06-21
**状态**: 已批准
**方案**: 混合方案（关键路径集中式 + 其他渐进式）

---

## 1. 目标

- 提升系统级异常处理能力（GDAL I/O、内存、磁盘）
- 加强数据异常处理（输入验证、格式检查）
- 改善 UI 异常保护（对话框崩溃、异步操作）
- 增强扩展异常保护（Python 脚本、插件）

---

## 2. GDAL 操作统一错误处理

### 2.1 包装器设计

```cpp
// gdal_safe_call.h
#define GDAL_SAFE_CALL(expr, errorMsg) \
    do { \
        auto _result = (expr); \
        if (_result != CE_None) { \
            SICNU_LOG_ERROR(SicnuLogTags::GDAL, \
                QString("%1: %2").arg(errorMsg).arg(CPLGetLastErrorMsg())); \
            throw std::runtime_error(errorMsg); \
        } \
    } while(0)

// 使用示例
GDAL_SAFE_CALL(
    GDALRasterIO(band, GF_Read, 0, 0, w, h, buf, w, h, GDT_Float32, 0, 0),
    "Failed to read raster band"
);
```

### 2.2 覆盖范围

- `GDALOpen` / `GDALClose`
- `GDALRasterIO` (读/写)
- `GDALCreate` / `GDALGetRasterBand`
- `GDALSetGeoTransform` / `GDALSetProjection`

### 2.3 错误恢复

- 读取失败 → 返回空数据 + 日志
- 写入失败 → 清理临时文件 + 日志
- 打开失败 → 返回 nullptr + 用户提示

---

## 3. 算法输入验证框架

### 3.1 验证器设计

```cpp
// input_validator.h
class InputValidator {
public:
    static bool validateRasterLayer(QgsRasterLayer *layer, QString &error);
    static bool validateRasterDimensions(int width, int height, QString &error);
    static bool validateBandIndex(int band, int maxBands, QString &error);
    static bool validateOutputPath(const QString &path, QString &error);
    static bool validateNumericRange(double value, double min, double max, QString &error);
};
```

### 3.2 验证规则

| 输入类型 | 验证规则 | 错误信息 |
|----------|----------|----------|
| 栅格图层 | 非空、有效尺寸、波段数匹配 | "Raster layer is null" |
| 矢量图层 | 非空、几何类型匹配 | "Vector layer has no features" |
| 数值参数 | 范围检查、NaN 检查 | "Value -1 out of range [0, 100]" |
| 文件路径 | 存在性、可写性、格式正确 | "Output path not writable" |

### 3.3 使用方式

```cpp
QVariantMap MyAlgorithm::processAlgorithm(...) {
    QString error;
    if (!InputValidator::validateRasterLayer(inputLayer, error)) {
        throw QgsProcessingException(error);
    }
    // ... 正常处理
}
```

---

## 4. UI 异常保护

### 4.1 SafeDialog 基类

```cpp
// safe_dialog.h
class SafeDialog : public QDialog {
protected:
    int safeExec() {
        try {
            return exec();
        } catch (const std::exception &e) {
            QMessageBox::critical(this, tr("Error"), e.what());
            return QDialog::Rejected;
        }
    }
};
```

### 4.2 异步操作保护

```cpp
// 使用 QPointer 检查 widget 生命周期
QPointer<QDialog> safeThis = this;
QgsApplication::taskManager()->addTask(task);
connect(task, &QgsTask::taskCompleted, this, [safeThis, task]() {
    if (!safeThis) return; // Widget 已销毁
    // ... 处理结果
});
```

### 4.3 长时间操作保护

- 显示进度条和取消按钮
- 使用 `QgsProcessingAlgRunnerTask` 异步执行
- 操作期间禁用相关控件

---

## 5. 扩展异常保护

### 5.1 Python 脚本保护

```cpp
bool QgisPython::runString(const QString &command, QString &error) {
    // 包装 Python 执行，捕获所有异常
    QString wrappedCmd = QString(
        "try:\n"
        "    %1\n"
        "except Exception as e:\n"
        "    sicnu_print(f'ERROR: {e}')\n"
    ).arg(command);
    // ... 执行 wrappedCmd
}
```

### 5.2 插件加载保护

```cpp
void PluginManager::loadPlugins(const QString &dir) {
    for (const QString &pluginPath : pluginFiles) {
        try {
            loadPlugin(pluginPath);
        } catch (const std::exception &e) {
            SICNU_LOG_ERROR("Main", QString("Plugin load failed: %1 - %2")
                .arg(pluginPath).arg(e.what()));
            // 继续加载其他插件
        }
    }
}
```

### 5.3 MCP 请求保护

```cpp
void McpServer::handleRequest(const QVariantMap &request) {
    try {
        // ... 处理请求
    } catch (const std::exception &e) {
        sendError(id, -32603, e.what());
    }
}
```

---

## 6. 实施顺序

1. **GDAL 操作统一错误处理**（最高优先级）
   - 创建 `GdalSafeCall` 宏
   - 更新关键 GDAL 操作

2. **算法输入验证框架**
   - 创建 `InputValidator` 类
   - 更新关键算法

3. **UI 异常保护**
   - 创建 `SafeDialog` 基类
   - 更新关键对话框

4. **扩展异常保护**
   - Python 脚本保护
   - 插件加载保护

---

## 7. 预期收益

| 优化项 | 预期收益 |
|--------|----------|
| GDAL 错误处理 | 防止静默失败，提升数据完整性 |
| 输入验证 | 防止垃圾数据，提升算法可靠性 |
| UI 异常保护 | 防止界面崩溃，提升用户体验 |
| 扩展异常保护 | 防止扩展崩溃影响主程序 |
