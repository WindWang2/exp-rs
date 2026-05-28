// src/core/plugin_manager.h
#pragma once

#include <QObject>
#include <QMap>
#include <QStringList>
#include "interfaces/sicnu_plugin_interface.h"

class QPluginLoader;
class QgsMapCanvas;
class QgsLayerTreeView;

/**
 * @brief Manages loading and unloading of plugins
 */
class PluginManager : public QObject
{
    Q_OBJECT

public:
    explicit PluginManager(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree, QObject *parent = nullptr);
    ~PluginManager();

    void loadPlugins(const QString &pluginDir);
    bool loadPlugin(const QString &pluginPath);
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
        QPluginLoader *loader;
        bool loaded;
    };

    QMap<QString, PluginInfo> m_plugins;
    QgsMapCanvas *m_canvas;
    QgsLayerTreeView *m_layerTree;
};
