// benchmark_compare.h — structured benchmark result comparisons (D19 GOAL §23).
#pragma once

#include "benchmark_runner.h"

#include <QJsonObject>
#include <QVector>

namespace sicnu::experiment
{

struct BenchmarkMetricDelta
{
    QString name;
    QString scope;
    QString classCode;
    double a = 0.0;
    double b = 0.0;
    double absolute = 0.0;
    double relative = 0.0; ///< (b-a)/|a| when a != 0; else 0 with warning
    bool relativeUndefined = false;

    QJsonObject toJson() const;
};

struct BenchmarkComparison
{
    QString resultIdA;
    QString resultIdB;
    bool comparable = false;
    QStringList reasons; ///< why not comparable, when applicable
    QVector<BenchmarkMetricDelta> deltas;

    QJsonObject toJson() const;
};

/// Compare two completed results. Requires matching benchmark id/version and
/// dataset/split pins; otherwise returns comparable=false with reasons.
BenchmarkComparison compareBenchmarkResults( const BenchmarkResult &a, const BenchmarkResult &b );

struct BenchmarkSeedSummary
{
    QString metricName;
    QString scope;
    double mean = 0.0;
    double stddev = 0.0;
    qint64 count = 0;
    QVector<double> values;

    QJsonObject toJson() const;
};

/// Mean/std across completed replicates that share benchmark id/version and
/// metric name. Does not claim statistical significance.
QVector<BenchmarkSeedSummary> summarizeAcrossSeeds( const QVector<BenchmarkResult> &results,
                                                    const QStringList &metricNames );

} // namespace sicnu::experiment
