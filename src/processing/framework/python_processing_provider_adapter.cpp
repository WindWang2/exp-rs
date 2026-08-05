#include "python_processing_provider_adapter.h"
#include "algorithm_engine.h"
#include "atomic_algorithm_registry.h"

namespace sicnu {

PythonProcessingProviderAdapter::PythonProcessingProviderAdapter( QString providerId,
                                                                  QString providerName )
  : m_providerId( std::move( providerId ) )
  , m_providerName( std::move( providerName ) )
{
}

QString PythonProcessingProviderAdapter::providerId() const
{
    return m_providerId;
}

QString PythonProcessingProviderAdapter::providerName() const
{
    return m_providerName;
}

ProviderResourceProfile PythonProcessingProviderAdapter::resourceProfile() const
{
    return ProviderResourceProfile::PythonWorkerProcess;
}

void PythonProcessingProviderAdapter::initialize()
{
    m_initialized = true;
}

void PythonProcessingProviderAdapter::discoverAlgorithms( AlgorithmEngine &engine )
{
    m_boundEngine = &engine;
    for ( auto it = m_algorithms.begin(); it != m_algorithms.end(); ++it )
    {
        processing::AtomicAlgorithmRegistry::instance().registerAdapter( it.value() );
    }
}

void PythonProcessingProviderAdapter::addAlgorithm( processing::AtomicAlgorithmAdapterPtr algoAdapter )
{
    if ( !algoAdapter )
        return;

    const QString algoId = QString::fromStdString( algoAdapter->algorithmId() );
    m_algorithms.insert( algoId, algoAdapter );
    processing::AtomicAlgorithmRegistry::instance().registerAdapter( algoAdapter );
}

void PythonProcessingProviderAdapter::removeAlgorithm( const QString &algoId )
{
    m_algorithms.remove( algoId );
    processing::AtomicAlgorithmRegistry::instance().unregisterAdapter( algoId.toStdString() );
}

} // namespace sicnu
