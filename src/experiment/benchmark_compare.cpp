// benchmark_compare.cpp
#include "benchmark_compare.h"

#include <QHash>
#include <QJsonArray>
#include <QtMath>

#include <algorithm>

namespace sicnu::experiment
{

QJsonObject BenchmarkMetricDelta::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "name" ), name );
    if ( !scope.isEmpty() )
        json.insert( QStringLiteral( "scope" ), scope );
    if ( !classCode.isEmpty() )
        json.insert( QStringLiteral( "class" ), classCode );
    json.insert( QStringLiteral( "a" ), a );
    json.insert( QStringLiteral( "b" ), b );
    json.insert( QStringLiteral( "absolute" ), absolute );
    json.insert( QStringLiteral( "relative" ), relative );
    if ( relativeUndefined )
        json.insert( QStringLiteral( "relative_undefined" ), true );
    return json;
}

QJsonObject BenchmarkComparison::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "result_id_a" ), resultIdA );
    json.insert( QStringLiteral( "result_id_b" ), resultIdB );
    json.insert( QStringLiteral( "comparable" ), comparable );
    if ( !reasons.isEmpty() )
        json.insert( QStringLiteral( "reasons" ), QJsonArray::fromStringList( reasons ) );
    QJsonArray arr;
    for ( const BenchmarkMetricDelta &delta : deltas )
        arr.append( delta.toJson() );
    json.insert( QStringLiteral( "deltas" ), arr );
    return json;
}

QJsonObject BenchmarkSeedSummary::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "metric" ), metricName );
    json.insert( QStringLiteral( "scope" ), scope );
    json.insert( QStringLiteral( "mean" ), mean );
    json.insert( QStringLiteral( "stddev" ), stddev );
    json.insert( QStringLiteral( "count" ), count );
    QJsonArray vals;
    for ( double value : values )
        vals.append( value );
    json.insert( QStringLiteral( "values" ), vals );
    return json;
}

BenchmarkComparison compareBenchmarkResults( const BenchmarkResult &a, const BenchmarkResult &b )
{
    BenchmarkComparison comparison;
    comparison.resultIdA = a.resultId();
    comparison.resultIdB = b.resultId();

    if ( a.status() != BenchmarkRunStatus::Completed ||
         b.status() != BenchmarkRunStatus::Completed )
    {
        comparison.reasons.append( QStringLiteral( "both results must be completed" ) );
        return comparison;
    }
    if ( a.benchmarkId() != b.benchmarkId() || a.benchmarkVersion() != b.benchmarkVersion() )
    {
        comparison.reasons.append( QStringLiteral( "benchmark id/version mismatch" ) );
        return comparison;
    }
    if ( a.datasetVersionId() != b.datasetVersionId() ||
         a.splitManifestId() != b.splitManifestId() )
    {
        comparison.reasons.append( QStringLiteral( "dataset/split pin mismatch" ) );
        return comparison;
    }

    comparison.comparable = true;
    QHash<QString, const MetricResult *> indexB;
    for ( const MetricResult &metric : b.metrics() )
    {
        const QString key =
            metric.name + QLatin1Char( '\x1f' ) + metric.scope + QLatin1Char( '\x1f' ) +
            metric.classCode;
        indexB.insert( key, &metric );
    }
    for ( const MetricResult &metricA : a.metrics() )
    {
        const QString key =
            metricA.name + QLatin1Char( '\x1f' ) + metricA.scope + QLatin1Char( '\x1f' ) +
            metricA.classCode;
        const auto it = indexB.constFind( key );
        if ( it == indexB.constEnd() )
            continue;
        BenchmarkMetricDelta delta;
        delta.name = metricA.name;
        delta.scope = metricA.scope;
        delta.classCode = metricA.classCode;
        delta.a = metricA.value;
        delta.b = ( *it )->value;
        delta.absolute = delta.b - delta.a;
        if ( qFuzzyIsNull( delta.a ) )
        {
            delta.relative = 0.0;
            delta.relativeUndefined = true;
        }
        else
        {
            delta.relative = delta.absolute / qAbs( delta.a );
        }
        comparison.deltas.append( delta );
    }
    return comparison;
}

QVector<BenchmarkSeedSummary> summarizeAcrossSeeds( const QVector<BenchmarkResult> &results,
                                                    const QStringList &metricNames )
{
    QHash<QString, BenchmarkSeedSummary> buckets;
    for ( const BenchmarkResult &result : results )
    {
        if ( result.status() != BenchmarkRunStatus::Completed )
            continue;
        for ( const MetricResult &metric : result.metrics() )
        {
            if ( !metricNames.isEmpty() && !metricNames.contains( metric.name ) )
                continue;
            const QString key =
                metric.name + QLatin1Char( '\x1f' ) + metric.scope + QLatin1Char( '\x1f' ) +
                metric.classCode;
            BenchmarkSeedSummary &summary = buckets[key];
            summary.metricName = metric.name;
            summary.scope = metric.scope;
            summary.values.append( metric.value );
        }
    }

    QVector<BenchmarkSeedSummary> out;
    out.reserve( buckets.size() );
    for ( auto it = buckets.begin(); it != buckets.end(); ++it )
    {
        BenchmarkSeedSummary summary = it.value();
        summary.count = summary.values.size();
        if ( summary.count == 0 )
            continue;
        double sum = 0.0;
        for ( double value : summary.values )
            sum += value;
        summary.mean = sum / double( summary.count );
        if ( summary.count >= 2 )
        {
            double accum = 0.0;
            for ( double value : summary.values )
            {
                const double d = value - summary.mean;
                accum += d * d;
            }
            summary.stddev = qSqrt( accum / double( summary.count - 1 ) );
        }
        out.append( summary );
    }
    // Sorted order so the summary layout is deterministic across processes
    // (QHash iteration order is seeded) — the same doctrine the families in
    // comparison_ext follow.
    std::sort( out.begin(), out.end(),
               []( const BenchmarkSeedSummary &a, const BenchmarkSeedSummary &b ) {
                   if ( a.metricName != b.metricName )
                       return a.metricName < b.metricName;
                   return a.scope < b.scope;
               } );
    return out;
}

} // namespace sicnu::experiment
