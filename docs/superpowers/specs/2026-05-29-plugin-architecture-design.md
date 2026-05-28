# SICNU GEO RS: 插件化架构设计规范

**生成时间**: 2026-05-29
**分支**: feat/p3-gui
**状态**: 设计完成

---

## 问题陈述

当前 SICNU GEO RS 应用程序存在以下架构问题：

1. **main.cpp 单文件 1057 行** - 所有类内联定义，可维护性差
2. **功能模块耦合** - 图层树、处理工具箱、Python 控制台等功能紧密耦合
3. **扩展性差** - 添加新功能需要修改主程序代码
4. **代码复用困难** - 功能模块无法独立使用

**目标**: 重构为插件化架构，实现模块化、可扩展、易维护的代码结构。

---

## 设计方案

### 1. 核心接口定义

```cpp
// src/core/interfaces/sicnu_plugin_interface.h
#pragma once

#include <QString>
#include <QWidget>
#include <QIcon>

class QgsMapCanvas;
class QgsLayerTreeView;

class SicnuPluginInterface
{
public:
    virtual ~SicnuPluginInterface() = default;
    
    // 插件基本信息
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const = 0;
    virtual QIcon icon() const = 0;
    
    // 生命周期
    virtual bool initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree) = 0;
    virtual void unload() = 0;
    
    // UI 组件（可选）
    virtual QWidget *createWidget(QWidget *parent = nullptr) { Q_UNUSED(parent); return nullptr; }
    
    // 菜单/工具栏贡献（可选）
    virtual QList<QAction*> menuActions() { return {}; }
    virtual QList<QAction*> toolbarActions() { return {}; }
};

// 声明接口，用于 Qt 插件系统
#define SicnuPluginInterface_iid "org.sicnu.SicnuPluginInterface/1.0"
Q_DECLARE_INTERFACE(SicnuPluginInterface, SicnuPluginInterface_iid)
```

### 2. 插件管理器

```cpp
// src/core/plugin_manager.h
#pragma once

#include <QObject>
#include <QMap>
#include <QDir>
#include "interfaces/sicnu_plugin_interface.h"

class QgsMapCanvas;
class QgsLayerTreeView;

class PluginManager : public QObject
{
    Q_OBJECT

public:
    explicit PluginManager(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree, QObject *parent = nullptr);
    
    // 插件加载
    void loadPlugins(const QString &pluginDir = QString());
    bool loadPlugin(const QString &pluginPath);
    void unloadAll();
    
    // 插件查询
    QStringList loadedPlugins() const;
    SicnuPluginInterface* plugin(const QString &name) const;
    
    // 插件状态
    bool isPluginLoaded(const QString &name) const;
    
signals:
    void pluginLoaded(const QString &name);
    void pluginUnloaded(const QString &name);
    void pluginError(const QString &name, const QString &error);

private:
    struct PluginInfo {
        SicnuPluginInterface *instance;
        QPluginLoader *loader;
        bool loaded;
    };
    
    QMap<QString, PluginInfo> m_plugins;
    QgsMapCanvas *m_canvas;
    QgsLayerTreeView *m_layerTree;
};
```

### 3. 目录结构

```
src/
├── core/                    # 核心库（共享）
│   ├── interfaces/          # 插件接口定义
│   │   └── sicnu_plugin_interface.h
│   ├── plugin_manager.h     # 插件管理器
│   ├── plugin_manager.cpp
│   ├── project.h            # 项目管理
│   ├── project.cpp
│   ├── layer_manager.h      # 图层管理
│   └── layer_manager.cpp
├── gui/                     # GUI 库（共享）
│   ├── main_window.h        # 主窗口框架
│   ├── main_window.cpp
│   ├── map_canvas.h         # 地图画布
│   └── map_canvas.cpp
├── plugins/                 # 插件目录
│   ├── layer_tree/          # 图层树插件
│   │   ├── layer_tree_plugin.h
│   │   ├── layer_tree_plugin.cpp
│   │   └── CMakeLists.txt
│   ├── processing/          # 处理工具箱插件
│   │   ├── processing_plugin.h
│   │   ├── processing_plugin.cpp
│   │   └── CMakeLists.txt
│   ├── python_console/      # Python 控制台插件
│   │   ├── python_plugin.h
│   │   ├── python_plugin.cpp
│   │   └── CMakeLists.txt
│   └── browser/             # 浏览器面板插件
│       ├── browser_plugin.h
│       ├── browser_plugin.cpp
│       └── CMakeLists.txt
└── app/                     # 主应用程序
    ├── main.cpp             # 精简的 main 函数
    └── CMakeLists.txt
```

