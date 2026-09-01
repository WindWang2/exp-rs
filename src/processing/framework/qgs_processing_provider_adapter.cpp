#include "qgs_processing_provider_adapter.h"
#include "algorithm_engine.h"
#include "atomic_algorithm_registry.h"
#include "provider_algorithm_adapter.h"

#include <QCoreApplication>
#include <QObject>
#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingprovider.h>
#include <processing/qgsprocessingalgorithm.h>

namespace sicnu {

QgsProcessingProviderAdapter::QgsProcessingProviderAdapter( QString providerId,
                                                            QString providerName,
                                                            ProviderResourceProfile profile,
                                                            ProviderFactory factory )
  : m_providerId( std::move( providerId ) )
  , m_providerName( std::move( providerName ) )
  , m_profile( profile )
  , m_factory( std::move( factory ) )
{
}

QString QgsProcessingProviderAdapter::providerId() const
{
  return m_providerId;
}

QString QgsProcessingProviderAdapter::providerName() const
{
  return m_providerName;
}

ProviderResourceProfile QgsProcessingProviderAdapter::resourceProfile() const
{
  return m_profile;
}

void QgsProcessingProviderAdapter::initialize()
{
  if ( m_initialized )
    return;

  auto *registry = QgsApplication::processingRegistry();
  if ( !registry || !m_factory )
    return;

  // Avoid double-registration when initialize() is re-entered.
  if ( !registry->providerById( m_providerId ) )
  {
    QgsProcessingProvider *provider = m_factory();
    if ( provider )
      registry->addProvider( provider );
  }

  m_initialized = true;
}

void QgsProcessingProviderAdapter::discoverAlgorithms( AlgorithmEngine &engine )
{
  Q_UNUSED( engine );
  auto *registry = QgsApplication::processingRegistry();
  if ( !registry )
    return;

  QgsProcessingProvider *provider = registry->providerById( m_providerId );
  if ( !provider )
    return;

  const auto algs = provider->algorithms();
  for ( const QgsProcessingAlgorithm *alg : algs )
  {
    if ( !alg )
      continue;

    const std::string algId = alg->id().toStdString();
    if ( processing::AtomicAlgorithmRegistry::instance().findAdapter( algId ) )
      continue;

    processing::AtomicAlgorithmRegistry::instance().registerAdapter(
      std::make_shared<processing::ProviderAlgorithmAdapter>( *alg ) );
  }

  // Provider-removal safety (#695): when a provider is unloaded, drop every
  // cached adapter whose algorithm id belongs to it so the Agent catalog never
  // advertises tools whose backing algorithm has been deleted. (execute()
  // additionally re-resolves the live algorithm per run — see
  // provider_algorithm_adapter.cpp.) Installed once; the registry is the
  // connection context, so the hook dies with it.
  static bool s_removalHookInstalled = false;
  if ( !s_removalHookInstalled )
  {
    s_removalHookInstalled = true;
    QObject::connect( registry, &QgsProcessingRegistry::providerRemoved,
                      registry, []( const QString &removedProviderId ) {
        const QString prefix = removedProviderId + QLatin1Char( ':' );
        auto &adapters = processing::AtomicAlgorithmRegistry::instance();
        for ( const auto &desc : adapters.listDescriptors() )
        {
          if ( QString::fromStdString( desc.id ).startsWith( prefix ) )
            adapters.unregisterAdapter( desc.id );
        }
      } );
  }
}

} // namespace sicnu
