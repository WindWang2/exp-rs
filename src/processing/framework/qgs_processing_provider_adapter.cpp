#include "qgs_processing_provider_adapter.h"
#include "algorithm_engine.h"

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
    if ( engine.findAlgorithm( alg->id() ) )
      continue;

    auto cloned = std::unique_ptr<QgsProcessingAlgorithm>( alg->create() );
    if ( !cloned )
      continue;

    engine.registerAlgorithm(
      std::make_shared<QgsProcessingAlgorithmAdapter>( std::move( cloned ), m_profile ) );
  }
}

} // namespace sicnu
