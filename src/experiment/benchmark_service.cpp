// benchmark_service.cpp
#include "benchmark_service.h"

namespace sicnu::experiment
{

namespace
{
QString defKey( const QString &id, quint64 version )
{
    return id + QLatin1Char( '@' ) + QString::number( version );
}
} // namespace

Result<void> BenchmarkService::publishDefinition( const BenchmarkDefinition &definition )
{
    const auto validated = definition.validate();
    if ( !validated )
        return Result<void>::failure( validated.diagnostics() );

    const QString key = defKey( definition.benchmarkId(), definition.benchmarkVersion() );
    if ( m_definitions.contains( key ) )
    {
        const BenchmarkDefinition &existing = m_definitions.value( key );
        if ( existing.contentDigest() != definition.contentDigest() )
        {
            return Result<void>::failure( Diagnostic{
                QStringLiteral( "experiment.benchmark_conflict" ),
                QStringLiteral( "benchmark %1@%2 already published with different content" )
                    .arg( definition.benchmarkId() )
                    .arg( definition.benchmarkVersion() ),
                DiagnosticSeverity::Error,
            } );
        }
        return Result<void>::success(); // idempotent
    }
    m_definitions.insert( key, definition );
    return Result<void>::success();
}

std::optional<BenchmarkDefinition> BenchmarkService::definition( const QString &benchmarkId,
                                                                 quint64 version ) const
{
    const auto it = m_definitions.constFind( defKey( benchmarkId, version ) );
    if ( it == m_definitions.constEnd() )
        return std::nullopt;
    return *it;
}

QVector<BenchmarkDefinition> BenchmarkService::listDefinitions( qint64 limit ) const
{
    QVector<BenchmarkDefinition> out;
    for ( auto it = m_definitions.constBegin(); it != m_definitions.constEnd(); ++it )
    {
        out.append( it.value() );
        if ( out.size() >= limit )
            break;
    }
    return out;
}

Result<BenchmarkResult> BenchmarkService::run( const BenchmarkRunRequest &request )
{
    auto result = BenchmarkRunner::run( request );
    if ( result )
        recordResult( *result );
    return result;
}

void BenchmarkService::recordResult( const BenchmarkResult &result )
{
    if ( m_resultIndex.contains( result.resultId() ) )
    {
        m_results[m_resultIndex.value( result.resultId() )] = result;
        return;
    }
    m_resultIndex.insert( result.resultId(), m_results.size() );
    m_results.append( result );
}

QVector<BenchmarkResult> BenchmarkService::resultsFor( const QString &benchmarkId,
                                                       qint64 limit ) const
{
    QVector<BenchmarkResult> out;
    for ( const BenchmarkResult &result : m_results )
    {
        if ( result.benchmarkId() != benchmarkId )
            continue;
        out.append( result );
        if ( out.size() >= limit )
            break;
    }
    return out;
}

BenchmarkComparison BenchmarkService::compare( const QString &resultIdA,
                                               const QString &resultIdB ) const
{
    const auto ia = m_resultIndex.constFind( resultIdA );
    const auto ib = m_resultIndex.constFind( resultIdB );
    if ( ia == m_resultIndex.constEnd() || ib == m_resultIndex.constEnd() )
    {
        BenchmarkComparison comparison;
        comparison.resultIdA = resultIdA;
        comparison.resultIdB = resultIdB;
        comparison.reasons.append( QStringLiteral( "result id not found" ) );
        return comparison;
    }
    return compareBenchmarkResults( m_results.at( *ia ), m_results.at( *ib ) );
}

QVector<BenchmarkSeedSummary> BenchmarkService::seedSummary( const QString &benchmarkId,
                                                             const QStringList &metricNames ) const
{
    QVector<BenchmarkResult> subset;
    for ( const BenchmarkResult &result : m_results )
    {
        if ( result.benchmarkId() == benchmarkId )
            subset.append( result );
    }
    return summarizeAcrossSeeds( subset, metricNames );
}

} // namespace sicnu::experiment
