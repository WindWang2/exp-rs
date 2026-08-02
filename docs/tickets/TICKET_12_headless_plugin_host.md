# Ticket TICKET-12: Headless PluginHost Core Module

- **Type**: `task`
- **Status**: Closed
- **Parent Map**: [MAP_plugin_host_unification.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_plugin_host_unification.md)

## Question

How should `PluginHost` in `src/core/plugin_host.h` be implemented to handle GUI-free discovery and loading of C++ libraries and Python plugins?

## Resolution

### 1. Header Specification (`src/core/plugin_host.h`)
```cpp
#pragma once

#include <QObject>
#include <QMap>
#include <QStringList>
#include <memory>
#include "interfaces/sicnu_plugin_interface.h"

class QPluginLoader;
class SicnuAppInterface;

namespace sicnu::python::isolated {
    class PythonPluginHost;
}

class PluginHost : public QObject
{
    Q_OBJECT

public:
    explicit PluginHost(int pythonPoolSize = 2, QObject *parent = nullptr);
    ~PluginHost() override;

    void setAppInterface(SicnuAppInterface *iface) { m_appInterface = iface; }
    SicnuAppInterface *appInterface() const { return m_appInterface; }

    sicnu::python::isolated::PythonPluginHost *pythonPluginHost() const { return m_pythonHost.get(); }

    void loadPlugins(const QString &pluginDir);
    bool loadPlugin(const QString &pluginPath);
    bool loadPythonPlugin(const QString &pluginDir);
    void unloadAll();

    QStringList loadedPlugins() const;
    SicnuPluginInterface* plugin(const QString &name) const;
    bool isPluginLoaded(const QString &name) const;

signals:
    void pluginLoaded(const QString &name);
    void pluginUnloaded(const QString &name);
    void pluginError(const QString &name, const QString &error);

private:
    struct PluginInfo {
        SicnuPluginInterface *instance = nullptr;
        QPluginLoader *loader = nullptr; // nullptr for Python plugins
        bool loaded = false;
        bool isPython = false;
    };

    QMap<QString, PluginInfo> m_plugins;
    SicnuAppInterface *m_appInterface = nullptr;
    std::unique_ptr<sicnu::python::isolated::PythonPluginHost> m_pythonHost;
};
```

### 2. Auto-Discovery Logic (`src/core/plugin_host.cpp`)
- `loadPlugins(dir)` scans files for `QLibrary::isLibrary(filePath)` (C++ `.so`/`.dll`) and subdirectories containing `metadata.txt` + `__init__.py` (Python plugins).
- `loadPlugin(path)` loads C++ plugins via `QPluginLoader`, casts to `SicnuPluginInterface`, and calls `interface->initialize(m_appInterface)`.
- `loadPythonPlugin(dir)` delegates to `m_pythonHost->loadPlugin(dir, dataManager, pluginMenu, activeViewHost, &error)` and stores the resulting `PythonPluginAdapter*` in `m_plugins`.
