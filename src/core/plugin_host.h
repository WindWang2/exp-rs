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

    /**
     * Scans @p pluginDir (review P1-5 hardening):
     *  - native libraries are loaded ONLY when their module name (file base
     *    name without a "lib" prefix) is in trustedNativePlugins(); anything
     *    else found in the directory (Qt SQL drivers, stray/dropped-in DLLs)
     *    is never dlopen'ed. The default allowlist is EMPTY, so a directory
     *    scan loads no native code unless the host opts first-party modules in;
     *  - entries whose canonical path escapes the directory (symlinks) and
     *    world-writable plugin directories are refused;
     *  - Python plugin directories load only when pythonPluginsEnabled().
     * Third-party native plugins belong to the validated exprs registry
     * (manifest, id, checksum, zip-slip checks), not to this legacy channel.
     */
    void loadPlugins(const QString &pluginDir);
    /// Loads one native plugin. The Qt plugin metadata IID must equal
    /// SicnuPluginInterface_iid BEFORE the library is instantiated.
    bool loadPlugin(const QString &pluginPath);

    /// First-party native module names a directory scan may load.
    void setTrustedNativePlugins(const QStringList &moduleNames) { m_trustedNativePlugins = moduleNames; }
    QStringList trustedNativePlugins() const { return m_trustedNativePlugins; }
    /// Whether directory scans load Python plugin directories (default true).
    void setPythonPluginsEnabled(bool enabled) { m_pythonPluginsEnabled = enabled; }
    bool pythonPluginsEnabled() const { return m_pythonPluginsEnabled; }

    /// Module name used for the allowlist: "libfoo.so" / "foo.dll" -> "foo".
    static QString nativeModuleName(const QString &libraryPath);
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
    QStringList m_trustedNativePlugins;
    bool m_pythonPluginsEnabled = true;
    SicnuAppInterface *m_appInterface = nullptr;
    int m_pythonPoolSize = DEFAULT_PYTHON_POOL_SIZE;
    std::unique_ptr<sicnu::python::isolated::PythonPluginHost> m_pythonHost;
};
