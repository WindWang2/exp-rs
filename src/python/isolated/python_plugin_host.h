// src/python/isolated/python_plugin_host.h
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

#include "plugin_load_context.h"

class QMenu;
class ActiveViewHost;
class PythonPluginAdapter;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::python::isolated
{

class PythonWorkerProcessPool;

/**
 * @brief GUI-free lifecycle owner for Python plugins (ADR 0023).
 *
 * Owns the PythonWorkerProcessPool and the full plugin lifecycle: metadata
 * parse, adapter creation, worker acquisition, classFactory init, and unload.
 * Headless surfaces (CLI, later MCP) consume it directly; PluginHost
 * (src/core) composes it, and the desktop PluginManager only wraps PluginHost.
 * The host holds no
 * loading policy — callers decide which plugin directories to load.
 */
class PythonPluginHost : public QObject
{
  Q_OBJECT

  public:
    explicit PythonPluginHost( int poolSize = 2, QObject *parent = nullptr );
    ~PythonPluginHost() override;

    /**
     * Loads the Python plugin at @a pluginDir (metadata.txt + __init__.py).
     * Returns the adapter (the host retains ownership) or nullptr on failure,
     * in which case @a errorOut receives the reason.
     *
     * ADR 0044: accepts a PluginLoadContext instead of 3 raw pointers.
     */
    PythonPluginAdapter *loadPlugin( const QString &pluginDir,
                                     const PluginLoadContext &context,
                                     QString *errorOut = nullptr );

    /// Backward-compatible overload — wraps individual arguments into PluginLoadContext.
    PythonPluginAdapter *loadPlugin( const QString &pluginDir,
                                     sicnu::data::DataManager *dataManager,
                                     QMenu *pluginMenu,
                                     ActiveViewHost *activeViewHost,
                                     QString *errorOut = nullptr )
    {
      return loadPlugin( pluginDir, PluginLoadContext{ dataManager, pluginMenu, activeViewHost }, errorOut );
    }

    void unloadAll();
    QStringList loadedPlugins() const;

    PythonWorkerProcessPool *pool() const { return m_pool; }

  private:
    bool ensurePool( QString *errorOut );

    int m_poolSize = 2;
    PythonWorkerProcessPool *m_pool = nullptr; // owned
    std::vector<std::unique_ptr<PythonPluginAdapter>> m_adapters;
};

} // namespace sicnu::python::isolated
