// benchmark_service.h — headless BenchmarkService façade for Agent/CLI/D18.
#pragma once

#include "benchmark_compare.h"
#include "benchmark_definition.h"
#include "benchmark_runner.h"

#include <QHash>
#include <QVector>

#include <optional>

namespace sicnu::experiment
{

/// In-memory registry of published definitions + results (process-local).
/// Persistence can ride ExperimentStore artifacts later; this seam is the
/// stable API D18/Agent consume.
class BenchmarkService
{
  public:
    BenchmarkService() = default;

    Result<void> publishDefinition( const BenchmarkDefinition &definition );
    std::optional<BenchmarkDefinition> definition( const QString &benchmarkId,
                                                   quint64 version ) const;
    QVector<BenchmarkDefinition> listDefinitions( qint64 limit = 100 ) const;

    Result<BenchmarkResult> run( const BenchmarkRunRequest &request );
    void recordResult( const BenchmarkResult &result );
    QVector<BenchmarkResult> resultsFor( const QString &benchmarkId,
                                         qint64 limit = 100 ) const;

    BenchmarkComparison compare( const QString &resultIdA, const QString &resultIdB ) const;
    QVector<BenchmarkSeedSummary> seedSummary( const QString &benchmarkId,
                                               const QStringList &metricNames ) const;

  private:
    QHash<QString, BenchmarkDefinition> m_definitions; ///< key = id@version
    QVector<BenchmarkResult> m_results;
    QHash<QString, int> m_resultIndex; ///< resultId → index
};

} // namespace sicnu::experiment
