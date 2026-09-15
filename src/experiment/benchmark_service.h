// benchmark_service.h — headless BenchmarkService façade for Agent/CLI/D18.
#pragma once

#include "benchmark_compare.h"
#include "benchmark_definition.h"
#include "benchmark_runner.h"
#include "experiment_store.h"

#include <QHash>
#include <QVector>

#include <optional>

namespace sicnu::experiment
{

/// Registry of published definitions + results.
///
/// When constructed with an ExperimentStore, publish/record also persist into
/// that store (D19 natural MLOps home: additive benchmark_* tables). Process-
/// local cache remains for fast compare/seedSummary within a session; cold
/// start hydrates from the store on list/lookup misses.
class BenchmarkService
{
  public:
    BenchmarkService() = default;
    explicit BenchmarkService( ExperimentStore *store );

    ExperimentStore *store() const { return m_store; }
    void setStore( ExperimentStore *store );

    /// Load all persisted definitions/results into the process-local cache
    /// (bounded). Safe to call repeatedly; idempotent for identical content.
    Result<void> hydrateFromStore( qint64 definitionLimit = 500, qint64 resultLimit = 2000 );

    Result<void> publishDefinition( const BenchmarkDefinition &definition );
    std::optional<BenchmarkDefinition> definition( const QString &benchmarkId,
                                                   quint64 version ) const;
    QVector<BenchmarkDefinition> listDefinitions( qint64 limit = 100 ) const;

    Result<BenchmarkResult> run( const BenchmarkRunRequest &request );
    Result<void> recordResult( const BenchmarkResult &result );
    QVector<BenchmarkResult> resultsFor( const QString &benchmarkId,
                                         qint64 limit = 100 ) const;
    std::optional<BenchmarkResult> resultById( const QString &resultId ) const;

    BenchmarkComparison compare( const QString &resultIdA, const QString &resultIdB ) const;
    QVector<BenchmarkSeedSummary> seedSummary( const QString &benchmarkId,
                                               const QStringList &metricNames ) const;

  private:
    static QString defKey( const QString &id, quint64 version );

    ExperimentStore *m_store = nullptr;
    QHash<QString, BenchmarkDefinition> m_definitions; ///< key = id@version
    QVector<BenchmarkResult> m_results;
    QHash<QString, int> m_resultIndex; ///< resultId → index
};

} // namespace sicnu::experiment