### 4. 主窗口重构

```cpp
// src/gui/main_window.h
#pragma once

#include <QMainWindow>
#include <QMap>

class QgsMapCanvas;
class QgsLayerTreeView;
class PluginManager;
class QgsDockWidget;

class SicnuMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SicnuMainWindow(QWidget *parent = nullptr);
    ~SicnuMainWindow();
    
    // 初始化
    void initialize();
    
    // 插件集成
    void addPluginDockWidget(const QString &pluginName, QgsDockWidget *dock);
    void addPluginMenu(const QString &pluginName, QMenu *menu);
    void addPluginToolbar(const QString &pluginName, QToolBar *toolbar);
    
    // 核心组件访问
    QgsMapCanvas *mapCanvas() const { return m_mapCanvas; }
    QgsLayerTreeView *layerTreeView() const { return m_layerTree; }
    PluginManager *pluginManager() const { return m_pluginManager; }

private:
    void setupUi();
    void setupMenuBar();
    void setupStatusBar();
    void loadPlugins();
    
    // 核心组件
    QgsMapCanvas *m_mapCanvas;
    QgsLayerTreeView *m_layerTree;
    PluginManager *m_pluginManager;
    
    // UI 容器
    QWidget *m_centralWidget;
    QMap<QString, QgsDockWidget*> m_pluginDocks;
    QMap<QString, QMenu*> m_pluginMenus;
    QMap<QString, QToolBar*> m_pluginToolbars;
};
```

### 5. 插件示例：图层树插件

```cpp
// src/plugins/layer_tree/layer_tree_plugin.h
#pragma once

#include <QObject>
#include "core/interfaces/sicnu_plugin_interface.h"

class QgsLayerTreeView;
class QgsLayerTreeModel;

class LayerTreePlugin : public QObject, public SicnuPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SicnuPluginInterface_iid)
    Q_INTERFACES(SicnuPluginInterface)

public:
    explicit LayerTreePlugin(QObject *parent = nullptr);
    
    // SicnuPluginInterface 实现
    QString name() const override { return "LayerTree"; }
    QString description() const override { return "Layer tree panel with context menu"; }
    QString version() const override { return "1.0.0"; }
    QIcon icon() const override;
    
    bool initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree) override;
    void unload() override;
    
    QWidget *createWidget(QWidget *parent) override;
    QList<QAction*> menuActions() override;

private:
    QgsLayerTreeView *m_layerTree;
    QgsLayerTreeModel *m_model;
};
```

### 6. 精简的 main.cpp

```cpp
// src/app/main.cpp
#include <QApplication>
#include <QgsApplication.h>
#include "gui/main_window.h"

int main(int argc, char *argv[])
{
    // 创建 QGIS 应用程序
    QgsApplication *app = new QgsApplication(argc, argv, true);
    app->setApplicationName("SICNU GEO RS");
    app->setApplicationVersion("2.0");
    app->setOrganizationName("SICNU");
    
    // 设置前缀路径并初始化
    QgsApplication::setPrefixPath("/home/kevin/projects/exp-rs", true);
    QgsApplication::initQgis();
    
    // 创建主窗口
    SicnuMainWindow window;
    window.initialize();
    window.show();
    
    int result = app->exec();
    
    // 清理
    delete app;
    return result;
}
```

### 7. CMake 构建系统

```cmake
# CMakeLists.txt（根目录）
cmake_minimum_required(VERSION 3.16)
project(SICNU_GEO_RS VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(QGIS REQUIRED)

# 核心库（共享）
add_subdirectory(src/core)

# GUI 库（共享）
add_subdirectory(src/gui)

# 插件目录
add_subdirectory(src/plugins)

# 主应用程序
add_subdirectory(src/app)
```

```cmake
# src/core/CMakeLists.txt
add_library(sicnu_core SHARED
    plugin_manager.cpp
    project.cpp
    layer_manager.cpp
)

target_include_directories(sicnu_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sicnu_core PUBLIC Qt6::Core Qt6::Gui QGIS::Core)
```

