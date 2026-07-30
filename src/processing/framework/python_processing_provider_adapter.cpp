#include "python_processing_provider_adapter.h"
#include "algorithm_engine.h"

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
    for ( auto it = m_algorithms.begin(); it != m_algorithms.end(); ++it )
    {
        if ( !engine.findAlgorithm( it.key() ) )
        {
            engine.registerAlgorithm( it.value() );
        }
    }
}

void PythonProcessingProviderAdapter::addAlgorithm( std::shared_ptr<TaskAlgorithmAdapter> algoAdapter )
{
    if ( !algoAdapter )
        return;

    m_algorithms.insert( algoAdapter->descriptor().id, algoAdapter );
    // Automatically register with global AlgorithmEngine
    AlgorithmEngine::instance().registerAlgorithm( algoAdapter );
}

void PythonProcessingProviderAdapter::removeAlgorithm( const QString &algoId )
{
    m_algorithms.remove( algoId );
}

} // namespace sicnu
