// src/core/plugin_manager.h
#pragma once

#include <QObject>
#include <QMap>
#include <QStringList>
#include <memory>
#include "interfaces/sicnu_plugin_interface.h"

class QPluginLoader;
class QgsMapCanvas;
class QgsLayerTreeView;

class SicnuAppInterface;

/**
 * @brief Manages loading and unloading of plugins (C++ and Python)
 */
namespace sicnu::python::isolated {
    class PythonPluginHost;
}

class PluginManager : public QObject
{
    Q_OBJECT

public:
    explicit PluginManager(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree, QObject *parent = nullptr);
    ~PluginManager();

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
        SicnuPluginInterface *instance;
        QPluginLoader *loader; // nullptr for Python plugins
        bool loaded;
        bool isPython = false;
    };

    QMap<QString, PluginInfo> m_plugins;
    QgsMapCanvas *m_canvas;
    QgsLayerTreeView *m_layerTree;
    SicnuAppInterface *m_appInterface = nullptr;
    std::unique_ptr<sicnu::python::isolated::PythonPluginHost> m_pythonHost;
};
