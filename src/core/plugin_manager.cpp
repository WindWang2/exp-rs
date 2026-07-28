#include "plugin_manager.h"
#include "app/python/python_plugin_adapter.h"
#include "python_worker_process_pool.h"

#include <QCoreApplication>
#include <QDir>
#include <QPluginLoader>
#include <QLibrary>
#include <QJsonObject>
#include <QSettings>
#include <QFileInfo>

using namespace sicnu::python::isolated;

PluginManager::PluginManager(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
    , m_layerTree(layerTree)
{
}

PluginManager::~PluginManager()
{
    unloadAll();
    if (m_ownsPythonPool && m_pythonPool) {
        m_pythonPool->shutdown();
        delete m_pythonPool;
        m_pythonPool = nullptr;
    }
}

void PluginManager::loadPlugins(const QString &pluginDir)
{
    QDir dir(pluginDir);
    if (!dir.exists()) {
        qWarning() << "Plugin directory not found:" << pluginDir;
        return;
    }

    qDebug() << "Loading plugins from:" << pluginDir;

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
    info.isPython = false;
    m_plugins[interface->name()] = info;

    qDebug() << "Loaded C++ plugin:" << interface->name();
    emit pluginLoaded(interface->name());

    return true;
}

bool PluginManager::loadPythonPlugin(const QString &pluginDir)
{
    const QString metadataPath = pluginDir + "/metadata.txt";
    if (!QFileInfo::exists(metadataPath)) {
        return false;
    }

    QMap<QString, QString> metadata;
    QFile file(metadataPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#') || line.startsWith('['))
                continue;
            int idx = line.indexOf('=');
            if (idx > 0) {
                QString key = line.left(idx).trimmed();
                QString val = line.mid(idx + 1).trimmed();
                metadata[key] = val;
            }
        }
    }

    const QString packageName = QDir(pluginDir).dirName();
    const QString name = metadata.value(QStringLiteral("name"), packageName);
    const QString description = metadata.value(QStringLiteral("description"), QString());
    const QString version = metadata.value(QStringLiteral("version"), QStringLiteral("1.0"));

    if (!m_pythonPool) {
        QString scriptPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../src/python/scripts/worker_daemon.py"));
        if (!QFileInfo::exists(scriptPath)) {
            scriptPath = QDir::current().filePath(QStringLiteral("src/python/scripts/worker_daemon.py"));
        }
        m_pythonPool = new PythonWorkerProcessPool(2, this);
        m_pythonPool->initialize(QString(), scriptPath);
        m_ownsPythonPool = true;
    }

    auto *adapter = new PythonPluginAdapter(pluginDir, packageName, name, description, version, m_appInterface, m_pythonPool);
    if (!adapter->initialize(m_canvas, m_layerTree)) {
        emit pluginError(name, "Python plugin initialization failed");
        delete adapter;
        return false;
    }

    PluginInfo info;
    info.instance = adapter;
    info.loader = nullptr;
    info.loaded = true;
    info.isPython = true;
    m_plugins[name] = info;

    qDebug() << "Loaded Python plugin:" << name;
    emit pluginLoaded(name);

    return true;
}

void PluginManager::unloadAll()
{
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        if (it.value().loaded) {
            it.value().instance->unload();
            if (it.value().loader) {
                it.value().loader->unload();
            } else if (it.value().isPython) {
                delete it.value().instance;
            }
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
