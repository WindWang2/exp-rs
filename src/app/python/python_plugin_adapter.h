// python_plugin_adapter.h — C++ adapter wrapping Python plugins for PluginManager
#pragma once

#include "interfaces/sicnu_plugin_interface.h"
#include <QString>
#include <QIcon>
#include <memory>

class SicnuAppInterface;

/**
 * Adapter that wraps a Python plugin (folder with metadata.txt + __init__.py)
 * as a C++ SicnuPluginInterface instance, allowing PluginManager to manage both
 * C++ shared-library and Python plugins uniformly.
 */
namespace sicnu::python::isolated {
    class PythonWorkerProcessPool;
    struct WorkerNode;
    class PythonAppInterfaceProxy;
}

class PythonPluginAdapter : public SicnuPluginInterface
{
public:
    explicit PythonPluginAdapter( const QString &pluginDir,
                                  const QString &packageName,
                                  const QString &name,
                                  const QString &description,
                                  const QString &version,
                                  SicnuAppInterface *appInterface,
                                  sicnu::python::isolated::PythonWorkerProcessPool *pool = nullptr );

    ~PythonPluginAdapter() override;

    // SicnuPluginInterface
    QString name() const override { return m_name; }
    QString description() const override { return m_description; }
    QString version() const override { return m_version; }
    QIcon icon() const override { return m_icon; }

    bool initialize( QgsMapCanvas *canvas, QgsLayerTreeView *layerTree ) override;
    void unload() override;

    QString pluginDir() const { return m_pluginDir; }
    QString packageName() const { return m_packageName; }

private:
    QString m_pluginDir;
    QString m_packageName;
    QString m_name;
    QString m_description;
    QString m_version;
    QIcon m_icon;
    SicnuAppInterface *m_appInterface = nullptr;
    sicnu::python::isolated::PythonWorkerProcessPool *m_pool = nullptr;
    sicnu::python::isolated::WorkerNode *m_workerNode = nullptr;
    std::unique_ptr<sicnu::python::isolated::PythonAppInterfaceProxy> m_uiProxy;
    bool m_initialized = false;
};
