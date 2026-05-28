// src/core/plugin_manager.cpp
#include "plugin_manager.h"

#include <QDir>
#include <QPluginLoader>
#include <QDebug>
#include <QJsonObject>

PluginManager::PluginManager(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
    , m_layerTree(layerTree)
{
}

PluginManager::~PluginManager()
{
    unloadAll();
}

void PluginManager::loadPlugins(const QString &pluginDir)
{
    QDir dir(pluginDir);
    if (!dir.exists()) {
        qWarning() << "Plugin directory not found:" << pluginDir;
        return;
    }

    qDebug() << "Loading plugins from:" << pluginDir;

    for (const QString &fileName : dir.entryList(QDir::Files)) {
        QString filePath = dir.absoluteFilePath(fileName);
        loadPlugin(filePath);
    }
}

bool PluginManager::loadPlugin(const QString &pluginPath)
{
    QPluginLoader *loader = new QPluginLoader(pluginPath, this);
    QJsonObject metadata = loader->metaData();

    if (metadata.isEmpty()) {
        qWarning() << "No metadata in plugin:" << pluginPath;
        delete loader;
        return false;
    }

    QObject *plugin = loader->instance();
    if (!plugin) {
        emit pluginError(pluginPath, loader->errorString());
        qWarning() << "Failed to load plugin:" << pluginPath << loader->errorString();
        delete loader;
        return false;
    }

    SicnuPluginInterface *interface = qobject_cast<SicnuPluginInterface*>(plugin);
    if (!interface) {
        emit pluginError(pluginPath, "Plugin does not implement SicnuPluginInterface");
        qWarning() << "Invalid plugin interface:" << pluginPath;
        delete loader;
        return false;
    }

    if (!interface->initialize(m_canvas, m_layerTree)) {
        emit pluginError(interface->name(), "Plugin initialization failed");
        qWarning() << "Plugin init failed:" << interface->name();
        delete loader;
        return false;
    }

    PluginInfo info;
    info.instance = interface;
    info.loader = loader;
    info.loaded = true;
    m_plugins[interface->name()] = info;

    qDebug() << "Loaded plugin:" << interface->name();
    emit pluginLoaded(interface->name());

    return true;
}

void PluginManager::unloadAll()
{
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        if (it.value().loaded) {
            it.value().instance->unload();
            it.value().loader->unload();
            emit pluginUnloaded(it.key());
        }
    }
    m_plugins.clear();
}

QStringList PluginManager::loadedPlugins() const
{
    return m_plugins.keys();
}

SicnuPluginInterface* PluginManager::plugin(const QString &name) const
{
    auto it = m_plugins.find(name);
    if (it != m_plugins.end() && it.value().loaded) {
        return it.value().instance;
    }
    return nullptr;
}

bool PluginManager::isPluginLoaded(const QString &name) const
{
    auto it = m_plugins.find(name);
    return it != m_plugins.end() && it.value().loaded;
}
