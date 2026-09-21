#include "suitability_provider.h"

namespace sicnu::suitability
{

InMemoryDataProvider::InMemoryDataProvider( DatasetFacts facts )
    : m_facts( std::move( facts ) )
{
}

InMemoryDataProvider::InMemoryDataProvider( sicnu::data::Diagnostic failure )
    : m_failure( std::move( failure ) )
    , m_fails( true )
{
}

sicnu::data::Result<DatasetFacts> InMemoryDataProvider::datasetFacts(
    const QString &datasetVersionId, const FactsLimits &limits ) const
{
    Q_UNUSED( datasetVersionId );
    Q_UNUSED( limits );
    if ( m_fails )
        return sicnu::data::Result<DatasetFacts>::failure( m_failure );
    return sicnu::data::Result<DatasetFacts>::success( m_facts );
}

} // namespace sicnu::suitability