```cmake
# src/gui/CMakeLists.txt
add_library(sicnu_gui SHARED
    main_window.cpp
    map_canvas.cpp
)

target_include_directories(sicnu_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sicnu_gui PUBLIC sicnu_core Qt6::Widgets QGIS::Gui)
```

```cmake
# src/plugins/layer_tree/CMakeLists.txt
add_library(layer_tree_plugin SHARED
    layer_tree_plugin.cpp
)

target_link_libraries(layer_tree_plugin PUBLIC
    sicnu_core
    sicnu_gui
    Qt6::Widgets
    QGIS::Gui
)

# 安装插件到 plugins 目录
install(TARGETS layer_tree_plugin
    LIBRARY DESTINATION plugins
)
```

### 8. 插件加载流程

```cpp
// src/core/plugin_manager.cpp
void PluginManager::loadPlugins(const QString &pluginDir)
{
    QDir dir(pluginDir);
    if (!dir.exists()) {
        qWarning() << "Plugin directory not found:" << pluginDir;
        return;
    }
    
    // 扫描插件目录
    for (const QString &fileName : dir.entryList(QDir::Files)) {
        QString filePath = dir.absoluteFilePath(fileName);
        
        // 尝试加载插件
        QPluginLoader loader(filePath);
        QJsonObject metadata = loader.metaData();
        
        if (metadata.contains("MetaData")) {
            QJsonObject metaData = metadata["MetaData"].toObject();
            QString pluginName = metaData["name"].toString();
            
            if (loadPlugin(filePath)) {
                emit pluginLoaded(pluginName);
            }
        }
    }
}

bool PluginManager::loadPlugin(const QString &pluginPath)
{
    QPluginLoader loader(pluginPath);
    QObject *plugin = loader.instance();
    
    if (!plugin) {
        emit pluginError(pluginPath, loader.errorString());
        return false;
    }
    
    SicnuPluginInterface *interface = qobject_cast<SicnuPluginInterface*>(plugin);
    if (!interface) {
        emit pluginError(pluginPath, "Plugin does not implement SicnuPluginInterface");
        return false;
    }
    
    // 初始化插件
    if (!interface->initialize(m_canvas, m_layerTree)) {
        emit pluginError(interface->name(), "Plugin initialization failed");
        return false;
    }
    
    // 保存插件信息
    PluginInfo info;
    info.instance = interface;
    info.loader = &loader;
    info.loaded = true;
    m_plugins[interface->name()] = info;
    
    return true;
}
```

---

## 实施计划

### 阶段 1：基础设施（1-2 天）
1. 创建新的目录结构
2. 定义插件接口 `SicnuPluginInterface`
3. 实现插件管理器 `PluginManager`
4. 重构主窗口 `SicnuMainWindow` 框架

### 阶段 2：核心插件迁移（2-3 天）
1. 将图层树功能迁移到 `LayerTreePlugin`
2. 将处理工具箱迁移到 `ProcessingPlugin`
3. 将 Python 控制台迁移到 `PythonConsolePlugin`
4. 实现浏览器面板 `BrowserPlugin`

### 阶段 3：清理和优化（1 天）
1. 移除旧的 main.cpp 中的内联代码
2. 更新 CMake 构建系统
3. 测试所有插件加载和功能
4. 更新文档

### 阶段 4：扩展功能（可选）
1. 实现插件热加载/卸载
2. 添加插件配置界面
3. 实现插件依赖管理

---

## 关键设计决策

1. **插件接口版本控制**：使用 `Q_DECLARE_INTERFACE` 和版本号
2. **插件间通信**：通过信号槽机制，避免直接依赖
3. **错误处理**：插件加载失败不应影响主程序
4. **性能考虑**：插件延迟加载，只在需要时初始化
5. **线程安全**：插件在主线程初始化，渲染相关操作可使用后台线程

---

## 验证标准

1. **功能完整性**：所有现有功能必须正常工作
2. **插件加载**：插件可以动态加载和卸载
3. **错误隔离**：单个插件失败不影响其他插件
4. **性能影响**：插件架构不应显著影响性能
5. **代码质量**：代码结构清晰，易于理解和维护

---

## 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 插件接口设计不合理 | 高 | 充分测试接口，预留扩展点 |
| 插件加载失败 | 中 | 实现错误处理和回退机制 |
| 性能下降 | 中 | 延迟加载，性能测试 |
| 代码迁移错误 | 高 | 充分测试，保持功能一致性 |
