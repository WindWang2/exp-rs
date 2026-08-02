// src/python/isolated/python_plugin_host.h
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

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
     */
    PythonPluginAdapter *loadPlugin( const QString &pluginDir,
                                     sicnu::data::DataManager *dataManager,
                                     QMenu *pluginMenu,
                                     ActiveViewHost *activeViewHost,
                                     QString *errorOut = nullptr );

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
