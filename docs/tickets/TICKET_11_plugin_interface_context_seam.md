# Ticket TICKET-11: C++ Plugin Interface Context Seam

- **Type**: `task`
- **Status**: Closed
- **Parent Map**: [MAP_plugin_host_unification.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_plugin_host_unification.md)

## Question

How should `SicnuPluginInterface::initialize` and concrete plugin implementations be refactored to remove raw GUI widget pointers (`QgsMapCanvas*`, `QgsLayerTreeView*`) and operate headlessly?

## Resolution

### 1. Interface Signature Change (`src/core/interfaces/sicnu_plugin_interface.h`)
Forward-declare `class SicnuAppInterface;` and update the `initialize` method:
```cpp
class SicnuPluginInterface
{
public:
    virtual ~SicnuPluginInterface() = default;

    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const = 0;
    virtual QIcon icon() const = 0;

    // Headless-safe lifecycle initialization
    virtual bool initialize(SicnuAppInterface *iface) = 0;
    virtual void unload() = 0;

    virtual QWidget *createWidget(QWidget *parent = nullptr) { Q_UNUSED(parent); return nullptr; }
    virtual QList<QAction*> menuActions() { return {}; }
    virtual QList<QAction*> toolbarActions() { return {}; }
};
```

### 2. Concrete Adapter Updates
- **`PythonPluginAdapter`** (`src/python/isolated/python_plugin_adapter.h` & `.cpp`):
  Update `bool initialize(SicnuAppInterface *iface)` to bind `m_uiProxy` directly to `iface->projectContext()->dataManager()`, `iface->pluginMenu()`, and `iface->activeViewHost()`.
- **`LayerTreePlugin`** (`src/plugins/layer_tree/layer_tree_plugin.h` & `.cpp`):
  Update `bool initialize(SicnuAppInterface *iface)` to query view host state via `iface->activeViewHost()`.
- **`ProcessingPlugin`** (`src/plugins/processing/processing_plugin.h` & `.cpp`):
  Update `bool initialize(SicnuAppInterface *iface)` to register processing algorithms using `iface`.
