#include "algorithm_engine.h"
#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <QSettings>

#include "providers/gdal_tools/provider.h"
#include "providers/otb_tools/provider.h"
#include "providers/qgis_algorithms/provider.h"
#include "providers/generic_cli/provider.h"
#include "processing/tools/tool_path_manager.h"

namespace sicnu {

QgsProcessingAlgorithmAdapter::QgsProcessingAlgorithmAdapter(std::unique_ptr<QgsProcessingAlgorithm> algo)
    : m_algo(std::move(algo))
{
}

AlgorithmDescriptor QgsProcessingAlgorithmAdapter::descriptor() const
{
    AlgorithmDescriptor desc;
    if (m_algo) {
        desc.id = m_algo->id();
        desc.name = m_algo->displayName();
        desc.group = m_algo->group();
        desc.description = m_algo->shortDescription();
    }
    return desc;
}

bool QgsProcessingAlgorithmAdapter::validateParameters(const QVariantMap& params, QString& error) const
{
    if (!m_algo) {
        error = QStringLiteral("Null QgsProcessingAlgorithm pointer");
        return false;
    }
    QgsProcessingContext context;
    return m_algo->checkParameterValues(params, context, &error);
}

bool QgsProcessingAlgorithmAdapter::execute(const QVariantMap& params, std::function<void(double)> progressCallback, QString& error)
{
    if (!m_algo) {
        error = QStringLiteral("Null QgsProcessingAlgorithm pointer");
        return false;
    }
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    if (progressCallback) {
        QObject::connect(&feedback, &QgsFeedback::progressChanged, [progressCallback](double progress) {
            progressCallback(progress / 100.0);
        });
    }

    bool ok = false;
    m_algo->run(params, context, &feedback, &ok);
    if (!ok) {
        error = feedback.textLog();
    }
    return ok;
}

AlgorithmEngine& AlgorithmEngine::instance()
{
    static AlgorithmEngine s_instance;
    return s_instance;
}

void AlgorithmEngine::registerAlgorithm(std::shared_ptr<TaskAlgorithmAdapter> adapter)
{
    if (adapter) {
        m_adapters.insert(adapter->descriptor().id, adapter);
    }
}

void AlgorithmEngine::registerProcessingAlgorithm(std::unique_ptr<QgsProcessingAlgorithm> algo)
{
    if (algo) {
        auto adapter = std::make_shared<QgsProcessingAlgorithmAdapter>(std::move(algo));
        registerAlgorithm(adapter);
    }
}

QList<AlgorithmDescriptor> AlgorithmEngine::registeredAlgorithms() const
{
    QList<AlgorithmDescriptor> list;
    for (auto it = m_adapters.begin(); it != m_adapters.end(); ++it) {
        list.append(it.value()->descriptor());
    }
    return list;
}

std::shared_ptr<TaskAlgorithmAdapter> AlgorithmEngine::findAlgorithm(const QString& id) const
{
    return m_adapters.value(id, nullptr);
}

void AlgorithmEngine::initialize()
{
    // Load custom GDAL/OTB tool paths from preferences
    QSettings toolSettings;
    const QString gdalPath = toolSettings.value( QStringLiteral( "tools/gdalPath" ) ).toString();
    const QString otbPath = toolSettings.value( QStringLiteral( "tools/otbPath" ) ).toString();
    if ( !gdalPath.isEmpty() )
        ToolPathManager::instance().setGdalPath( gdalPath );
    if ( !otbPath.isEmpty() )
        ToolPathManager::instance().setOtbPath( otbPath );

    if (QgsApplication::processingRegistry()) {
        QgsApplication::processingRegistry()->addProvider(new GdalToolsProvider());
        QgsApplication::processingRegistry()->addProvider(new OtbToolsProvider());
        QgsApplication::processingRegistry()->addProvider(new QgisAlgorithmsProvider());
        QgsApplication::processingRegistry()->addProvider(new GenericCliProvider());
    }

    populateFromProcessingRegistry();
}

void AlgorithmEngine::populateFromProcessingRegistry()
{
    if (!QgsApplication::processingRegistry()) {
        return;
    }
    const auto algs = QgsApplication::processingRegistry()->algorithms();
    for (const QgsProcessingAlgorithm *alg : algs) {
        if (alg && !findAlgorithm(alg->id())) {
            registerProcessingAlgorithm(std::unique_ptr<QgsProcessingAlgorithm>(alg->create()));
        }
    }
}

bool AlgorithmEngine::validateParameters(const QString& id, const QVariantMap& params, QString& error) const
{
    auto adapter = findAlgorithm(id);
    if (!adapter) {
        error = QString(QStringLiteral("Algorithm not registered: %1")).arg(id);
        return false;
    }
    return adapter->validateParameters(params, error);
}

bool AlgorithmEngine::executeAlgorithm(const QString& id, const QVariantMap& params, std::function<void(double)> progressCallback, QString& error)
{
    auto adapter = findAlgorithm(id);
    if (!adapter) {
        error = QString(QStringLiteral("Algorithm not registered: %1")).arg(id);
        return false;
    }
    return adapter->execute(params, progressCallback, error);
}

void AlgorithmEngine::clear()
{
    m_adapters.clear();
}

} // namespace sicnu
