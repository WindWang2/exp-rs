#include "plugin_manager.h"

#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
#include "app/python/python_plugin_adapter.h"
#include "app/python/sicnu_app_interface.h"
#include "app/project_context.h"
#include "python/isolated/python_worker_process_pool.h"
#endif

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
#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
    if (m_ownsPythonPool && m_pythonPool) {
        m_pythonPool->shutdown();
        delete m_pythonPool;
        m_pythonPool = nullptr;
    }
#endif
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
#if !defined( SICNU_EMBED_PYTHON ) || !SICNU_EMBED_PYTHON
    Q_UNUSED( pluginDir )
    qWarning() << "PluginManager: Python plugin support is disabled (SICNU_EMBED_PYTHON=OFF)";
    return false;
#else
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
        // Resolve worker_daemon.py from common layouts: installed app, source tree
        // relative to the test binary, and cwd when developing from the repo root.
        const QStringList candidates = {
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../src/python/scripts/worker_daemon.py")),
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../src/python/scripts/worker_daemon.py")),
            QDir::current().filePath(QStringLiteral("src/python/scripts/worker_daemon.py")),
            QDir::current().filePath(QStringLiteral("../src/python/scripts/worker_daemon.py")),
        };
        QString scriptPath;
        for (const QString &candidate : candidates) {
            if (QFileInfo::exists(candidate)) {
                scriptPath = QFileInfo(candidate).absoluteFilePath();
                break;
            }
        }
        if (scriptPath.isEmpty()) {
            qWarning() << "PluginManager: worker_daemon.py not found; Python plugins cannot load";
            return false;
        }
        m_pythonPool = new PythonWorkerProcessPool(2, this);
        if (!m_pythonPool->initialize(QString(), scriptPath)) {
            qWarning() << "PluginManager: PythonWorkerProcessPool initialize failed:" << scriptPath;
            delete m_pythonPool;
            m_pythonPool = nullptr;
            return false;
        }
        m_ownsPythonPool = true;
    }

    QMenu *pluginMenu = m_appInterface ? m_appInterface->pluginMenu() : nullptr;
    sicnu::data::DataManager *dataManager = nullptr;
    if ( m_appInterface && m_appInterface->projectContext() )
    {
        dataManager = &m_appInterface->projectContext()->dataManager();
    }
    ActiveViewHost *activeViewHost = m_appInterface ? m_appInterface->activeViewHost() : nullptr;
    auto *adapter = new PythonPluginAdapter( pluginDir, packageName, name, description, version,
                                             dataManager, pluginMenu, activeViewHost, m_pythonPool );
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
#endif
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
