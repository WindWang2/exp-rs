#include "algorithm_engine.h"
#include "atomic_algorithm_registry.h"
#include "provider_algorithm_adapter.h"
#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <QSettings>

#include "qgs_processing_provider_adapter.h"
#include "python_processing_provider_adapter.h"
#include "providers/gdal_tools/provider.h"
#include "providers/otb_tools/provider.h"
#include "providers/qgis_algorithms/provider.h"
#include "providers/generic_cli/provider.h"
#include "processing/tools/tool_path_manager.h"

namespace sicnu {

AlgorithmEngine& AlgorithmEngine::instance()
{
    static AlgorithmEngine s_instance;
    return s_instance;
}

void AlgorithmEngine::registerProvider( AlgorithmProviderAdapterPtr provider )
{
    if ( provider )
    {
        m_providers.insert( provider->providerId(), provider );
        provider->initialize();
        provider->discoverAlgorithms( *this );
    }
}

QList<AlgorithmProviderAdapterPtr> AlgorithmEngine::registeredProviders() const
{
    return m_providers.values();
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

    // Canonical Agent-facing algorithm catalog: explicitly initialize the
    // AtomicAlgorithmRegistry (idempotent) so every surface — GUI copilot,
    // CLI, MCP, workflow — observes the same populated catalog.
    sicnu::processing::AtomicAlgorithmRegistry::instance().initialize();

    // Deep AlgorithmProviderAdapter seam: each backend declares a resource profile.
    registerProvider( std::make_shared<QgsProcessingProviderAdapter>(
      QStringLiteral( "gdal_tools" ), QStringLiteral( "GDAL Tools" ),
      ProviderResourceProfile::ExternalCliSubprocess,
      []() -> QgsProcessingProvider * { return new GdalToolsProvider(); } ) );

    registerProvider( std::make_shared<QgsProcessingProviderAdapter>(
      QStringLiteral( "otb_tools" ), QStringLiteral( "OTB Tools" ),
      ProviderResourceProfile::ExternalCliSubprocess,
      []() -> QgsProcessingProvider * { return new OtbToolsProvider(); } ) );

    registerProvider( std::make_shared<QgsProcessingProviderAdapter>(
      QStringLiteral( "qgis_algorithms" ), QStringLiteral( "QGIS Algorithms" ),
      ProviderResourceProfile::QgsTaskThread,
      []() -> QgsProcessingProvider * { return new QgisAlgorithmsProvider(); } ) );

    registerProvider( std::make_shared<QgsProcessingProviderAdapter>(
      QStringLiteral( "custom_tools" ), QStringLiteral( "Generic CLI" ),
      ProviderResourceProfile::ExternalCliSubprocess,
      []() -> QgsProcessingProvider * { return new GenericCliProvider(); } ) );

    registerProvider( std::make_shared<PythonProcessingProviderAdapter>(
      QStringLiteral( "python_plugins" ), QStringLiteral( "Python Plugins" ) ) );
}

} // namespace sicnu
