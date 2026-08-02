// src/core/plugin_host.cpp
#include "plugin_host.h"

#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
#include "python/isolated/python_plugin_adapter.h"
#include "app/python/sicnu_app_interface.h"
#include "app/project_context.h"
#include "python/isolated/python_plugin_host.h"
#endif

#include <QDir>
#include <QPluginLoader>
#include <QLibrary>
#include <QJsonObject>
#include <QFileInfo>
#include <QDebug>

using namespace sicnu::python::isolated;

PluginHost::PluginHost(int pythonPoolSize, QObject *parent)
    : QObject(parent)
    , m_pythonPoolSize(pythonPoolSize)
{
#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
    m_pythonHost = std::make_unique<PythonPluginHost>(m_pythonPoolSize, this);
#else
    Q_UNUSED(pythonPoolSize);
#endif
}

PluginHost::~PluginHost()
{
    unloadAll();
}

void PluginHost::loadPlugins(const QString &pluginDir)
{
    QDir dir(pluginDir);
    if (!dir.exists()) {
        qWarning() << "PluginHost: Plugin directory not found:" << pluginDir;
        return;
    }

    qDebug() << "PluginHost: Loading plugins from:" << pluginDir;

    // 1. Scan for C++ plugin libraries (.so / .dll)
    for (const QString &fileName : dir.entryList(QDir::Files)) {
        QString filePath = dir.absoluteFilePath(fileName);
        if (QLibrary::isLibrary(filePath)) {
            loadPlugin(filePath);
        }
    }

    // 2. Scan for Python plugin directories containing metadata.txt + __init__.py
    for (const QString &subDirName : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString subDirPath = dir.absoluteFilePath(subDirName);
        if (QFileInfo::exists(subDirPath + "/metadata.txt") && QFileInfo::exists(subDirPath + "/__init__.py")) {
            loadPythonPlugin(subDirPath);
        }
    }
}

bool PluginHost::loadPlugin(const QString &pluginPath)
{
    QPluginLoader *loader = new QPluginLoader(pluginPath, this);
    QJsonObject metadata = loader->metaData();

    if (metadata.isEmpty()) {
        qWarning() << "PluginHost: No metadata in plugin:" << pluginPath;
        delete loader;
        return false;
    }

    QObject *plugin = loader->instance();
    if (!plugin) {
        emit pluginError(pluginPath, loader->errorString());
        qWarning() << "PluginHost: Failed to load plugin:" << pluginPath << loader->errorString();
        delete loader;
        return false;
    }

    SicnuPluginInterface *interface = qobject_cast<SicnuPluginInterface*>(plugin);
    if (!interface) {
        emit pluginError(pluginPath, "Plugin does not implement SicnuPluginInterface");
        qWarning() << "PluginHost: Invalid plugin interface:" << pluginPath;
        delete loader;
        return false;
    }

    if (!interface->initialize(m_appInterface)) {
        emit pluginError(interface->name(), "Plugin initialization failed");
        qWarning() << "PluginHost: Plugin init failed:" << interface->name();
        delete loader;
        return false;
    }

    PluginInfo info;
    info.instance = interface;
    info.loader = loader;
    info.loaded = true;
    info.isPython = false;
    m_plugins[interface->name()] = info;

    qDebug() << "PluginHost: Loaded C++ plugin:" << interface->name();
    emit pluginLoaded(interface->name());

    return true;
}

bool PluginHost::loadPythonPlugin(const QString &pluginDir)
{
#if !defined( SICNU_EMBED_PYTHON ) || !SICNU_EMBED_PYTHON
    Q_UNUSED(pluginDir)
    qWarning() << "PluginHost: Python plugin support is disabled (SICNU_EMBED_PYTHON=OFF)";
    return false;
#else
    if (!m_pythonHost) {
        m_pythonHost = std::make_unique<PythonPluginHost>(m_pythonPoolSize, this);
    }

    // ADR 0044: consolidated context instead of 3 raw pointers.
    sicnu::python::isolated::PluginLoadContext context;
    if (m_appInterface) {
        context.pluginMenu = m_appInterface->pluginMenu();
        context.activeViewHost = m_appInterface->activeViewHost();
        if (m_appInterface->projectContext())
            context.dataManager = &m_appInterface->projectContext()->dataManager();
    }

    QString error;
    PythonPluginAdapter *adapter = m_pythonHost->loadPlugin(pluginDir, context, &error);
    if (!adapter) {
        emit pluginError(QDir(pluginDir).dirName(), error);
        qWarning() << "PluginHost: Python plugin load failed:" << pluginDir << error;
        return false;
    }

    PluginInfo info;
    info.instance = adapter;
    info.loader = nullptr;
    info.loaded = true;
    info.isPython = true;
    m_plugins[adapter->name()] = info;

    qDebug() << "PluginHost: Loaded Python plugin:" << adapter->name();
    emit pluginLoaded(adapter->name());

    return true;
#endif
}

void PluginHost::unloadAll()
{
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        if (it.value().loaded) {
            it.value().instance->unload();
            if (it.value().loader) {
                it.value().loader->unload();
            }
            emit pluginUnloaded(it.key());
        }
    }
#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
    if (m_pythonHost) {
        m_pythonHost->unloadAll();
    }
#endif
    m_plugins.clear();
}

QStringList PluginHost::loadedPlugins() const
{
    return m_plugins.keys();
}

SicnuPluginInterface* PluginHost::plugin(const QString &name) const
{
    auto it = m_plugins.find(name);
    if (it != m_plugins.end() && it.value().loaded) {
        return it.value().instance;
    }
    return nullptr;
}

bool PluginHost::isPluginLoaded(const QString &name) const
{
    auto it = m_plugins.find(name);
    return it != m_plugins.end() && it.value().loaded;
}
