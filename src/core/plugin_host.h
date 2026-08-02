// src/core/plugin_host.h
#pragma once

#include <QObject>
#include <QMap>
#include <QStringList>
#include <memory>
#include "interfaces/sicnu_plugin_interface.h"

class QPluginLoader;
class SicnuAppInterface;
class PythonPluginAdapter;

namespace sicnu::python::isolated {
    class PythonPluginHost;
}

/**
 * @brief GUI-free unified owner of C++ and Python plugin lifecycles.
 *
 * Manages plugin discovery, dynamic loading via QPluginLoader (C++) and
 * out-of-process process pool execution via PythonPluginHost (Python).
 * Operates headlessly using SicnuAppInterface facade without raw GUI widgets.
 */
class PluginHost : public QObject
{
    Q_OBJECT

public:
    static constexpr int DEFAULT_PYTHON_POOL_SIZE = 2;

    explicit PluginHost(int pythonPoolSize = DEFAULT_PYTHON_POOL_SIZE, QObject *parent = nullptr);
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
    int m_pythonPoolSize = DEFAULT_PYTHON_POOL_SIZE;
    std::unique_ptr<sicnu::python::isolated::PythonPluginHost> m_pythonHost;
};
