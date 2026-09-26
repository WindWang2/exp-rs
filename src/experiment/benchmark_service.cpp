// benchmark_service.cpp
#include "benchmark_service.h"

#include <QSet>
#include <QStringList>

#include <algorithm>

namespace sicnu::experiment
{

QString BenchmarkService::defKey( const QString &id, quint64 version )
{
    return id + QLatin1Char( '@' ) + QString::number( version );
}

BenchmarkService::BenchmarkService( ExperimentStore *store )
  : m_store( store )
{
}

void BenchmarkService::setStore( ExperimentStore *store )
{
    m_store = store;
}

Result<void> BenchmarkService::hydrateFromStore( qint64 definitionLimit, qint64 resultLimit )
{
    using ResultT = Result<void>;
    if ( !m_store || !m_store->isOpen() )
        return ResultT::success();

    const auto defs = m_store->listBenchmarkDefinitions( 0, definitionLimit );
    if ( !defs )
        return ResultT::failure( defs.diagnostics() );
    for ( const BenchmarkDefinition &definition : defs.value().second )
        m_definitions.insert( defKey( definition.benchmarkId(), definition.benchmarkVersion() ),
                              definition );

    // Results: page via listing each known definition id, plus any already
    // cached. The ids are visited in sorted order (QSet/QHash iteration
    // order is seeded per process — unsorted, which ids hydrate within the
    // remaining-result budget would vary between runs of the same binary).
    QSet<QString> seenIds;
    for ( auto it = m_definitions.constBegin(); it != m_definitions.constEnd(); ++it )
        seenIds.insert( it.value().benchmarkId() );
    QStringList orderedIds = QStringList( seenIds.cbegin(), seenIds.cend() );
    std::sort( orderedIds.begin(), orderedIds.end() );
    qint64 remaining = resultLimit;
    for ( const QString &benchmarkId : orderedIds )
    {
        if ( remaining <= 0 )
            break;
        const auto page = m_store->benchmarkResultsFor( benchmarkId, remaining );
        if ( !page )
            return ResultT::failure( page.diagnostics() );
        const QVector<BenchmarkResult> rows = page.value();
        for ( const BenchmarkResult &result : rows )
        {
            if ( m_resultIndex.contains( result.resultId() ) )
            {
                m_results[m_resultIndex.value( result.resultId() )] = result;
            }
            else
            {
                m_resultIndex.insert( result.resultId(), m_results.size() );
                m_results.append( result );
            }
            --remaining;
        }
    }
    return ResultT::success();
}

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
        // Still ensure store has it when bound (idempotent).
        if ( m_store && m_store->isOpen() )
            return m_store->saveBenchmarkDefinition( definition );
        return Result<void>::success();
    }

    if ( m_store && m_store->isOpen() )
    {
        const auto written = m_store->saveBenchmarkDefinition( definition );
        if ( !written )
            return written;
    }
    m_definitions.insert( key, definition );
    return Result<void>::success();
}

std::optional<BenchmarkDefinition> BenchmarkService::definition( const QString &benchmarkId,
                                                                 quint64 version ) const
{
    const auto it = m_definitions.constFind( defKey( benchmarkId, version ) );
    if ( it != m_definitions.constEnd() )
        return *it;
    if ( m_store && m_store->isOpen() )
        return m_store->benchmarkDefinition( benchmarkId, version );
    return std::nullopt;
}

QVector<BenchmarkDefinition> BenchmarkService::listDefinitions( qint64 limit ) const
{
    if ( m_definitions.isEmpty() && m_store && m_store->isOpen() )
    {
        const auto page = m_store->listBenchmarkDefinitions( 0, limit );
        if ( page )
            return page.value().second;
    }
    // Deterministic order AND deterministic cut: the cache is a QHash, so
    // the unsorted iteration leaked a process-random order (and a random
    // subset once the limit cut) into the listing.
    QVector<BenchmarkDefinition> cached;
    cached.reserve( m_definitions.size() );
    for ( auto it = m_definitions.constBegin(); it != m_definitions.constEnd(); ++it )
        cached.append( it.value() );
    std::sort( cached.begin(), cached.end(),
               []( const BenchmarkDefinition &a, const BenchmarkDefinition &b ) {
                   if ( a.benchmarkId() != b.benchmarkId() )
                       return a.benchmarkId() < b.benchmarkId();
                   return a.benchmarkVersion() < b.benchmarkVersion();
               } );
    if ( cached.size() > limit )
        cached.resize( int( limit ) );
    return cached;
}

Result<BenchmarkResult> BenchmarkService::run( const BenchmarkRunRequest &request )
{
    auto result = BenchmarkRunner::run( request );
    if ( result )
    {
        const auto recorded = recordResult( *result );
        if ( !recorded )
            return Result<BenchmarkResult>::failure( recorded.diagnostics() );
    }
    return result;
}

Result<void> BenchmarkService::recordResult( const BenchmarkResult &result )
{
    if ( m_resultIndex.contains( result.resultId() ) )
    {
        m_results[m_resultIndex.value( result.resultId() )] = result;
    }
    else
    {
        m_resultIndex.insert( result.resultId(), m_results.size() );
        m_results.append( result );
    }
    if ( m_store && m_store->isOpen() )
        return m_store->saveBenchmarkResult( result );
    return Result<void>::success();
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
            return out;
    }
    if ( out.isEmpty() && m_store && m_store->isOpen() )
    {
        const auto page = m_store->benchmarkResultsFor( benchmarkId, limit );
        if ( page )
            return page.value();
        // The store refused to answer; an empty cache view is the honest
        // response here, not a fabricated listing.
        return out;
    }
    return out;
}

std::optional<BenchmarkResult> BenchmarkService::resultById( const QString &resultId ) const
{
    const auto it = m_resultIndex.constFind( resultId );
    if ( it != m_resultIndex.constEnd() )
        return m_results.at( *it );
    if ( m_store && m_store->isOpen() )
        return m_store->benchmarkResultById( resultId );
    return std::nullopt;
}

BenchmarkComparison BenchmarkService::compare( const QString &resultIdA,
                                               const QString &resultIdB ) const
{
    const auto a = resultById( resultIdA );
    const auto b = resultById( resultIdB );
    if ( !a || !b )
    {
        BenchmarkComparison comparison;
        comparison.resultIdA = resultIdA;
        comparison.resultIdB = resultIdB;
        comparison.reasons.append( QStringLiteral( "result id not found" ) );
        return comparison;
    }
    return compareBenchmarkResults( *a, *b );
}

QVector<BenchmarkSeedSummary> BenchmarkService::seedSummary( const QString &benchmarkId,
                                                             const QStringList &metricNames ) const
{
    QVector<BenchmarkResult> subset = resultsFor( benchmarkId, 10000 );
    return summarizeAcrossSeeds( subset, metricNames );
}

} // namespace sicnu::experiment
