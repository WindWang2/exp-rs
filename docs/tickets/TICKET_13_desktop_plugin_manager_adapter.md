# Ticket TICKET-13: Desktop PluginManager UI Adapter

- **Type**: `task`
- **Status**: Closed
- **Parent Map**: [MAP_plugin_host_unification.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_plugin_host_unification.md)

## Question

How should `PluginManager` in `src/app` be refactored into a thin UI adapter decorating `PluginHost` with main window menu bar and action injections?

## Resolution

### 1. Refactored `PluginManager` (`src/app/plugin_manager.h` & `.cpp`)
`PluginManager` no longer accepts `QgsMapCanvas*` or `QgsLayerTreeView*`. Its constructor takes `(SicnuAppInterface *iface, QObject *parent)`:
```cpp
class PluginManager : public QObject
{
    Q_OBJECT
public:
    explicit PluginManager(SicnuAppInterface *iface, QObject *parent = nullptr);

    PluginHost *host() const { return m_host.get(); }

    void loadPlugins(const QString &pluginDir);
    void unloadAll();

signals:
    void pluginLoaded(const QString &name);
    void pluginUnloaded(const QString &name);
    void pluginError(const QString &name, const QString &error);

private:
    SicnuAppInterface *m_iface = nullptr;
    std::unique_ptr<PluginHost> m_host;
};
```

### 2. Desktop Window Wiring (`src/app/main_window.cpp`)
In `QgisDesktopWindow::setupPlugins()`, update initialization:
```cpp
m_pluginManager = std::make_unique<PluginManager>(m_appInterface.get());
```
When a plugin loads, `PluginManager` automatically wires `interface->menuActions()` into the main window plugin menu (`m_appInterface->pluginMenu()`) and registers dock widgets/actions without `PluginHost` knowing about Qt desktop widgets.
